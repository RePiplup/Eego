#include "bt_disk.hpp"
#include "checkpoint.hpp"
#include "http.hpp"
#include "magnet.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/libtorrent.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <sstream>
#include <windows.h>
namespace {
std::vector<char> load(std::string const &path) {
  std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
  if (!f)
    throw std::runtime_error("cannot read " + path);
  return {std::istreambuf_iterator<char>(f), {}};
}
void save(std::filesystem::path const &path, std::vector<char> const &b) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(b.data(), b.size());
  f.flush();
  if (!f)
    throw std::runtime_error("cannot save file");
  f.close();
  core::FileStore::flush(path);
}
lt::settings_pack settings(int port) {
  lt::settings_pack s;
  s.set_str(lt::settings_pack::listen_interfaces,
            "127.0.0.1:" + std::to_string(port));
  s.set_bool(lt::settings_pack::enable_dht, false);
  s.set_bool(lt::settings_pack::enable_lsd, false);
  s.set_bool(lt::settings_pack::enable_upnp, false);
  s.set_bool(lt::settings_pack::enable_natpmp, false);
  s.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
  s.set_int(lt::settings_pack::alert_mask, lt::alert_category::error |
                                               lt::alert_category::storage |
                                               lt::alert_category::status);
  return s;
}
int bt_get(std::string torrent, std::string dir, int port, int pause_after,
           int delay, bool baseline,
           std::shared_ptr<core::FileStore> store = nullptr) {
  auto ti = std::make_shared<lt::torrent_info>(torrent);
  if (ti->num_files() != 1 || ti->info_hashes().has_v2())
    throw std::runtime_error(
        "this migration probe supports single-file v1 torrents only");
  auto directory = std::filesystem::absolute(std::filesystem::u8path(dir))
                       .lexically_normal();
  auto relative =
      std::filesystem::u8path(ti->files().file_path(lt::file_index_t{0}));
  if (relative.has_root_path())
    throw std::runtime_error("absolute torrent path rejected");
  for (auto const &p : relative)
    if (p == "..")
      throw std::runtime_error("torrent path traversal rejected");
  auto first = relative.begin()->u8string();
  for (auto &c : first)
    if (c >= 'A' && c <= 'Z')
      c += 32;
  if (first == "task.resume" || first == "task.hbt-lock" ||
      first == ".hbt-owner")
    throw std::runtime_error("torrent path conflicts with recovery metadata");
  core::TaskLock ownership_lock(directory / "task.hbt-lock");
  auto data_path = directory / relative, owner = directory / ".hbt-owner";
  std::string info;
  const char *hex = "0123456789abcdef";
  for (unsigned char c : ti->info_hashes().v1) {
    info += hex[c >> 4];
    info += hex[c & 15];
  }
  std::ostringstream expected_owner;
  expected_owner << "BT1\n"
                 << info << '\n'
                 << std::quoted(data_path.u8string()) << '\n'
                 << ti->total_size() << '\n';
  if (std::filesystem::exists(owner)) {
    if (core::load_record(owner) != expected_owner.str())
      throw std::runtime_error(
          "torrent ownership mismatch; existing data preserved");
  } else {
    if (std::filesystem::exists(data_path) ||
        std::filesystem::exists(directory / "task.resume"))
      throw std::runtime_error(
          "unowned BT file or resume record; refusing overwrite");
    core::save_record(owner, expected_owner.str());
  }
  auto started = std::chrono::steady_clock::now();
  if (!store)
    store = std::make_shared<core::FileStore>();
  auto metrics = std::make_shared<core::DiskMetrics>();
  bool paused = false;
  int64_t final_done = 0;
  {
    lt::session_params sp(settings(0));
    if (!baseline)
      sp.disk_io_constructor = core::disk_factory(store, metrics, delay);
    lt::session session(sp);
    lt::add_torrent_params p;
    auto resume = std::filesystem::u8path(dir) / "task.resume";
    bool resumed = std::filesystem::exists(resume);
    if (resumed) {
      auto b = core::load_record(resume);
      auto prior = lt::read_resume_data(
          {b.data(), static_cast<std::ptrdiff_t>(b.size())});
      if (prior.info_hashes != ti->info_hashes())
        throw std::runtime_error("resume torrent identity mismatch");
      // Never import file remaps, trust flags or paths from a recovery payload.
      // Our custom disk backend rechecks existing pieces against trusted
      // .torrent metadata.
    }
    p.ti = ti;
    p.save_path = dir;
    p.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused |
                 lt::torrent_flags::seed_mode);
    auto h = session.add_torrent(p);
    if (pause_after > 0)
      h.set_download_limit(1024 * 1024);
    auto deadline = started + std::chrono::seconds(120);
    auto connect_at = started;
    bool finished = false, pausing = false, resume_requested = false;
    while (std::chrono::steady_clock::now() < deadline) {
      auto now = std::chrono::steady_clock::now();
      auto st = h.status();
      if (st.errc)
        throw std::runtime_error(st.errc.message());
      final_done = st.total_done;
      if (!pausing && now >= connect_at) {
        h.connect_peer(
            {lt::make_address("127.0.0.1"), static_cast<unsigned short>(port)});
        connect_at = now + std::chrono::seconds(1);
      }
      if (!pausing && pause_after > 0 && st.total_done >= pause_after &&
          !st.is_seeding) {
        h.pause();
        pausing = true;
      }
      if (st.is_seeding && !pausing) {
        finished = true;
        break;
      }
      std::vector<lt::alert *> alerts;
      session.pop_alerts(&alerts);
      for (auto *a : alerts) {
        if (lt::alert_cast<lt::torrent_paused_alert>(a) && pausing &&
            !resume_requested) {
          h.save_resume_data(lt::torrent_handle::flush_disk_cache |
                             lt::torrent_handle::save_info_dict);
          resume_requested = true;
        }
        if (auto *r = lt::alert_cast<lt::save_resume_data_alert>(a)) {
          auto bytes = lt::write_resume_data_buf(r->params);
          core::save_record(resume, std::string(bytes.begin(), bytes.end()));
          paused = true;
        }
        if (auto *r = lt::alert_cast<lt::save_resume_data_failed_alert>(a))
          throw std::runtime_error(r->message());
        if (auto *r = lt::alert_cast<lt::file_error_alert>(a))
          throw std::runtime_error(r->message());
      }
      if (paused)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!finished && !paused)
      throw std::runtime_error("BT download/pause timed out");
    store->barrier();
    core::FileStore::flush(
        std::filesystem::u8path(dir) /
        std::filesystem::u8path(ti->files().file_path(lt::file_index_t{0})));
    if (finished && std::filesystem::exists(resume))
      std::filesystem::remove(resume);
  }
  store->barrier();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started)
                .count();
  if (!baseline && (metrics->writes != metrics->completed ||
                    metrics->wrong_thread != 0 || metrics->flush_errors != 0))
    throw std::runtime_error("disk callback/flush invariant failed");
  std::cout << "{\"protocol\":\"bt\",\"baseline\":"
            << (baseline ? "true" : "false")
            << ",\"completed\":" << (paused ? "false" : "true")
            << ",\"total_done\":" << final_done << ",\"elapsed_ms\":" << ms
            << ",\"writes\":" << metrics->writes
            << ",\"write_callbacks\":" << metrics->completed
            << ",\"backpressure\":" << metrics->blocked
            << ",\"wakeups\":" << metrics->wakeups
            << ",\"peak_queue\":" << metrics->peak_queue
            << ",\"wrong_callback_thread\":" << metrics->wrong_thread << "}\n";
  return paused ? 3 : 0;
}
} // namespace
int run(int argc, char **argv) try {
  if(argc==4&&std::string(argv[1])=="magnet-inspect") {core::HttpControl c;if(auto nodes=std::getenv("HBT_DHT_BOOTSTRAP"))c.bt_bootstrap_nodes=nodes;return core::magnet_download(argv[2],argv[3],c,true);}
  if(argc==4&&std::string(argv[1])=="magnet-select") {std::vector<int> indexes;std::istringstream in(argv[3]);std::string n;while(std::getline(in,n,','))indexes.push_back(std::stoi(n));core::select_magnet_files(argv[2],indexes);return 0;}
  if(argc==3&&std::string(argv[1])=="magnet-files") {for(auto const&f:core::magnet_files(argv[2]))std::cout<<f.index<<' '<<f.size<<' '<<std::quoted(f.path)<<'\n';return 0;}
  if(argc==4&&std::string(argv[1])=="magnet-uri") {
    lt::torrent_info ti(argv[2]);std::cout<<lt::make_magnet_uri(ti)<<"&x.pe=127.0.0.1:"<<std::stoi(argv[3])<<'\n';return 0;
  }
  if(argc==4&&std::string(argv[1])=="make-torrent-dir") {
    auto path=std::filesystem::absolute(std::filesystem::u8path(argv[2]));lt::file_storage fs;
    lt::add_files(fs,path.u8string());lt::create_torrent t(fs,256*1024,lt::create_torrent::v1_only);
    lt::set_piece_hashes(t,path.parent_path().u8string());std::vector<char> b;lt::bencode(std::back_inserter(b),t.generate());save(std::filesystem::u8path(argv[3]),b);return 0;
  }
  if(argc>=4&&std::string(argv[1])=="magnet-get") {
    core::HttpControl c;std::atomic<bool> ended{false};
    std::thread stopper;
    if(argc==5){int milliseconds=std::stoi(argv[4]);stopper=std::thread([&]{auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);while(!ended&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(10));if(!ended)c.pause=true;});}
    int result=1;
    try{result=core::magnet_download(argv[2],argv[3],c);}catch(...){ended=true;if(stopper.joinable())stopper.join();throw;}
    ended=true;if(stopper.joinable())stopper.join();
    std::cout<<"{\"completed\":"<<(result==0?"true":"false")<<",\"verified_bytes\":"<<c.verified<<",\"manifest_sha256\":\""<<c.result_sha256<<"\"}\n";return result;
  }
  if (argc < 2)
    throw std::runtime_error(
        "commands: http URL OUTPUT SEGMENTS [PAUSE_CHUNKS]; make-torrent FILE "
        "TORRENT; bt-seed TORRENT DIR PORT; bt-get|bt-baseline-get TORRENT DIR "
        "PORT [PAUSE_BYTES] [DISK_DELAY_MS]");
  std::string mode = argv[1];
  if (mode == "mixed" && argc == 7) {
    auto store = std::make_shared<core::FileStore>();
    auto http = std::async(std::launch::async, [&] {
      return core::http_download(argv[2], argv[3], 4, 0, store);
    });
    int bt = bt_get(argv[4], argv[5], std::stoi(argv[6]), 0, 1, false, store);
    int result = http.get();
    store->barrier();
    std::cout << "{\"protocol\":\"mixed\",\"shared_store_bytes\":"
              << store->bytes << ",\"shared_store_writes\":" << store->writes
              << "}\n";
    return bt ? bt : result;
  }
  if (mode == "http" && argc >= 5)
    return core::http_download(argv[2], argv[3], std::stoi(argv[4]),
                               argc > 5 ? std::stoi(argv[5]) : 0);
  if (mode == "make-torrent" && argc == 4) {
    auto path = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    lt::file_storage fs;
    fs.add_file(path.filename().u8string(), std::filesystem::file_size(path));
    lt::create_torrent t(fs, 256 * 1024, lt::create_torrent::v1_only);
    lt::set_piece_hashes(t, path.parent_path().u8string());
    std::vector<char> b;
    lt::bencode(std::back_inserter(b), t.generate());
    save(std::filesystem::u8path(argv[3]), b);
    return 0;
  }
  if (mode == "bt-seed" && argc >= 5) {
    lt::session s(settings(std::stoi(argv[4])));
    lt::add_torrent_params p;
    p.ti = std::make_shared<lt::torrent_info>(argv[2]);
    p.save_path = argv[3];
    p.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
    auto h = s.add_torrent(p);
    if(argc>5)h.set_upload_limit(std::stoi(argv[5]));
    bool ready = false;
    int seconds=argc>6?std::stoi(argv[6]):240;
    if(seconds<1||seconds>86400)throw std::runtime_error("invalid seed test duration");
    for (int i = 0; i < seconds*50; ++i) {
      auto st = h.status();
      if (st.errc)
        throw std::runtime_error(st.errc.message());
      if (st.is_seeding && !ready) {
        std::cout << "SEED_READY " << s.listen_port() << std::endl;
        ready = true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return 0;
  }
  if ((mode == "bt-get" || mode == "bt-baseline-get") && argc >= 5)
    return bt_get(argv[2], argv[3], std::stoi(argv[4]),
                  argc > 5 ? std::stoi(argv[5]) : 0,
                  argc > 6 ? std::stoi(argv[6]) : 0, mode == "bt-baseline-get");
  throw std::runtime_error("invalid command arguments");
} catch (std::exception const &e) {
  std::cerr << "ERROR: " << e.what() << '\n';
  return 1;
}

// Windows supplies UTF-16 arguments. Keep the internal protocol/path boundary
// UTF-8 regardless of the user's active ANSI code page.
int wmain(int argc, wchar_t **wide_argv) try {
  std::vector<std::string> arguments;
  arguments.reserve(argc);
  for (int i = 0; i < argc; ++i) {
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i],
                                -1, nullptr, 0, nullptr, nullptr);
    if (!n)
      throw std::runtime_error("invalid UTF-16 command argument");
    std::string value(n, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i],
                             -1, value.data(), n, nullptr, nullptr))
      throw std::runtime_error("argument conversion failed");
    value.pop_back();
    arguments.push_back(std::move(value));
  }
  std::vector<char *> argv;
  for (auto &value : arguments)
    argv.push_back(value.data());
  argv.push_back(nullptr);
  return run(argc, argv.data());
} catch (std::exception const &e) {
  std::cerr << "ERROR: " << e.what() << '\n';
  return 1;
}
