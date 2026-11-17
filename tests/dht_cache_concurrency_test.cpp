// Include implementation to exercise private optional-cache helpers without a production test API.
#include "../src/magnet.cpp"
#include <iostream>
int main(){try{
 namespace fs=std::filesystem;auto root=fs::current_path()/"test-temp"/("dht-cache-"+std::to_string(GetCurrentProcessId()));fs::create_directories(root);
 core::HttpControl c;c.bt_node_cache=(root/"dht-peers.txt").u8string();
 lt::dht::dht_state a,b;a.nodes.emplace_back(lt::make_address("8.8.8.8"),6881);b.nodes.emplace_back(lt::make_address("1.1.1.1"),6882);
 std::atomic<bool> bad{false};
 std::thread x([&]{for(int i=0;i<100;++i)core::save_shared(a,c);});
 std::thread y([&]{for(int i=0;i<100;++i)core::save_shared(b,c);});
 std::thread z([&]{for(int i=0;i<200;++i){lt::dht::dht_state s;core::load_shared(s,c);if(s.nodes.size()>1)bad=true;if(s.nodes.size()==1&&s.nodes[0]!=a.nodes[0]&&s.nodes[0]!=b.nodes[0])bad=true;}});
 x.join();y.join();z.join();lt::dht::dht_state s;core::load_shared(s,c);
 if(bad||s.nodes.size()!=1||fs::exists(root/"dht-peers.txt.tmp"))throw std::runtime_error("shared cache torn or left temporary");
 {std::ofstream corrupt(root/"dht-peers.txt");corrupt<<"not-an-address invalid";}s={};core::load_shared(s,c);if(!s.nodes.empty())throw std::runtime_error("invalid cache accepted");
 std::cout<<"PASS concurrent shared DHT writers/readers and malformed optional cache (no network)\n";return 0;
}catch(std::exception const&e){std::cerr<<e.what()<<'\n';return 1;}}
