#include "http.hpp"
#include "http_redirect.hpp"
#include "browser_session.hpp"
#include "aria2/HttpHeader.h"
#include "aria2/HttpHeaderProcessor.h"
#include "aria2/Range.h"
#include "checkpoint.hpp"
#include "file_store.hpp"
#include "file_identity.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <libtorrent/hasher.hpp>
#include <sstream>
#include <windows.h>
#include <winhttp.h>
#include <shlwapi.h>
#include <set>
namespace core {
namespace {
struct PauseRequested {};
std::wstring wide(std::string const &s) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                              int(s.size()), nullptr, 0);
  if (!n)
    throw std::runtime_error("invalid UTF-8");
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}
struct Internet {
  HINTERNET h = nullptr;
  ~Internet() {
    if (h)
      WinHttpCloseHandle(h);
  }
  Internet() = default;
  Internet(Internet const &) = delete;
  void reset() { if (h) WinHttpCloseHandle(h); h = nullptr; }
};
void check(BOOL b) {
  if (!b)
    throw std::runtime_error("WinHTTP error " + std::to_string(GetLastError()));
}
struct Response {
  Internet session, connection, request;
  std::unique_ptr<aria2::HttpHeader> header;
  bool unknown_length = false;
  Response(std::string const &url, std::string const &range,
           std::string const &etag, BrowserSession const* auth = nullptr, bool discovery = false) {
    auto u = wide(url);
    std::set<std::wstring> visited;
    bool secure_previous = false;
    for (unsigned hop = 0; ; ++hop) {
    auto fragment = u.find(L'#');
    if (fragment != std::wstring::npos) u.resize(fragment);
    if (!visited.insert(u).second)
      throw std::runtime_error("HTTP redirect loop; download stopped");
    auto c = checked_http_url(u, secure_previous);
    secure_previous = c.nScheme == INTERNET_SCHEME_HTTPS;
    std::wstring host(c.lpszHostName, c.dwHostNameLength),
        target(c.lpszUrlPath, c.dwUrlPathLength);
    if (c.dwExtraInfoLength)
      target.append(c.lpszExtraInfo, c.dwExtraInfoLength);
    if (target.empty())
      target = L"/";
    session.h = WinHttpOpen(L"HandmadeBT/0.8.2",
                            WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    check(session.h != nullptr);
    check(WinHttpSetTimeouts(session.h, discovery?2000:5000, discovery?2000:5000, discovery?2500:10000, discovery?2500:10000));
    connection.h = WinHttpConnect(session.h, host.c_str(), c.nPort, 0);
    check(connection.h != nullptr);
    request.h = WinHttpOpenRequest(
        connection.h, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        c.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    check(request.h != nullptr);
    // Default Windows certificate/hostname validation is deliberately retained.
    DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
    check(WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &disable,
                           sizeof(disable)));
    DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    check(WinHttpSetOption(request.h, WINHTTP_OPTION_AUTOLOGON_POLICY,
                          &logon, sizeof(logon)));
    std::string hs = "Accept-Encoding: identity\r\n";
    if(discovery) hs+="Accept: application/dns-json\r\n";
    if (!range.empty())
      hs += "Range: bytes=" + range + "\r\n";
    if (!etag.empty())
      hs += "If-Range: " + etag + "\r\n";
    if(auth&&!auth->url.empty()&&browser_session::same_origin(wide(auth->url),u)) {
      std::string cookies;std::wstring current(c.lpszUrlPath,c.dwUrlPathLength);if(current.empty())current=L"/";
      for(auto const&cookie:auth->cookies) {
        auto path=wide(cookie.path);bool match=current==path||(current.rfind(path,0)==0&&(path.back()==L'/'||(current.size()>path.size()&&current[path.size()]==L'/')));
        if(match&&(!cookie.secure||c.nScheme==INTERNET_SCHEME_HTTPS)) {if(!cookies.empty())cookies+="; ";cookies+=cookie.name+"="+cookie.value;}
      }
      if(!cookies.empty())hs+="Cookie: "+cookies+"\r\n";
      if(!auth->referer.empty())hs+="Referer: "+auth->referer+"\r\n";
      if(!auth->user_agent.empty())hs+="User-Agent: "+auth->user_agent+"\r\n";
    }
    auto wh = wide(hs);
    check(WinHttpSendRequest(request.h, wh.c_str(), DWORD(wh.size()), nullptr,
                             0, 0, 0));
    check(WinHttpReceiveResponse(request.h, nullptr));
    DWORD size = 0;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                        WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size,
                        WINHTTP_NO_HEADER_INDEX);
    if (size > 65536 || !size)
      throw std::runtime_error("invalid/oversized header");
    std::vector<wchar_t> raw(size / sizeof(wchar_t));
    check(WinHttpQueryHeaders(request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                              WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &size,
                              WINHTTP_NO_HEADER_INDEX));
    // WinHTTP exposes Unicode headers (including non-ASCII filenames).
    // Encode values as UTF-8; protocol field names/numbers remain ASCII.
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, raw.data(),
                                    -1, nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("invalid Unicode response header");
    std::string bytes(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, raw.data(), -1,
                        bytes.data(), count, nullptr, nullptr);
    bytes.pop_back();
    aria2::HttpHeaderProcessor parser(
        aria2::HttpHeaderProcessor::CLIENT_PARSER);
    if (!parser.parse(bytes))
      throw std::runtime_error("incomplete response header");
    header = parser.getResult();
    int status = header->getStatusCode();
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      if (discovery) throw std::runtime_error("unexpected DNS redirect");
      if (hop >= 10) throw std::runtime_error("HTTP redirect limit (10) exceeded");
      auto locations = header->findAll(aria2::HttpHeader::LOCATION);
      if (locations.size() != 1 || locations.front().empty())
        throw std::runtime_error("redirect requires exactly one Location");
      auto location = wide(locations.front());
      if (location.find_first_of(L"\r\n\t") != std::wstring::npos)
        throw std::runtime_error("invalid redirect Location");
      std::vector<wchar_t> combined(8193);
      DWORD length = DWORD(combined.size());
      if (FAILED(UrlCombineW(u.c_str(), location.c_str(), combined.data(), &length, 0)))
        throw std::runtime_error("invalid/oversized redirect Location");
      u.assign(combined.data(), length);
      request.reset(); connection.reset(); session.reset();
      continue;
    }
    if ((status == 401 || status == 403) && browser_session::same_origin(wide(url),u))
      throw BrowserAuthorizationError("HTTP " + std::to_string(status) + "; server denied access; check website login or permissions");
    if (status != 200 && status != 206)
      throw std::runtime_error("HTTP " + std::to_string(status) +
          (status == 401 || status == 403 ? "; server denied access; browser session is not shared" : "; download stopped"));
    auto content_type = header->find(aria2::HttpHeader::CONTENT_TYPE);
    std::transform(content_type.begin(), content_type.end(), content_type.begin(),
                   [](unsigned char ch) { return char(std::tolower(ch)); });
    if (content_type.find("text/html") != std::string::npos ||
        content_type.find("application/xhtml+xml") != std::string::npos) {
      std::wstring response_path(c.lpszUrlPath,c.dwUrlPathLength);
      std::transform(response_path.begin(),response_path.end(),response_path.begin(),::towlower);
      if(hop>0 && (response_path.find(L"/login")!=std::wstring::npos || response_path.find(L"/signin")!=std::wstring::npos))
        throw BrowserAuthorizationError("server redirected download to a login page; open the original website in browser");
      throw std::runtime_error("server returned an HTML page, not a downloadable file; check the link in browser");
    }
    auto transfer = header->find(aria2::HttpHeader::TRANSFER_ENCODING);
    std::transform(transfer.begin(), transfer.end(), transfer.begin(),
                   [](unsigned char ch) { return char(std::tolower(ch)); });
    bool chunked = transfer == "chunked" && status == 200 &&
              !header->defined(aria2::HttpHeader::CONTENT_LENGTH);
    if ((header->defined(aria2::HttpHeader::TRANSFER_ENCODING) && !chunked) ||
        header->defined(aria2::HttpHeader::CONTENT_ENCODING))
      throw std::runtime_error(
          "encoded/unknown-length bodies not migrated yet");
    unknown_length = status == 200 && !header->defined(aria2::HttpHeader::CONTENT_LENGTH);
    break;
    }
  }
  std::vector<char> read(size_t limit) {
    std::vector<char> b(std::min<size_t>(limit, 65536));
    DWORD n = 0;
    check(WinHttpReadData(request.h, b.data(), DWORD(b.size()), &n));
    b.resize(n);
    return b;
  }

};
void validate(Response const &r, std::uint64_t start, std::uint64_t end,
              std::uint64_t size, std::string const &etag) {
  auto &h = *r.header;
  auto v = h.getRange();
  if (h.getStatusCode() != 206 ||
      !h.defined(aria2::HttpHeader::CONTENT_RANGE) ||
      v.startByte != int64_t(start) || v.endByte != int64_t(end) ||
      v.entityLength != int64_t(size))
    throw std::runtime_error(
        "Range rejected or mismatched; file not committed");
  if (!etag.empty() && h.find(aria2::HttpHeader::ETAG) != etag)
    throw std::runtime_error("resource changed (ETag)");
  if (h.findAll(aria2::HttpHeader::CONTENT_LENGTH).size() != 1 ||
      std::stoull(h.find(aria2::HttpHeader::CONTENT_LENGTH)) != end - start + 1)
    throw std::runtime_error("invalid range Content-Length");
}
} // namespace
std::string resolve_bootstrap_json(std::string const &hostname, bool alternate) {
  std::string url = alternate ? "https://cloudflare-dns.com/dns-query?type=A&name=" : "https://dns.google/resolve?type=A&name=";
  Response response(url+hostname, "", "", nullptr, true);
  std::string json;
  while(json.size()<=16384) {
    auto bytes=response.read(4096);
    if(bytes.empty())return json;
    json.append(bytes.begin(),bytes.end());
  }
  throw std::runtime_error("oversized DNS response");
}

