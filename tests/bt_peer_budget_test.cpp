#include "bt_peer_budget.hpp"
#include <array>
#include <future>
#include <iostream>
#include <stdexcept>

namespace lt = libtorrent;
using Settings = lt::settings_pack;
using core::BtPeerSession;

lt::session_params offline_params() {
  Settings settings;
  settings.set_str(Settings::listen_interfaces, "");
  settings.set_str(Settings::dht_bootstrap_nodes, "");
  settings.set_bool(Settings::enable_dht, false);
  settings.set_bool(Settings::enable_lsd, false);
  settings.set_bool(Settings::enable_natpmp, false);
  settings.set_bool(Settings::enable_upnp, false);
  return lt::session_params(settings);
}

void expect(BtPeerSession const& session, int peers, int attempts) {
  auto const settings = session.get().get_settings();
  if (settings.get_int(Settings::connections_limit) != peers ||
      settings.get_int(Settings::connection_speed) != attempts ||
      settings.get_int(Settings::torrent_connect_boost) != 0 ||
      settings.get_int(Settings::connections_slack) != 0)
    throw std::runtime_error("unexpected live session budget");
}

int main() {
  static_assert(core::bt_peer_share(8).connections == 100);
  static_assert(core::bt_peer_share(8).attempts_per_second == 12);
  for (int count = 1; count <= 8; ++count) {
    auto share = core::bt_peer_share(count);
    if (share.connections * count > 800 || share.attempts_per_second * count > 100)
      return 1;
  }
  try {
    BtPeerSession first(offline_params());
    expect(first, 200, 30);
    // Concurrent admissions exercise serialization of reductions.
    std::array<std::future<std::unique_ptr<BtPeerSession>>, 4> starts;
    for (auto& start : starts)
      start = std::async(std::launch::async, [] {
        return std::make_unique<BtPeerSession>(offline_params());
      });
    std::array<std::unique_ptr<BtPeerSession>, 4> others;
    for (size_t i = 0; i < others.size(); ++i) others[i] = starts[i].get();
    expect(first, 160, 20);
    for (auto const& session : others) expect(*session, 160, 20);
    others[1].reset();
    expect(first, 200, 25);
    for (auto const& session : others) if (session) expect(*session, 200, 25);
    std::cout << "PASS shared BT budget, concurrent admission and release\n";
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
