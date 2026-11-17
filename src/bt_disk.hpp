#pragma once
#include "file_store.hpp"
#include <libtorrent/session_params.hpp>
namespace core {
struct DiskMetrics {
  std::atomic<std::uint64_t> writes{0}, bytes{0}, completed{0}, blocked{0},
      wakeups{0}, peak_queue{0}, callbacks{0}, wrong_thread{0}, flush_errors{0};
};
lt::disk_io_constructor_type disk_factory(std::shared_ptr<FileStore>,
                                          std::shared_ptr<DiskMetrics>,
                                          int delay_ms, std::filesystem::path boundary = {});
} // namespace core
