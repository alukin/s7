// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in research only. NodeRuntime owns snapshots and the boundary Delay;
// Region owns and dispatches unchanged compiled core/net elements. No core API
// change. Sources/scope: CROSS_NODE_CAUSALITY_RESEARCH.md L1/L3/L7/L10/T1/T2.
#include <s7/core.hpp>
#include <s7/net.hpp>
#include <boost/json.hpp>
#include <openssl/sha.h>
#include <openssl/pem.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <thread>
#include <sys/resource.h>
#include <sys/utsname.h>

namespace j = boost::json;
using namespace s7;
using Clock = std::chrono::steady_clock;
using Ns = std::chrono::nanoseconds;
const std::array<std::string,3> nodes{"aaaaaaaa-0000-4000-8000-000000000001",
    "bbbbbbbb-0000-4000-8000-000000000002", "cccccccc-0000-4000-8000-000000000003"};
std::string read_file(const std::string& path) {
    std::ifstream f(path,std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + path);
    return std::string(std::istreambuf_iterator<char>{f},{});
}
std::string hex(const unsigned char* bytes,std::size_t count) {
    std::ostringstream out;
    for (std::size_t i=0;i<count;++i) out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(bytes[i]);
    return out.str();
}
std::string hash_file(const std::string& path) {
    const auto bytes=read_file(path); unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size(),digest);
    return hex(digest,sizeof(digest));
}
std::uint64_t cpu_ns(clockid_t which) {
    timespec t{};
    if (clock_gettime(which,&t)!=0) throw std::runtime_error("CPU clock unavailable");
    return std::uint64_t(t.tv_sec)*1000000000ULL+std::uint64_t(t.tv_nsec);
}
std::uint64_t ns(Clock::duration d) { return std::uint64_t(std::chrono::duration_cast<Ns>(d).count()); }
std::string str(const j::value& v) { return std::string(v.as_string()); }
j::object distribution(std::vector<std::uint64_t> v) {
    if (v.empty()) return {};
    std::ranges::sort(v);
    auto q=[&](double p) { return v[std::size_t(std::ceil(p*double(v.size())))-1]; };
    const auto mid=v.size()/2;
    return {{"n",v.size()},{"min",v.front()},{"median",v.size()%2?double(v[mid]):(double(v[mid-1])+double(v[mid]))/2},
        {"p95",q(.95)},{"p99",q(.99)},{"max",v.back()}};
}

// Preopening is identical for every network policy. Elements claim each known
// handle once; no stream is opened twice, and all auth/ACK rules stay backend-owned.
class PreparedEndpoint final : public transport::IRuntimeNetEndpoint {
    transport::RuntimeNetEndpointPtr backend_;
    stream::StreamConfig tx_config_,rx_config_;
    transport::StreamHandle tx_,rx_;
    bool tx_claimed_{},rx_claimed_{};
public:
    PreparedEndpoint(transport::RuntimeNetEndpointPtr b,stream::StreamConfig tx,stream::StreamConfig rx)
        : backend_(std::move(b)),tx_config_(std::move(tx)),rx_config_(std::move(rx)) {
        tx_=backend_->open_producer(tx_config_); rx_=backend_->open_consumer(rx_config_);
    }
    std::string backend_name() const override { return backend_->backend_name(); }
    transport::StreamHandle open_producer(const stream::StreamConfig& c) override {
        if (tx_claimed_ || c.stream_id!=tx_config_.stream_id || c.type!=tx_config_.type) throw std::runtime_error("invalid tx claim");
        tx_claimed_=true; return tx_;
    }
    transport::StreamHandle open_consumer(const stream::StreamConfig& c) override {
        if (rx_claimed_ || c.stream_id!=rx_config_.stream_id || c.type!=rx_config_.type) throw std::runtime_error("invalid rx claim");
        rx_claimed_=true; return rx_;
    }
    void close_stream(transport::StreamHandle h) override { backend_->close_stream(h); }
    transport::PublishResult publish(transport::StreamHandle h,const void* p,std::size_t n,std::span<const wire::TimestampRecord> t={}) override { return backend_->publish(h,p,n,t); }
    transport::ReceiveResult receive(transport::StreamHandle h,void* p,std::size_t n) override { return backend_->receive(h,p,n); }
    std::vector<transport::StreamStatus> stream_statuses() const override { return backend_->stream_statuses(); }
    bool recover_stream(const wire::StreamId& id) override { return backend_->recover_stream(id); }
    void note_on_all_streams(stream::FaultCode c,const std::string& s) override { backend_->note_on_all_streams(c,s); }
    void set_clock_usable(bool b) override { backend_->set_clock_usable(b); }
    std::vector<std::string> take_transport_errors() override { return backend_->take_transport_errors(); }
    transport::TransportStats stats() const override { return backend_->stats(); }
};