int http_download(std::string const &url, std::string const &path, int segments,
                  int stop_after, std::shared_ptr<FileStore> shared_store,
                  HttpControl *control) {
  if (segments < 1 || segments > 16)
    throw std::runtime_error("segments must be 1..16");
  const auto started = std::chrono::steady_clock::now();
  const std::uint64_t chunk = 256 * 1024;
  auto out = std::filesystem::u8path(path), part = out;
  part += ".part";
  auto checkpoint = out;
  checkpoint += ".resume";
  if (std::filesystem::exists(out))
    throw std::runtime_error("output exists; refusing overwrite");
  auto lock_path = out;
  lock_path += ".hbt-lock";
  TaskLock task_lock(lock_path,true);
  task_lock.remove_on_release();
  auto identity = std::filesystem::absolute(out).lexically_normal().u8string();
  Response probe(url, "0-0", "", control?&control->browser_session:nullptr);
  bool ranged = probe.header->getStatusCode() == 206;
  auto range = probe.header->getRange();
  bool streaming = probe.unknown_length;
  if (!ranged && probe.header->getStatusCode() != 200)
    throw std::runtime_error("unexpected HTTP status " +
                             std::to_string(probe.header->getStatusCode()));
  if (!streaming && range.entityLength <= 0)
    throw std::runtime_error("server returned an empty body or invalid file size; check the download link and login in browser");
  uint64_t size = streaming ? 0 : uint64_t(range.entityLength);
  if (control) {
    control->total = size;
    control->ranged = ranged;
  }
  auto etag = probe.header->find(aria2::HttpHeader::ETAG);
  if (etag.rfind("W/", 0) == 0)
    etag.clear();
  if (ranged)
    validate(probe, 0, 0, size, etag);
  if (!ranged && !streaming && !control &&
      (segments > 1 || stop_after || std::filesystem::exists(checkpoint)))
    throw std::runtime_error(
        "server does not support Range; use segments=1 without resume");
  if (ranged && etag.empty())
    throw std::runtime_error("strong ETag required for safe segmented/resumed "
                             "download in this probe");
  std::vector<int> done(size_t((size + chunk - 1) / chunk), 0);
  std::vector<std::string> hashes(done.size(), "-");
  bool resume = std::filesystem::exists(checkpoint);
  if (resume && !streaming) {
    std::istringstream f(load_record(checkpoint));
    std::string magic, u, e, target;
    uint64_t n = 0, c = 0;
    if (!(f >> magic >> std::quoted(u) >> std::quoted(e) >>
          std::quoted(target) >> n >> c))
      throw std::runtime_error("truncated HTTP checkpoint");
    if (magic != "HTTP2" || u != url || e != etag || target != identity ||
        n != size || c != chunk)
      throw std::runtime_error("checkpoint identity mismatch");
    for (auto &hash : hashes) {
      if (!(f >> hash) ||
          (hash != "-" && (hash.size() != 64 ||
                           !std::all_of(hash.begin(), hash.end(), [](char c) {
                             return (c >= '0' && c <= '9') ||
                                    (c >= 'a' && c <= 'f');
                           }))))
        throw std::runtime_error("bad checkpoint");
    }
    f >> std::ws;
    if (!f.eof())
      throw std::runtime_error("trailing checkpoint data");
  }
  if (!resume && std::filesystem::exists(part))
    throw std::runtime_error("unowned partial file; refusing overwrite");
  bool existing = std::filesystem::exists(part);
  if (resume && !existing &&
      std::any_of(hashes.begin(), hashes.end(),
                  [](auto const &h) { return h != "-"; }))
    throw std::runtime_error("checkpoint data file missing");
  if (!streaming && existing && std::filesystem::file_size(part) != size)
    throw std::runtime_error("resume file size mismatch");
  if (existing)
    for (size_t i = 0; i < hashes.size(); ++i) {
      if (hashes[i] == "-")
        continue;
      auto b = FileStore::read(part, i * chunk,
                               size_t(std::min(chunk, size - i * chunk)));
      if (sha256({b.data(), b.size()}) == hashes[i])
        done[i] = 1;
      else
        hashes[i] = "-";
    }
  if (!ranged) {
    std::fill(done.begin(), done.end(), 0);
    std::fill(hashes.begin(), hashes.end(), "-");
  }
  auto serialize = [&] {
    std::ostringstream f;
    f << "HTTP2\n"
      << std::quoted(url) << '\n'
      << std::quoted(etag) << '\n'
      << std::quoted(identity) << '\n'
      << size << ' ' << chunk << '\n';
    for (auto const &h : hashes)
      f << h << '\n';
    return f.str();
  };
  uint64_t checkpoint_count = 0, checkpoint_us = 0, checkpoint_wait_us = 0;
  auto persist = [&](std::string const &payload, bool flush_data) {
    auto begin = std::chrono::steady_clock::now();
    if (flush_data)
      FileStore::flush(part);
    save_record(checkpoint, payload);
    checkpoint_us += std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now() - begin).count();
    ++checkpoint_count;
  };
  // Commit ownership/identity BEFORE creating the data file. A kill between
  // these steps is recoverable only when no completed blocks were recorded.
  if (streaming) {
    // Non-range streams always restart from zero. Only reuse the exact partial
    // file for which this URL and destination recorded a Windows file identity.
    auto stream_record = [&] {
      std::ostringstream out;
      out << "HTTPSTREAM1 " << std::quoted(url) << ' ' << std::quoted(identity)
          << ' ' << std::quoted(managed::identity(part,false));
      return out.str();
    };
    if (resume) {
      if (!existing || load_record(checkpoint)!=stream_record())
        throw std::runtime_error("stream checkpoint or partial file identity mismatch; files preserved");
      FileStore::truncate(part);
    } else {
      FileStore::prepare(part,0,false);
      save_record(checkpoint,stream_record());
    }
  } else {
    if ((ranged || control) && !resume) persist(serialize(), false);
    FileStore::prepare(part, size, existing);
  }
  if (control) {
    uint64_t reused = 0;
    for (size_t k = 0; k < done.size(); ++k)
      if (done[k]) reused += std::min(chunk, size - k * chunk);
    control->received = control->verified = reused;
    control->phase = 1;
  }
  if (!shared_store)
    shared_store = std::make_shared<FileStore>();
  auto &store = *shared_store;
  std::atomic<uint64_t> written{0};
  std::mutex lock;
  std::atomic<size_t> next{0};
  std::atomic<int> completed{0};
  std::atomic<bool> failed{false};
  std::exception_ptr failure;
  auto save = [&] {
    auto begin = std::chrono::steady_clock::now();
    auto payload = serialize();
    auto completion = std::make_shared<std::promise<void>>();
    auto future = completion->get_future();
    store.enqueue([&, payload, completion] {
      try {
        persist(payload, true);
        completion->set_value();
      } catch (...) {
        completion->set_exception(std::current_exception());
      }
    });
    future.get();
    checkpoint_wait_us += std::chrono::duration_cast<std::chrono::microseconds>(
                              std::chrono::steady_clock::now() - begin).count();
  };
  auto write_body = [&](Response &r, uint64_t start, uint64_t length) {
    lt::hasher256 hash;
    uint64_t at = 0;
    while (streaming || at < length) {
      if (control && control->pause) throw PauseRequested{};
      auto b = r.read(streaming ? 65536 : size_t(std::min<uint64_t>(65536, length-at)));
      if (streaming && b.empty()) break;
      if (b.empty())
        throw std::runtime_error("truncated HTTP body");
      at += b.size();
      auto n = b.size();
      hash.update({b.data(), static_cast<std::ptrdiff_t>(b.size())});
      store.write_wait(part, start + at - n, std::move(b));
      written += n;
      if (control) control->received += n;
    }
    if (streaming) {
      if (!at) throw std::runtime_error("empty body");
      size=at;
      if(control) control->total=size;
    } else if (!r.read(1).empty())
      throw std::runtime_error("oversized HTTP body");
    std::string result;
    const char *hex = "0123456789abcdef";
    for (unsigned char c : hash.final()) {
      result += hex[c >> 4];
      result += hex[c & 15];
    }
    return result;
  };
  std::string whole_received_hash;
  bool body_finished=false;
  if (!ranged) {
    try {
      whole_received_hash = write_body(probe, 0, size);
      body_finished=true;
      std::fill(done.begin(), done.end(), 1);
    } catch (PauseRequested const &) {
      // GUI-owned non-Range tasks restart from zero; no partial data is trusted.
    }
  } else {
    // A deterministic pause boundary: finish at most N complete chunks, then
    // persist.
    std::atomic<int> claimed{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < segments; ++i)
      workers.emplace_back([&] {
        try {
          while (!failed) {
            if (control && control->pause) break;
            size_t k = next++;
            if (k >= done.size())
              break;
            if (done[k])
              continue;
            if (stop_after > 0 && claimed.fetch_add(1) >= stop_after)
              break;
            uint64_t start = k * chunk, end = std::min(size, start + chunk) - 1;
            Response r(url, std::to_string(start) + "-" + std::to_string(end),
                       etag, control?&control->browser_session:nullptr);
            validate(r, start, end, size, etag);
            auto hash = write_body(r, start, end - start + 1);
            {
              std::lock_guard<std::mutex> l(lock);
              hashes[k] = std::move(hash);
              done[k] = 1;
              ++completed;
              if (control) control->verified += end - start + 1;
              // Bound uncheckpointed completed data to two chunks (512 KiB).
              // Save still flushes data before committing hashes; pause/error/end
              // always saves the remainder after all workers have stopped.
              if (completed % 2 == 0)
                save();
            }
          }
        } catch (PauseRequested const &) {
          // Interactive pause: leave an incomplete chunk unrecorded.
        } catch (...) {
          failed = true;
          std::lock_guard<std::mutex> l(lock);
          if (!failure)
            failure = std::current_exception();
        }
      });
    for (auto &t : workers)
      t.join();
    store.barrier();
    save();
    if (failure)
      std::rethrow_exception(failure);
  }
  bool finished = !ranged ? body_finished :
      std::all_of(done.begin(), done.end(), [](int d) { return d == 1; });
  FileStore::flush(part);
  if (finished) {
    if (control) control->phase = 2;
    lt::hasher256 full;
    for (uint64_t k = 0; k*chunk < size; ++k) {
      auto bytes = FileStore::read(part, k * chunk,
                                  size_t(std::min(chunk, size - k * chunk)));
      if (ranged && sha256({bytes.data(), bytes.size()}) != hashes[k])
        throw std::runtime_error("on-disk chunk checksum mismatch");
      full.update({bytes.data(), static_cast<std::ptrdiff_t>(bytes.size())});
    }
    std::string actual;
    const char *hex = "0123456789abcdef";
    for (unsigned char c : full.final()) {
      actual += hex[c >> 4]; actual += hex[c & 15];
    }
    if (!ranged && actual != whole_received_hash)
      throw std::runtime_error("on-disk HTTP checksum mismatch");
    if (control && !control->expected_sha256.empty() &&
        control->expected_sha256 != actual)
      throw std::runtime_error("expected SHA-256 mismatch; file not committed");
    if (control && control->before_publish) control->before_publish(part, actual);
    check(MoveFileExW(part.c_str(), out.c_str(), MOVEFILE_WRITE_THROUGH));
    FileStore::notify_created(out,part);
    if (std::filesystem::exists(checkpoint))
      std::filesystem::remove(checkpoint);
    if (control) {
      control->result_sha256 = actual;
      control->verified = control->received = size;
      control->phase = 3;
    }
  }
  else if (control) {
    control->received = control->verified.load();
    control->phase = 4;
  }
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started)
                .count();
  std::cout << "{\"protocol\":\"http\",\"completed\":"
            << (finished ? "true" : "false") << ",\"written_bytes\":" << written
            << ",\"elapsed_ms\":" << ms << ",\"segments\":" << segments
            << ",\"checkpoint_count\":" << checkpoint_count
            << ",\"checkpoint_us\":" << checkpoint_us
            << ",\"checkpoint_wait_us\":" << checkpoint_wait_us << "}\n";
  return finished ? 0 : 3;
}
} // namespace core
