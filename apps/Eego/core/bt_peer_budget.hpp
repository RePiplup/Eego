#pragma once
#include <libtorrent/session.hpp>
#include <algorithm>
#include <list>
#include <mutex>
#include <utility>

namespace core {

struct BtPeerShare {
  int connections;
  int attempts_per_second;
};

constexpr BtPeerShare bt_peer_share(int active_sessions) {
  return {std::min(200, 800 / active_sessions), std::min(30, 100 / active_sessions)};
}

// Each session owns one torrent. Only its networking lifetime counts here.
class BtPeerSession {
  using Session = libtorrent::session;
  using Settings = libtorrent::settings_pack;
  inline static std::mutex mutex_;
  inline static std::list<Session> sessions_;
  std::list<Session>::iterator session_;

  static void set_share(Settings& settings, int count) {
    auto const share = bt_peer_share(count);
    settings.set_int(Settings::connections_limit, share.connections);
    settings.set_int(Settings::connection_speed, share.attempts_per_second);
  }

  static void resize_existing(int count, bool wait_for_reductions) {
    Settings share;
    set_share(share, count);
    for (auto& session : sessions_) session.apply_settings(share);
    // apply_settings is posted to each network thread. Wait for it before
    // admitting the next torrent, so old and new shares cannot be spent together.
    if (wait_for_reductions)
      for (auto& session : sessions_) (void)session.get_settings();
  }

public:
  explicit BtPeerSession(libtorrent::session_params params) {
    std::lock_guard<std::mutex> lock(mutex_);
    int const count = static_cast<int>(sessions_.size()) + 1;
    set_share(params.settings, count);
    // An incoming replacement allowance per session would grow with task count.
    params.settings.set_int(Settings::connections_slack, 0);
    // libtorrent copies the initial boost into each torrent. Use the regular
    // scheduler so an old startup allowance cannot survive a reduction.
    params.settings.set_int(Settings::torrent_connect_boost, 0);
    try {
      if (!sessions_.empty()) resize_existing(count, true);
      session_ = sessions_.emplace(sessions_.end(), std::move(params));
    } catch (...) {
      if (!sessions_.empty()) resize_existing(count - 1, false);
      throw;
    }
  }

  BtPeerSession(BtPeerSession const&) = delete;
  BtPeerSession& operator=(BtPeerSession const&) = delete;
  Session& get() const { return *session_; }

  ~BtPeerSession() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Erase also joins the old network thread before its share is lent out.
    sessions_.erase(session_);
    if (!sessions_.empty()) resize_existing(static_cast<int>(sessions_.size()), false);
  }
};

} // namespace core