class Seed final : public Element {
public:
    double value{};
    Seed() : Element("ResearchBoundaryInput") { register_output(0,"out",SignalType::F64,&value); }
    void step() override {}
};

struct Built {
    ExecutableGraph graph;
    std::vector<std::string> names;
    std::vector<std::vector<std::size_t>> parents;
    std::vector<std::size_t> static_order;
    std::string output;
};

Built build_inner(const j::object& plan,const std::string& role,bool network,
                  const stream::StreamConfig& tx,const stream::StreamConfig& rx,bool flat_memory=false) {
    Schema schema("research_inner"); schema.set_rate(Rate::from_hz(1000));
    std::map<std::string,std::string> placement;
    for (const auto& v:plan.at("actors").as_array()) placement[str(v.at("name"))]=str(v.at("node"));
    std::vector<std::string> biases;
    for (const auto& v:plan.at("actors").as_array()) {
        if (str(v.at("node"))!=role) continue;
        const auto name=str(v.at("name")),kind=str(v.at("kind"));
        if (kind=="delay") {
            if (flat_memory) schema.add_element(name,[]{return std::make_unique<Delay>(0.0);});
            else schema.add_element(name,[]{return std::make_unique<Seed>();});
        }
        else if (kind=="constant") {
            const auto value=v.at("parameter").to_number<double>();
            schema.add_element(name,[value]{return std::make_unique<Constant>(value);});
        } else if (kind=="identity") schema.add_element(name,[]{return std::make_unique<Gain>(1.0);});
        else if (kind=="add_one") {
            schema.add_element(name,[]{return std::make_unique<Adder>();});
            const auto bias="bias_"+name; biases.push_back(bias);
            schema.add_element(bias,[]{return std::make_unique<Constant>(1.0);});
            schema.connect(bias,0,name,1);
        } else throw std::runtime_error("unsupported actor kind " + kind);
        if (kind!="delay" || flat_memory) {
            for (const auto& input:v.at("inputs").as_array()) {
                const auto source=str(input);
                schema.connect(placement.at(source)==role?source:"receive:"+source+"@"+role,0,name,0);
            }
        }
    }
    for (const auto& [key,value]:plan.at("tasks").as_object()) {
        const auto name=std::string(key),kind=str(value.at("kind"));
        if (str(value.at("node"))!=role) continue;
        if (kind=="send") {
            if (!network) throw std::runtime_error("network task in CPU graph");
            schema.add_element(name,[tx]{return std::make_unique<NetSink>(tx);});
            schema.connect(str(value.at("inputs").as_array()[0]),0,name,0);
        } else if (kind=="receive") {
            if (!network) throw std::runtime_error("network task in CPU graph");
            schema.add_element(name,[rx]{return std::make_unique<NetSource>(rx);});
        }
    }
    Built built; built.graph=Compiler::compile(schema);
    built.names=built.graph.causal_order;
    std::map<std::string,std::size_t> index;
    for (std::size_t n=0;n<built.names.size();++n) index[built.names[n]]=n;
    built.parents.resize(index.size());
    for (const auto& edge:schema.connections())
        if (index.contains(edge.to_element) && index.contains(edge.from_element))
            built.parents[index.at(edge.to_element)].push_back(index.at(edge.from_element));
    std::vector<std::string> static_names=biases;
    for (const auto& v:plan.at("projected").at(role).as_array())
        if (index.contains(str(v))) static_names.push_back(str(v));
    if (static_names.size()!=index.size() || std::set<std::string>(static_names.begin(),static_names.end()).size()!=index.size())
        throw std::runtime_error("plan membership mismatch");
    std::vector<bool> done(index.size());
    for (const auto& name:static_names) {
        const auto n=index.at(name);
        for (const auto parent:built.parents[n]) if (!done[parent]) throw std::runtime_error("plan violates local edge");
        done[n]=true; built.static_order.push_back(n);
    }
    if (network) {
        for (const auto& name:built.names) if (name.starts_with("receive:")) built.output=name;
    } else built.output=static_names.back();
    return built;
}

