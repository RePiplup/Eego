/*

Copyright (c) 2017-2018, Steven Siloti
Copyright (c) 2019-2021, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

// Interface implementation based on libtorrent examples/custom_storage.cpp.
// Original attribution and BSD-3-Clause terms:
// LICENSES/libtorrent-custom-storage.txt. Modified 2026-10-26: real shared
// FileStore, queued I/O, backpressure and barriers.
#include "bt_disk.hpp"
#include "bt_files.hpp"
#include <boost/asio/post.hpp>
#include <libtorrent/disk_observer.hpp>
#include <libtorrent/libtorrent.hpp>
#include <map>
#include <set>
namespace core {
namespace {
lt::storage_error unsupported() {
  return lt::storage_error(lt::error_code(
      boost::system::errc::operation_not_supported, lt::generic_category()));
}
lt::storage_error disk_error() {
  return lt::storage_error(
      lt::error_code(boost::system::errc::io_error, lt::generic_category()),
      lt::operation_t::file_write);
}
struct Entry {
  std::filesystem::path path;
  lt::file_storage files;
  std::vector<bool> selected;
  lt::storage_error init_error;
};
class Disk final : public lt::disk_interface,
                   public lt::buffer_allocator_interface {
  lt::io_context &io_;
  std::shared_ptr<FileStore> store_;
  std::shared_ptr<DiskMetrics> stats_;
  int delay_;
  std::filesystem::path boundary_;
  std::map<lt::storage_index_t, std::shared_ptr<Entry>> entries_;
  unsigned next_ = 0;
  std::uint64_t queued_ = 0;
  std::vector<std::weak_ptr<lt::disk_observer>> observers_;
  std::thread::id network_;
  void callback() {
    ++stats_->callbacks;
    if (std::this_thread::get_id() != network_)
      ++stats_->wrong_thread;
  }
  template <class F> void barrier(F f) {
    store_->enqueue([this, f = std::move(f)]() mutable {
      boost::asio::post(io_, [this, f = std::move(f)]() mutable {
        callback();
        f();
      });
    });
  }

public:
  Disk(lt::io_context &io, std::shared_ptr<FileStore> s,
       std::shared_ptr<DiskMetrics> m, int delay, std::filesystem::path boundary)
      : io_(io), store_(std::move(s)), stats_(std::move(m)), delay_(delay), boundary_(std::move(boundary)) {}
  lt::storage_holder new_torrent(lt::storage_params const &p,
                                 std::shared_ptr<void> const &) override {
    network_ = std::this_thread::get_id();
    auto e = std::make_shared<Entry>();
    e->files = p.mapped_files ? *p.mapped_files : p.files;
    e->path = std::filesystem::u8path(p.path);
    for(auto i:p.files.file_range())e->selected.push_back(int(i)>=int(p.priorities.size())||p.priorities[i]!=lt::dont_download);
    try {
      validate_bt_files(e->files);
      for(auto i:p.files.file_range())if(!p.files.pad_file_at(i)&&e->selected[int(i)]) {
        auto path=bt_path(e->path,e->files,i);
        FileStore::prepare(path,p.files.file_size(i),std::filesystem::exists(path));
      }
    } catch (...) {
      e->init_error = disk_error();
    }
    auto id = lt::storage_index_t{next_++};
    entries_[id] = e;
    return lt::storage_holder(id, *this);
  }
  void remove_torrent(lt::storage_index_t id) override { entries_.erase(id); }
  void async_read(
      lt::storage_index_t id, lt::peer_request const &r,
      std::function<void(lt::disk_buffer_holder, lt::storage_error const &)> h,
      lt::disk_job_flags_t) override {
    auto e = entries_.at(id);
    store_->enqueue([this, e, r, h] {
      lt::storage_error error;
      std::vector<char> b;
      try {
        b = bt_read(e->path,e->files,r.piece,r.start,r.length,e->selected,boundary_);
      } catch (...) {
        error = disk_error();
      }
      boost::asio::post(io_, [this, h, error, b = std::move(b)] {
        callback();
        char *p = nullptr;
        if (!error) {
          p = new char[b.size()];
          std::copy(b.begin(), b.end(), p);
        }
        h(lt::disk_buffer_holder(*this, p, int(b.size())), error);
      });
    });
  }
  bool async_write(lt::storage_index_t id, lt::peer_request const &r,
                   char const *buf, std::shared_ptr<lt::disk_observer> o,
                   std::function<void(lt::storage_error const &)> h,
                   lt::disk_job_flags_t) override {
    auto e = entries_.at(id);
    std::vector<char> b(buf, buf + r.length);
    queued_ += r.length;
    ++stats_->writes;
    stats_->bytes += r.length;
    stats_->peak_queue = std::max(stats_->peak_queue.load(), queued_);
    bool blocked = queued_ >= 32768;
    if (blocked) {
      ++stats_->blocked;
      observers_.push_back(o);
    }
    store_->enqueue([this, e, r, b = std::move(b), h] {
      lt::storage_error error;
      try {
        if (delay_)
          std::this_thread::sleep_for(std::chrono::milliseconds(delay_));
        bt_write(e->path,e->files,r.piece,r.start,b.data(),int(b.size()),e->selected,boundary_);
        ++store_->writes;
        store_->bytes += b.size();
      } catch (...) {
        error = disk_error();
      }
      boost::asio::post(io_, [this, r, h, error] {
        callback();
        queued_ -= r.length;
        ++stats_->completed;
        h(error);
        if (queued_ < 16384) {
          auto obs = std::move(observers_);
          observers_.clear();
          std::set<lt::disk_observer *> seen;
          for (auto &w : obs)
            if (auto o = w.lock())
              if (seen.insert(o.get()).second) {
                ++stats_->wakeups;
                o->on_disk();
              }
        }
      });
    });
    return blocked;
  }
  void async_hash(lt::storage_index_t id, lt::piece_index_t piece,
                  lt::span<lt::sha256_hash> v2, lt::disk_job_flags_t,
                  std::function<void(lt::piece_index_t, lt::sha1_hash const &,
                                     lt::storage_error const &)>
                      h) override {
    auto e = entries_.at(id);
    store_->enqueue([this, e, piece, v2, h] {
      lt::storage_error error;
      lt::sha1_hash hash;
      try {
        auto b = bt_read(e->path,e->files,piece,0,e->files.piece_size(piece),e->selected,boundary_);
        hash = lt::hasher(b).final();
        if (!v2.empty())
          error = unsupported();
      } catch (...) {
        error = disk_error();
      }
      boost::asio::post(io_, [this, piece, hash, error, h] {
        callback();
        h(piece, hash, error);
      });
    });
  }
  void
  async_hash2(lt::storage_index_t, lt::piece_index_t p, int,
              lt::disk_job_flags_t,
              std::function<void(lt::piece_index_t, lt::sha256_hash const &,
                                 lt::storage_error const &)>
                  h) override {
    barrier([=] { h(p, {}, unsupported()); });
  }
  void async_check_files(
      lt::storage_index_t id, lt::add_torrent_params const *,
      lt::aux::vector<std::string, lt::file_index_t>,
      std::function<void(lt::status_t, lt::storage_error const &)> h) override {
    auto error = entries_.at(id)->init_error;
    barrier([=] { h(error ? lt::status_t::fatal_disk_error : lt::status_t::need_full_check, error); });
  }
  void async_release_files(lt::storage_index_t id,
                           std::function<void()> h) override {
    auto e = entries_.at(id);
    store_->enqueue([this, e, h] {
      try {
        bt_flush(e->path,e->files,e->selected,boundary_);
      } catch (...) {
        ++stats_->flush_errors;
      }
      boost::asio::post(io_, [this, h] {
        callback();
        if (h)
          h();
      });
    });
  }
  void async_stop_torrent(lt::storage_index_t id,
                          std::function<void()> h) override {
    async_release_files(id, std::move(h));
  }
  void async_clear_piece(lt::storage_index_t, lt::piece_index_t p,
                         std::function<void(lt::piece_index_t)> h) override {
    barrier([=] { h(p); });
  }
  void async_move_storage(lt::storage_index_t, std::string p, lt::move_flags_t,
                          std::function<void(lt::status_t, std::string const &,
                                             lt::storage_error const &)>
                              h) override {
    barrier([=] { h(lt::status_t::fatal_disk_error, p, unsupported()); });
  }
  void
  async_rename_file(lt::storage_index_t, lt::file_index_t i, std::string n,
                    std::function<void(std::string const &, lt::file_index_t,
                                       lt::storage_error const &)>
                        h) override {
    barrier([=] { h(n, i, unsupported()); });
  }
  void async_delete_files(
      lt::storage_index_t, lt::remove_flags_t,
      std::function<void(lt::storage_error const &)> h) override {
    barrier([=] { h(unsupported()); });
  }
  void async_set_file_priority(
      lt::storage_index_t,
      lt::aux::vector<lt::download_priority_t, lt::file_index_t> p,
      std::function<
          void(lt::storage_error const &,
               lt::aux::vector<lt::download_priority_t, lt::file_index_t>)>
          h) override {
    barrier([=] { h(unsupported(), p); });
  }
  void free_disk_buffer(char *p) override { delete[] p; }
  void update_stats_counters(lt::counters &) const override {}
  std::vector<lt::open_file_state>
  get_status(lt::storage_index_t) const override {
    return {};
  }
  void settings_updated() override {}
  void submit_jobs() override {}
  void abort(bool) override { store_->barrier(); }
};
} // namespace
lt::disk_io_constructor_type disk_factory(std::shared_ptr<FileStore> store,
                                          std::shared_ptr<DiskMetrics> metrics,
                                          int delay, std::filesystem::path boundary) {
  return
      [store, metrics, delay, boundary](lt::io_context &io,
                              lt::settings_interface const &, lt::counters &) {
        return std::make_unique<Disk>(io, store, metrics, delay, boundary);
      };
}
} // namespace core
