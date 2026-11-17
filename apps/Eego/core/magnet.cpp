#include "magnet.hpp"
#include "output_layout.hpp"
#include "bt_peer_budget.hpp"
#include "bt_disk.hpp"
#include "bt_files.hpp"
#include "checkpoint.hpp"
#include <libtorrent/libtorrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/disabled_disk_io.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <fstream>
#include <future>
#include <boost/property_tree/json_parser.hpp>
#include <libtorrent/aux_/ip_helpers.hpp>
#include <iomanip>
#include <sstream>
#include <iostream>
namespace core {
namespace lt=libtorrent;namespace fs=std::filesystem;
namespace {
std::string hex(lt::sha256_hash const& hash) {std::string s;for(unsigned char c:hash){s+="0123456789abcdef"[c>>4];s+="0123456789abcdef"[c&15];}return s;}
std::string disk_hash(fs::path const&p,int64_t size) {
  if(fs::file_size(p)!=uint64_t(size))throw std::runtime_error("BT file length changed");
  lt::hasher256 h;for(int64_t at=0;at<size;) {auto b=FileStore::read(p,at,size_t(std::min<int64_t>(1048576,size-at)));h.update(b);at+=b.size();}return hex(h.final());
}
lt::settings_pack settings(HttpControl const&c) {
  lt::settings_pack s;s.set_str(lt::settings_pack::listen_interfaces,"0.0.0.0:0,[::]:0");
  if(auto iface=std::getenv("HBT_BT_LISTEN"))s.set_str(lt::settings_pack::listen_interfaces,iface);
  std::string routers = "dht.libtorrent.org:25401,router.bittorrent.com:6881,dht.transmissionbt.com:6881";
  if(!c.bt_bootstrap_nodes.empty()) {
    if(c.bt_bootstrap_nodes.size()>2048||c.bt_bootstrap_nodes.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:[],")!=std::string::npos)throw std::runtime_error("invalid DHT bootstrap configuration");
    routers=c.bt_bootstrap_nodes;
  }
  s.set_str(lt::settings_pack::dht_bootstrap_nodes,routers);
  s.set_bool(lt::settings_pack::enable_dht,true);s.set_bool(lt::settings_pack::enable_lsd,std::getenv("HBT_BT_DHT_ONLY")==nullptr);
  s.set_bool(lt::settings_pack::enable_upnp,false);s.set_bool(lt::settings_pack::enable_natpmp,false);
  s.set_bool(lt::settings_pack::allow_multiple_connections_per_ip,true);
  s.set_int(lt::settings_pack::alert_mask,lt::alert_category::error|lt::alert_category::storage|lt::alert_category::status|lt::alert_category::dht|lt::alert_category::tracker);
  if(std::getenv("HBT_DHT_DEBUG"))s.set_int(lt::settings_pack::alert_mask,lt::alert_category::all);
  return s;
}
// Optional app-wide hints are independent of task-owned data and its inventory.
bool automatic(HttpControl const &c) {
  return c.bt_bootstrap_nodes.empty();
}
bool public_node(lt::udp::endpoint const &node) {
  return node.port()!=0 && lt::aux::is_global(node.address()) && !node.address().is_loopback();
}
void bootstrap_trace(std::string const& message){
  if(std::getenv("HBT_BOOTSTRAP_TRACE"))std::clog<<message<<std::endl;
}
std::mutex shared_cache_mutex;
void load_shared(lt::dht::dht_state &state,HttpControl const &c) {
  std::lock_guard<std::mutex> lock(shared_cache_mutex);
  if(!automatic(c)||c.bt_node_cache.empty())return;
  try {
    auto path=fs::u8path(c.bt_node_cache);
    reject_bt_reparse(path);
    if(!fs::exists(path)||fs::file_size(path)>16384||
       fs::file_time_type::clock::now()-fs::last_write_time(path)>std::chrono::hours(24*30))return;
    std::ifstream in(path);std::string ip;unsigned port;int loaded=0;
    for(int i=0;i<128 && in>>ip>>port;++i){
      lt::error_code ec;auto address=lt::make_address(ip,ec);
      if(ec||port==0||port>65535)continue;
      lt::udp::endpoint endpoint(address,static_cast<unsigned short>(port));
      if(!public_node(endpoint))continue;
      auto &nodes=address.is_v4()?state.nodes:state.nodes6;
      if(std::find(nodes.begin(),nodes.end(),endpoint)==nodes.end()){nodes.push_back(endpoint);++loaded;}
    }
    bootstrap_trace("loaded shared endpoints: "+std::to_string(loaded));
  }catch(std::exception const&){/* A cache miss must not fail the task. */}
}
void save_shared(lt::dht::dht_state const &state,HttpControl const &c) {
  std::lock_guard<std::mutex> lock(shared_cache_mutex);
  if(!automatic(c)||c.bt_node_cache.empty())return;
  try {
    std::ostringstream out;int count=0;
    for(auto const *nodes:{&state.nodes,&state.nodes6})for(auto const &node:*nodes){
      if(count>=128||!public_node(node))continue;
      out<<node.address().to_string()<<' '<<node.port()<<'\n';++count;
    }
    if(!count)return;
    auto path=fs::u8path(c.bt_node_cache);auto temp=path;temp+=L".tmp";
    reject_bt_reparse(path);reject_bt_reparse(temp);
    // The userdata instance lock excludes other hosts; the mutex excludes workers.
    std::ofstream file(temp,std::ios::binary|std::ios::trunc);file<<out.str();file.close();
    if(file)MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
  }catch(std::exception const&){}
}
struct DynamicBootstrap {
  using Clock=std::chrono::steady_clock;
  using Nodes=std::vector<std::pair<std::string,int>>;
  Clock::time_point next=Clock::now()+std::chrono::seconds(5);
  unsigned attempt=0;
  std::future<Nodes> pending;
  void poll(lt::session &session,HttpControl const &c){
    if(!automatic(c))return;
    if(pending.valid()){
      if(pending.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
      auto nodes=pending.get();
      for(auto const &node:nodes)session.add_dht_node(node);
    }
    if(Clock::now()<next || c.bt_peers>0 || c.bt_dht_nodes>0)return;
    constexpr char const *hosts[]={"dht.libtorrent.org","dht.transmissionbt.com","router.bittorrent.com"};
    unsigned index=attempt%3;
    bool alternate=(attempt/3)%2!=0;
    std::string host=hosts[index];int port=index==0?25401:6881;
    ++attempt;next=Clock::now()+std::chrono::seconds(attempt<=6?6:30);
    pending=std::async(std::launch::async,[host,port,alternate]{
      Nodes nodes;
      try{
        std::istringstream input(resolve_bootstrap_json(host,alternate));
        boost::property_tree::ptree reply;boost::property_tree::read_json(input,reply);
        if(reply.get<int>("Status")!=0)return nodes;
        for(auto const &answer:reply.get_child("Answer")){
          if(answer.second.get<int>("type")!=1)continue;
          lt::error_code ec;auto addr=lt::make_address(answer.second.get<std::string>("data"),ec);
          if(!ec&&addr.is_v4()&&public_node({addr,static_cast<unsigned short>(port)}))
            nodes.emplace_back(addr.to_string(),port);
          if(nodes.size()==8)break;
        }
      }catch(std::exception const&){}
      bootstrap_trace("HTTPS DNS "+host+" returned "+std::to_string(nodes.size())+" addresses");
      return nodes;
    });
  }
};
lt::session_params discovery_params(fs::path const&root,HttpControl const&c) {
  lt::session_params sp(settings(c));auto cache=root/L"dht.record";reject_bt_reparse(cache);
  if(fs::exists(cache))try{auto bytes=load_record(cache);auto saved=lt::read_session_params({bytes.data(),static_cast<std::ptrdiff_t>(bytes.size())},lt::session_handle::save_dht_state);sp.dht_state=std::move(saved.dht_state); }catch(std::exception const&){bootstrap_trace("unreadable task DHT cache");}
  load_shared(sp.dht_state,c);
  return sp;
}
void save_discovery(lt::session const&session,fs::path const&root,HttpControl const &c) {
  auto params=session.session_state(lt::session_handle::save_dht_state);
  save_shared(params.dht_state,c);
  auto bytes=lt::write_session_params_buf(params,lt::session_handle::save_dht_state);
  save_record(root/L"dht.record",{bytes.begin(),bytes.end()});
}
void discovery_alerts(lt::session&session,HttpControl&c,std::map<std::string,int>&counts) {
  std::vector<lt::alert*> alerts;session.pop_alerts(&alerts);
  for(auto a:alerts) {
    if(std::getenv("HBT_DHT_DEBUG"))std::cout<<a->message()<<std::endl;
    if(auto d=lt::alert_cast<lt::dht_stats_alert>(a)){int nodes=0;for(auto const&b:d->routing_table)nodes+=b.num_nodes;counts[d->local_endpoint.address().to_string()]=nodes;int sum=0;for(auto const&v:counts)sum+=v.second;c.bt_dht_nodes=sum;}
    if(lt::alert_cast<lt::tracker_error_alert>(a))++c.bt_tracker_errors;
  }
}
void connect(lt::torrent_handle const&h,lt::add_torrent_params const&p) {for(auto const &peer:p.peers)h.connect_peer(peer);}
std::shared_ptr<lt::torrent_info> metadata_info(fs::path const&root,fs::path const&destination) {
  reject_bt_reparse(root/L"metadata.record");reject_bt_reparse(root/L"magnet.owner");
  auto bytes=load_record(root/L"metadata.record");
  auto ti=std::make_shared<lt::torrent_info>(lt::span<char const>(bytes.data(),bytes.size()),lt::from_span);
  validate_bt_files(ti->files());std::ostringstream owner;owner<<ti->info_hashes().v1<<'\n'<<destination.u8string();
  if(load_record(root/L"magnet.owner")!=owner.str())throw std::runtime_error("Magnet ownership mismatch");
  return ti;
}
std::vector<bool> selection(fs::path const&root,lt::torrent_info const&ti) {
  std::vector<bool> out(ti.num_files(),true);auto path=root/L"selection.record";reject_bt_reparse(path);
  if(!fs::exists(path))return out;
  std::istringstream in(load_record(path));std::string magic,hash;size_t n;
  std::ostringstream expected;expected<<ti.info_hashes().v1;
  if(!(in>>magic>>hash>>n)||magic!="BTSELECT1"||hash!=expected.str()||n!=out.size())throw std::runtime_error("invalid file selection");
  bool any=false;
  for(size_t i=0;i<n;++i){int value;if(!(in>>value)||(value!=0&&value!=1))throw std::runtime_error("invalid file selection");out[i]=value;any|=value&&!ti.files().pad_file_at(lt::file_index_t(int(i)));}
  in>>std::ws;if(!in.eof()||!any)throw std::runtime_error("empty or invalid file selection");return out;
}
}
std::vector<MagnetFile> magnet_files(std::string const&directory,fs::path state) {
  auto destination=fs::absolute(fs::u8path(directory)).lexically_normal();
  auto root=state.empty()?destination:state;
  if(root!=destination && fs::exists(root/L"files.record")){
    std::istringstream in(load_record(root/L"files.record"));
    std::string magic;size_t count;
    if(!(in>>magic>>count)||magic!="BTFILES1"||count>100000)throw std::runtime_error("invalid completion manifest");
    std::vector<MagnetFile> out;
    for(size_t i=0;i<count;++i){
      MagnetFile file{};std::string hash;
      if(!(in>>std::quoted(file.path)>>file.size>>hash))throw std::runtime_error("invalid completed file");
      file.index=int(i);file.selected=true;out.push_back(std::move(file));
    }
    return out;
  }
  auto ti=metadata_info(root,destination);auto chosen=selection(root,*ti);
  std::vector<MagnetFile> out;for(auto i:ti->files().file_range())if(!ti->files().pad_file_at(i))out.push_back({int(i),ti->files().file_path(i),uint64_t(ti->files().file_size(i)),chosen[int(i)]});return out;
}
void select_magnet_files(std::string const&directory,std::vector<int> const&indexes,fs::path state) {
  auto destination=fs::absolute(fs::u8path(directory)).lexically_normal();
  auto root=state.empty()?destination:state;reject_bt_reparse(root/L"magnet.lock");TaskLock lock(root/L"magnet.lock");
  auto ti=metadata_info(root,destination);reject_bt_reparse(root/L"selection.record");
  if(fs::exists(destination/L"files")||!output_name(state).leaf.empty())throw std::runtime_error("已开始下载的任务不能更改文件选择；请新建任务，原文件保留");
  if(indexes.empty())throw std::runtime_error("请至少选择一个文件");
  std::vector<bool> chosen(ti->num_files(),false);
  for(int i:indexes){if(i<0||i>=ti->num_files()||ti->files().pad_file_at(lt::file_index_t(i)))throw std::runtime_error("invalid file index");chosen[i]=true;}
  std::ostringstream out;out<<"BTSELECT1 "<<ti->info_hashes().v1<<' '<<chosen.size()<<'\n';for(bool b:chosen)out<<int(b)<<' ';save_record(root/L"selection.record",out.str());
}
std::string verify_magnet_output(std::string const&directory,fs::path state,
    std::function<bool(fs::path const&)> const& previously_verified) {
  auto destination=fs::u8path(directory);auto root=state.empty()?destination:state;reject_bt_reparse(root);auto payload=load_record(root/L"files.record");
  std::istringstream in(payload);std::string magic;size_t count;
  if(!(in>>magic>>count)||magic!="BTFILES1"||count>100000)throw std::runtime_error("invalid BT file manifest");
  lt::file_storage files;std::vector<std::string> hashes;
  for(size_t n=0;n<count;++n){std::string path,hash;int64_t size;if(!(in>>std::quoted(path)>>size>>hash)||size<0)throw std::runtime_error("invalid BT manifest entry");files.add_file(path,size);hashes.push_back(hash);}
  in>>std::ws;if(!in.eof())throw std::runtime_error("trailing BT manifest data");validate_bt_files(files);
  auto layout=output_name(state);auto leaf=layout.leaf;
  for(auto i:files.file_range()){
    auto path=leaf.empty()?bt_path(destination/L"files",files,i):destination.parent_path()/(layout.single?leaf:named_relative(fs::u8path(files.file_path(i)),leaf));
    reject_bt_reparse(path);
    if(previously_verified && fs::file_size(path)==uint64_t(files.file_size(i)) && previously_verified(path))continue;
    if(disk_hash(path,files.file_size(i))!=hashes[int(i)])throw std::runtime_error("BT file SHA-256 changed");
  }
  return sha256(payload);
}
int magnet_download(std::string const&uri,std::string const&directory,HttpControl &c,bool choose_files,fs::path state) {
  auto parsed=lt::parse_magnet_uri(uri);
  if(std::getenv("HBT_BT_DHT_ONLY")) {
    parsed.trackers.clear();parsed.tracker_tiers.clear();parsed.url_seeds.clear();parsed.http_seeds.clear();parsed.peers.clear();
    parsed.flags|=lt::torrent_flags::disable_pex|lt::torrent_flags::disable_lsd;
  }
  if(!parsed.info_hashes.has_v1()||parsed.info_hashes.has_v2())throw std::runtime_error("目前支持 BT v1 Magnet；v2/hybrid 尚未接入");
  auto destination=fs::absolute(fs::u8path(directory)).lexically_normal();
  auto root=state.empty()?destination:state;
  // A stable info-hash identity; tracker additions never authorize another swarm.
  std::ostringstream id;id<<parsed.info_hashes.v1<<'\n'<<destination.u8string();
  reject_bt_reparse(root);fs::create_directories(root);
  for(auto name:{L"magnet.lock",L"magnet.owner",L"metadata.record",L"files.record"})reject_bt_reparse(root/name);
  TaskLock lock(root/L"magnet.lock");
  auto owner=root/L"magnet.owner";
  if(fs::exists(owner)){if(load_record(owner)!=id.str())throw std::runtime_error("Magnet ownership mismatch; files preserved");}
  else {
    for(auto const&e:fs::directory_iterator(root))if(e.path().filename()!=L"magnet.lock")throw std::runtime_error("unowned Magnet directory; refusing overwrite");
    save_record(owner,id.str());
  }
  std::shared_ptr<lt::torrent_info> ti;
  auto metadata=root/L"metadata.record";
  if(fs::exists(metadata)) {auto bytes=load_record(metadata);ti=std::make_shared<lt::torrent_info>(lt::span<char const>(bytes.data(),bytes.size()),lt::from_span);}
  else {
    c.phase=5;
    auto sp=discovery_params(root,c);sp.disk_io_constructor=lt::disabled_disk_io_constructor;
    BtPeerSession network(std::move(sp));
    auto& session=network.get();DynamicBootstrap bootstrap;std::map<std::string,int> counts;auto p=parsed;p.save_path=root.u8string();
    p.flags &= ~(lt::torrent_flags::auto_managed|lt::torrent_flags::paused);
    p.flags |= lt::torrent_flags::upload_mode|lt::torrent_flags::default_dont_download;
    auto h=session.add_torrent(p);auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);auto next=std::chrono::steady_clock::now();
    while(!c.pause) {
      bootstrap.poll(session,c);
      auto st=h.status();c.bt_peers=st.num_peers;c.bt_candidates=st.list_peers;if(st.errc)throw std::runtime_error(st.errc.message());
      if(st.has_metadata){ti=std::make_shared<lt::torrent_info>(*h.torrent_file());break;}
      auto now=std::chrono::steady_clock::now();if(now>=deadline){save_discovery(session,root,c);if(choose_files)return 5;throw std::runtime_error("暂未找到可提供元数据的节点；不代表资源不存在，可稍后重试");}
      if(now>=next){connect(h,parsed);session.post_dht_stats();h.force_dht_announce();save_discovery(session,root,c);std::cout<<"BT discovery: dht="<<c.bt_dht_nodes<<" connected="<<c.bt_peers<<" known_peers="<<c.bt_candidates<<" tracker_errors="<<c.bt_tracker_errors<<std::endl;next=now+std::chrono::seconds(30);}
      discovery_alerts(session,c,counts);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    save_discovery(session,root,c);h.pause();
    if(c.pause){c.phase=4;return 3;}
    lt::add_torrent_params met;met.ti=ti;auto b=lt::write_torrent_file_buf(met,{});save_record(metadata,{b.begin(),b.end()});
  }
  if(ti->info_hashes()!=parsed.info_hashes)throw std::runtime_error("Magnet metadata hash mismatch");
  validate_bt_files(ti->files());
  if(choose_files&&!fs::exists(root/L"selection.record")&&!fs::exists(destination/L"files")){c.total=ti->total_size();return 4;}
  auto chosen=selection(root,*ti);
  uint64_t wanted=0;for(auto i:ti->files().file_range())if(chosen[int(i)]&&!ti->files().pad_file_at(i))wanted+=ti->files().file_size(i);
  c.total=wanted;c.ranged=true;c.phase=0;
  auto layout=output_name(state);auto leaf=layout.leaf;
  auto files=leaf.empty()?destination/L"files":destination.parent_path();
  reject_bt_reparse(files);fs::create_directories(files);
  if(!leaf.empty())for(auto i:ti->orig_files().file_range()){
    if(layout.single && (!chosen[int(i)]||ti->orig_files().pad_file_at(i)))continue;
    auto name=(layout.single?leaf:named_relative(fs::u8path(ti->orig_files().file_path(i)),leaf)).u8string();
    ti->rename_file(i,name);
  }
  auto store=std::make_shared<FileStore>();auto metrics=std::make_shared<DiskMetrics>();
  bool finished=false;
  {
    auto sp=discovery_params(root,c);sp.disk_io_constructor=disk_factory(store,metrics,0,root/L"boundary-cache");
    BtPeerSession network(std::move(sp));
    auto& session=network.get();DynamicBootstrap bootstrap;
    auto p=parsed;p.ti=ti;p.save_path=files.u8string();
    p.flags &= ~(lt::torrent_flags::auto_managed|lt::torrent_flags::paused|lt::torrent_flags::seed_mode|lt::torrent_flags::no_verify_files|lt::torrent_flags::default_dont_download);
    p.file_priorities.assign(ti->num_files(),lt::default_priority);
    for(auto i:ti->files().file_range())if(!chosen[int(i)])p.file_priorities[int(i)]=lt::dont_download;
    auto h=session.add_torrent(p);auto next=std::chrono::steady_clock::now();bool pausing=false;auto pause_deadline=next;
    c.bt_dht_nodes=0;c.bt_peers=0;
    while(true) {
      bootstrap.poll(session,c);
      auto st=h.status();c.bt_peers=st.num_peers;c.bt_candidates=st.list_peers;if(st.errc)throw std::runtime_error(st.errc.message());
      c.received=c.verified=std::min<uint64_t>(wanted,st.total_wanted_done);
      c.phase=(st.state==lt::torrent_status::checking_files||st.state==lt::torrent_status::checking_resume_data)?2:(st.num_peers==0?6:1);
      {
        std::lock_guard<std::mutex> guard(c.file_progress_mutex);
        c.file_progress_valid=st.state!=lt::torrent_status::checking_files&&st.state!=lt::torrent_status::checking_resume_data;
        if(c.file_progress_valid)c.file_progress=h.file_progress(lt::torrent_handle::piece_granularity);
      }
      if(st.is_finished&&st.state!=lt::torrent_status::checking_files&&st.state!=lt::torrent_status::checking_resume_data){finished=true;h.pause();break;}
      auto now=std::chrono::steady_clock::now();
      if(c.pause&&!pausing){h.pause();pausing=true;pause_deadline=now+std::chrono::seconds(30);}
      if(!pausing&&now>=next){connect(h,parsed);next=now+std::chrono::seconds(1);}
      std::vector<lt::alert*> alerts;session.pop_alerts(&alerts);bool paused=false;
      for(auto a:alerts){if(lt::alert_cast<lt::torrent_paused_alert>(a)&&pausing)paused=true;if(auto e=lt::alert_cast<lt::file_error_alert>(a))throw std::runtime_error(e->message());}
      if(paused)break;
      if(pausing&&now>=pause_deadline)throw std::runtime_error("BT pause timed out; files retained for full recheck");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  store->barrier();bt_flush(files,ti->files(),chosen,root/L"boundary-cache");
  if(metrics->wrong_thread||metrics->flush_errors||metrics->completed!=metrics->writes)throw std::runtime_error("BT storage callback/flush invariant failed");
  if(!finished){c.phase=4;return 3;}
  c.phase=2;
  for(auto piece:ti->files().piece_range()) {
    bool needed=false;for(auto const&s:ti->files().map_block(piece,0,ti->files().piece_size(piece)))if(chosen[int(s.file_index)]&&!ti->files().pad_file_at(s.file_index))needed=true;
    if(!needed)continue;
    auto b=bt_read(files,ti->files(),piece,0,ti->files().piece_size(piece),chosen,root/L"boundary-cache");
    if(lt::hasher(b).final()!=ti->hash_for_piece(piece))throw std::runtime_error("BT completion piece recheck failed");
  }
  std::ostringstream out;int count=0;for(auto i:ti->files().file_range())if(!ti->files().pad_file_at(i)&&chosen[int(i)])++count;
  out<<"BTFILES1 "<<count<<'\n';
  for(auto i:ti->files().file_range())if(!ti->files().pad_file_at(i)&&chosen[int(i)])out<<std::quoted(ti->orig_files().file_path(i))<<' '<<ti->files().file_size(i)<<' '<<disk_hash(bt_path(files,ti->files(),i),ti->files().file_size(i))<<'\n';
  auto digest=sha256(out.str());
  if(c.before_publish)c.before_publish(metadata,digest);
  save_record(root/L"files.record",out.str());c.result_sha256=digest;c.received=c.verified=c.total.load();c.phase=3;
  return 0;
}
}