class Region final : public Element {
    Built built_;
    std::string method_;
    transport::RuntimeNetEndpointPtr endpoint_;
    Seed* seed_{};
    std::vector<Element*> elements_;
    std::vector<std::vector<std::size_t>> followers_;
    std::vector<std::size_t> degree_,ready_,blocked_,scan_;
    std::vector<bool> done_;
    std::vector<int> io_; // 0 computation, 1 send, 2 receive
    std::chrono::microseconds poll_;
    bool fine_timing_{};
public:
    std::uint64_t io_probes{},dependency_updates{},dispatches{},waits{},wait_ns{},selection_ns{},scans{};
    bool fail_once{};
    Region(Built built,std::string method,transport::RuntimeNetEndpointPtr endpoint,std::chrono::microseconds poll,bool fine_timing=false)
        : Element("ResearchCompiledRegion",true),built_(std::move(built)),method_(std::move(method)),
          endpoint_(std::move(endpoint)),poll_(poll),fine_timing_(fine_timing) {
        const auto count=built_.names.size();
        followers_.resize(count); degree_.resize(count); done_.resize(count); io_.resize(count);
        ready_.reserve(count); blocked_.reserve(count); scan_.reserve(count);
        for (std::size_t n=0;n<count;++n) {
            auto* element=built_.graph.elements.at(built_.names[n]).get(); elements_.push_back(element);
            if (auto* seed=dynamic_cast<Seed*>(element)) seed_=seed;
            if (element->type_name()=="NetSink") io_[n]=1;
            if (element->type_name()=="NetSource") io_[n]=2;
            for (const auto p:built_.parents[n]) followers_[p].push_back(n);
        }
        if (seed_) register_input(0,"state",SignalType::F64,nullptr);
        // Register every owned inner output so NodeRuntime snapshots/restores
        // all compiled element outputs, not just the region's feedback output.
        const auto& primary=built_.graph.elements.at(built_.output)->output_ports()[0];
        register_output(0,"result",primary.type,primary.value_ptr);
        std::size_t port=1;
        for (auto* e:elements_) for (const auto& p:e->output_ports())
            if (p.value_ptr!=primary.value_ptr) register_output(port++,"inner",p.type,p.value_ptr);
    }
    void reset_metrics() { io_probes=dependency_updates=dispatches=waits=wait_ns=selection_ns=scans=0; }
    void on_step_failure() noexcept override { for (auto* e:elements_) e->on_step_failure(); }
    void step() override {
        if (seed_) seed_->value=read_input<double>(0);
        if (method_=="canonical" || method_=="egress") {
            for (const auto n:built_.static_order) { elements_[n]->step(); ++dispatches; }
        } else {
            std::fill(done_.begin(),done_.end(),false); ready_.clear(); blocked_.clear(); scan_.clear();
            for (std::size_t n=0;n<elements_.size();++n) {
                degree_[n]=built_.parents[n].size();
                if (degree_[n]==0) ready_.push_back(n);
                scan_.push_back(elements_.size()-1-n);
            }
            std::size_t count=0,head=0;
            const auto until=Clock::now()+std::chrono::seconds(4);
            while (count<elements_.size()) {
                const auto selection=fine_timing_?Clock::now():Clock::time_point{};
                // Existing endpoint has no wakeup subscription/readiness API.
                // Poll only stream state (at most two I/O tasks here), never
                // rescan compute actors in queue mode. Poll cost is measured.
                std::vector<transport::StreamStatus> statuses;
                auto eligible=[&](std::size_t n) {
                    if (!io_[n]) return true;
                    ++io_probes;
                    if (statuses.empty() && endpoint_) statuses=endpoint_->stream_statuses();
                    for (const auto& s:statuses) {
                        if ((io_[n]==1)!=(s.role==transport::StreamRole::Producer)) continue;
                        if (s.fault!=stream::FaultCode::None) throw std::runtime_error(stream::to_string(s.fault));
                        return io_[n]==1?s.state!="blocked":s.buffered_elements>0;
                    }
                    return false;
                };
                std::size_t chosen=elements_.size();
                if (method_=="scan") {
                    for (const auto n:scan_) {
                        if (done_[n]) continue;
                        ++scans;
                        if (std::ranges::all_of(built_.parents[n],[&](auto p){return done_[p];}) && eligible(n)) { chosen=n; break; }
                    }
                } else if (method_=="queue") {
                    while (head<ready_.size()) {
                        const auto n=ready_[head++];
                        if (eligible(n)) { chosen=n; break; }
                        blocked_.push_back(n);
                    }
                } else throw std::runtime_error("unknown dispatcher");
                if (fine_timing_) selection_ns+=ns(Clock::now()-selection);
                if (chosen==elements_.size()) {
                    if (Clock::now()>until) throw std::runtime_error("readiness timeout");
                    const auto wait=Clock::now(); std::this_thread::sleep_for(poll_);
                    wait_ns+=ns(Clock::now()-wait); ++waits;
                    if (method_=="queue") {
                        if (head!=ready_.size()) throw std::logic_error("queue head");
                        ready_.clear(); head=0;
                        for (const auto n:blocked_) ready_.push_back(n);
                        blocked_.clear();
                        if (ready_.empty()) throw std::runtime_error("dependency stall without blocked I/O");
                    }
                    continue;
                }
                elements_[chosen]->step(); done_[chosen]=true; ++count; ++dispatches;
                if (method_=="queue") for (const auto child:followers_[chosen]) {
                    ++dependency_updates;
                    if (--degree_[child]==0) ready_.push_back(child);
                }
            }
        }
        if (fail_once) { fail_once=false; throw std::runtime_error("injected region failure"); }
    }
};

int main(int argc,char** argv) try {
    std::map<std::string,std::string> opts;
    for (int i=1;i<argc;++i) {
        const std::string key=argv[i];
        if (key=="--cpu" || key=="--self-test" || key=="--wait-start" || key=="--flat" || key=="--selection-timing") opts[key]="1";
        else { if (++i==argc) throw std::invalid_argument("missing option"); opts[key]=argv[i]; }
    }
    const bool cpu=opts.contains("--cpu"),self_test=opts.contains("--self-test"),network=!cpu&&!self_test;
    auto get=[&](const std::string& key,const std::string& fallback) { return opts.contains(key)?opts.at(key):fallback; };
    const auto role=get("--role","A"),method=get("--approach","egress"),topology=get("--topology","cpu");
    const auto index=std::string("ABC").find(role);
    if (role.size()!=1 || index>=3) throw std::invalid_argument("role");
    const auto steps=std::stoul(get("--steps","128")),warmup=std::stoul(get("--warmup","32"));
    const auto window=std::stoul(get("--window","8")),packet=std::stoul(get("--packet-frames","1"));
    const auto poll=std::stoul(get("--poll-us","1000"));
    if (!steps || steps>100000 || warmup>10000 || window==0 || packet==0 || poll==0 || poll>10000)
        throw std::invalid_argument("resource limits");
    const auto plan=j::parse(read_file(opts.at("--plan"))).as_object();
    if (opts.contains("--plan-sha256") && get("--plan-sha256","")!=str(plan.at("sha256"))) throw std::runtime_error("plan identity mismatch");
    auto stream_config=[&](std::size_t n) {
        auto c=net_stream_config("00000000-0000-4000-8000-00000000000"+std::to_string(n+1));
        c.type=SignalType::F64; c.producer_node=nodes[n]; c.consumer_nodes={nodes[(n+1)%3]};
        c.max_elements_per_packet=static_cast<std::uint32_t>(packet); c.send_log_max_packets=static_cast<std::uint32_t>(window);
        c.max_block=c.max_wait=std::chrono::milliseconds(4000);
        c.max_batch_latency=std::chrono::milliseconds(std::stoul(get("--batch-ms","5")));
        c.sample_rate=stream::SampleRate{1000,1}; return c;
    };
    const auto tx=stream_config(index),rx=stream_config((index+2)%3);
    std::shared_ptr<transport::BeastRuntimeNetEndpoint> backend;
    transport::RuntimeNetEndpointPtr endpoint;
    transport::TlsConfig tls;
    if (network) {
        const auto dir=opts.at("--credentials");
        tls={true,dir+"/node.cert.pem",dir+"/node.key.pem",dir+"/ca.cert.pem",true};
        backend=std::make_shared<transport::BeastRuntimeNetEndpoint>();
        backend->set_node_uuid(nodes[index]); backend->set_clock_usable(false);
        static_cast<void>(backend->configure_server(static_cast<std::uint16_t>(std::stoul(opts.at("--listen"))),tls,"127.0.0.1",transport::ListenerChannels::Data));
        endpoint=std::make_shared<PreparedEndpoint>(backend,tx,rx); RuntimeNetContext::set_endpoint(endpoint);
    }
    const auto setup=Clock::now();
    Region* region=nullptr; ExecutableGraph graph;
    std::string output="region";
    if (method=="flat" || opts.contains("--flat")) {
        auto built=build_inner(plan,role,network,tx,rx,true); output=built.output;
        built.graph.causal_order.clear(); built.graph.causal_execution.clear();
        for (auto n:built.static_order) {
            built.graph.causal_order.push_back(built.names[n]);
            built.graph.causal_execution.push_back(built.graph.elements.at(built.names[n]).get());
        }
        if (built.graph.domains.size()!=1) throw std::runtime_error("single domain only");
        built.graph.domains[0].causal_execution=built.graph.causal_execution;
        graph=std::move(built.graph);
    } else {
        Schema outer("phase4_owner"); outer.set_rate(Rate::from_hz(1000));
        outer.add_element("region",[&] { return std::make_unique<Region>(build_inner(plan,role,network,tx,rx),method,endpoint,std::chrono::microseconds(poll),opts.contains("--selection-timing")); });
        const bool memory=std::ranges::any_of(plan.at("actors").as_array(),[&](const auto& a){return str(a.at("node"))==role && str(a.at("kind"))=="delay";});
        if (memory) {
            outer.add_element("state",[]{return std::make_unique<Delay>(0.0);});
            outer.connect("state",0,"region",0); outer.connect("region",0,"state",0);
        }
        graph=Compiler::compile(outer); region=dynamic_cast<Region*>(graph.elements.at("region").get());
    }
    const auto setup_ns=ns(Clock::now()-setup);
    NodeRuntime runtime(std::move(graph));
    if (self_test) {
        if (!region) throw std::runtime_error("region needed");
        runtime.step(); runtime.step();
        std::vector<double> before;
        for (const auto& p:region->output_ports()) before.push_back(*static_cast<double*>(p.value_ptr));
        region->fail_once=true;
        bool threw=false; try {runtime.step();} catch (const std::runtime_error&) {threw=true;}
        if (!threw || runtime.step_count()!=2) throw std::runtime_error("fault not rolled back");
        std::size_t n=0;
        for (const auto& p:region->output_ports()) if (*static_cast<double*>(p.value_ptr)!=before[n++]) throw std::runtime_error("inner publication torn");
        runtime.step();
        if (runtime.real_output_of("region")!=3.0) throw std::runtime_error("memory staging/retry mismatch");
        std::cout<<"{\"rollback_and_memory_retry\":true}"<<std::endl; return 0;
    }
    std::string line;
    if (network) {
        std::cout<<j::serialize(j::object{{"ready",role}})<<std::endl;
        if (opts.contains("--wait-start") && (!std::getline(std::cin,line)||line!="start")) throw std::runtime_error("missing start");
        backend->set_data_peers({{"wss://127.0.0.1:"+opts.at("--next"),nodes[(index+2)%3],false}},tls);
    }
    std::vector<std::uint64_t> times; times.reserve(steps);
    j::array samples; std::string error;
    std::size_t max_log=0,max_buffer=0;
    const auto all_start=Clock::now(); Clock::time_point measured_start{};
    std::uint64_t cpu_start=0,owner_start=0; rusage usage_start{},usage_end{};
    try {
        for (std::size_t n=0;n<warmup+steps;++n) {
            if (n==warmup) {
                if (region) region->reset_metrics();
                measured_start=Clock::now(); cpu_start=cpu_ns(CLOCK_PROCESS_CPUTIME_ID); owner_start=cpu_ns(CLOCK_THREAD_CPUTIME_ID);
                getrusage(RUSAGE_SELF,&usage_start);
            }
            const auto begin=Clock::now();
            if (network && std::stoul(get("--slow-ms","0"))) std::this_thread::sleep_for(std::chrono::milliseconds(std::stoul(opts.at("--slow-ms"))));
            runtime.step();
            const auto value=runtime.real_output_of(output);
            const auto expected=!network?1.0:topology=="independent"?std::array{5.,2.,3.}[index]:double(n+(role=="B"?0:1));
            if (value!=expected) throw std::runtime_error("reference mismatch at "+std::to_string(n));
            samples.emplace_back(value);
            if (n>=warmup) times.push_back(ns(Clock::now()-begin));
            if (endpoint) for (const auto& s:endpoint->stream_statuses()) { max_log=std::max(max_log,s.send_log_depth); max_buffer=std::max(max_buffer,s.buffered_elements); }
        }
    } catch (const std::exception& e) { error=e.what(); }
    const auto elapsed=measured_start==Clock::time_point{}?0:ns(Clock::now()-measured_start);
    const auto process_cpu=cpu_ns(CLOCK_PROCESS_CPUTIME_ID)-cpu_start,owner_cpu=cpu_ns(CLOCK_THREAD_CPUTIME_ID)-owner_start;
    getrusage(RUSAGE_SELF,&usage_end);
    const auto drain=Clock::now();
    if (endpoint && error.empty()) while (true) {
        bool drained=true;
        for (const auto& s:endpoint->stream_statuses()) if (s.role==transport::StreamRole::Producer) drained&=s.send_log_depth==0 && s.elements==warmup+steps;
        if (drained) break;
        if (Clock::now()-drain>std::chrono::seconds(5)) { error="ACK drain timeout"; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto drain_ns=ns(Clock::now()-drain);
    j::array statuses,errors;
    if (endpoint) {
        for (const auto& s:endpoint->stream_statuses()) statuses.emplace_back(j::object{
            {"role",s.role==transport::StreamRole::Producer?"producer":"consumer"},{"fault",stream::to_string(s.fault)},
            {"state",s.state},{"packets",s.packets},{"elements",s.elements},{"send_log_depth",s.send_log_depth},
            {"buffered_elements",s.buffered_elements},{"blocked_ms_total",s.blocked_total.count()},
            {"unauthorized",s.unauthorized},{"malformed",s.malformed},{"replayed",s.replayed}});
        for (const auto& e:endpoint->take_transport_errors()) errors.emplace_back(e);
    }
    utsname info{}; uname(&info);
    j::object perf{{"warmup_steps",warmup},{"measured_steps",times.size()},{"elapsed_ns",elapsed},
        {"steps_per_second",elapsed?double(times.size())*1e9/double(elapsed):0.0},
        {"process_cpu_ns",process_cpu},{"owner_cpu_ns",owner_cpu},{"step_wall_ns",distribution(times)},
        {"process_lifetime_peak_rss_kib",usage_end.ru_maxrss},
        {"context_switches",j::object{{"ru_nvcsw",usage_end.ru_nvcsw-usage_start.ru_nvcsw},{"ru_nivcsw",usage_end.ru_nivcsw-usage_start.ru_nivcsw}}},
        {"queue_io_probes",region?region->io_probes:0},{"dependency_updates",region?region->dependency_updates:0},
        {"queue_dispatches",region?region->dispatches:0},{"ready_waits",region?region->waits:0},
        {"ready_wait_wall_ns",region?region->wait_ns:0},{"selection_wall_ns",region?region->selection_ns:0},
        {"readiness_checks",region?region->scans:0}};
    j::array durations; for (auto t:times) durations.emplace_back(t); perf["step_samples_ns"]=std::move(durations);
    j::object result{{"engine","s7cpp_region_beast_mtls"},{"execution",region?"NodeRuntime_owned_compiled_region":"NodeRuntime_flat"},
        {"role",role},{"approach",method},{"topology",topology},{"run_id",get("--run-id","cpu")},
        {"window",window},{"packet_frames",packet},{"work",std::stoul(get("--work","1"))},
        {"steps",samples.size()},{"samples",std::move(samples)},{"completed",error.empty()},
        {"error",error.empty()?j::value(nullptr):j::value(error)},{"performance",std::move(perf)},
        {"stream_statuses",std::move(statuses)},{"transport_errors",std::move(errors)},
        {"sampled_max_send_log",max_log},{"sampled_max_receive_buffer",max_buffer},
        {"ack_drain_ns",drain_ns},{"plan_setup_ns",setup_ns},{"plan_sha256",plan.at("sha256")},
        {"plan_file_sha256",hash_file(opts.at("--plan"))},{"runner_sha256",hash_file(argv[0])},
        {"hostname",info.nodename},{"platform",std::string(info.sysname)+" "+info.release+" "+info.machine},
        {"compiler",__VERSION__},{"poll_us",poll},{"fine_selection_timing",opts.contains("--selection-timing")},
        {"elapsed_seconds",double(ns(Clock::now()-all_start))/1e9}};
    if (network) {
        const auto path=opts.at("--credentials")+"/node.cert.pem";
        FILE* f=fopen(path.c_str(),"r"); X509* cert=f?PEM_read_X509(f,nullptr,nullptr,nullptr):nullptr;
        if (f) fclose(f);
        if (!cert) throw std::runtime_error("cannot read certificate");
        unsigned char bytes[EVP_MAX_MD_SIZE]; unsigned int count=0;
        const auto ok=X509_digest(cert,EVP_sha256(),bytes,&count); X509_free(cert);
        if (!ok) throw std::runtime_error("certificate hash");
        result["certificate_sha256"]=hex(bytes,count);
    }
    std::cout<<j::serialize(result)<<std::endl;
    if (opts.contains("--wait-start")) static_cast<void>(std::getline(std::cin,line));
    RuntimeNetContext::set_endpoint(nullptr);
    return error.empty()?0:2;
} catch (const std::exception& e) { std::cerr<<"Phase4: "<<e.what()<<'\n'; return 1; }
