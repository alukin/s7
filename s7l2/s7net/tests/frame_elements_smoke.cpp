// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/net_frames.hpp>
#include <s7/element_catalog.hpp>
#include <algorithm>
#include <future>
#include <iostream>

using namespace s7;
namespace json = boost::json;
namespace {
constexpr auto id = "8a5e7c3b-1d2f-4a6b-9c8d-7e6f5a4b3c2d";
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void add(Schema& schema, std::string name, const char* type, json::object params = {}) {
    schema.add_element(make_element_spec(std::move(name), *ElementRegistry::instance().find(type), std::move(params)));
}
json::object params(const char* encoding = "raw", int packet = 7) {
    return {{"stream_id",id},{"channels",json::array{"left","right"}},{"frame_rate",json::array{48000,1}},
        {"encoding",encoding},{"max_frames_per_packet",packet},{"window_packets",2},{"max_wait_ms",1000}};
}
class Thrower final : public Element {
public:
    Thrower() : Element("Thrower",true) { register_input(0,"in",SignalType::I16,nullptr); }
    void step() override { if (fail) throw std::runtime_error("deliberate downstream failure"); }
    static inline bool fail = false;
};
void local(const char* encoding) {
    auto endpoint = std::make_shared<transport::InMemoryRuntimeNetEndpoint>();
    RuntimeNetContext::set_endpoint(endpoint);
    Schema producer{"producer"}; producer.set_rate(Rate::from_hz(48000));
    add(producer,"left","Constant",{{"type","I16"},{"value",-1234}});
    add(producer,"right","Constant",{{"type","I16"},{"value",2345}});
    add(producer,"tx","NetFrameSink",params(encoding));
    producer.connect("left",0,"tx",0); producer.connect("right",0,"tx",1);
    Schema consumer{"consumer"}; consumer.set_rate(Rate::from_hz(48000));
    auto rx = params(encoding); // inferred through every downstream channel
    add(consumer,"rx","NetFrameSource",rx);
    add(consumer,"gain","Gain",{{"type","I16"},{"factor",2}});
    add(consumer,"delay","Delay",{{"type","I16"},{"initial_value",0}});
    consumer.connect("rx",0,"gain",0); consumer.connect("rx",1,"delay",0);
    NodeRuntime sending{Compiler::compile(producer)}, receiving{Compiler::compile(consumer)};
    for (int n=0;n<19;++n) {
        sending.step(); receiving.step();
        check(receiving.output_of<SignalType::I16>("rx",0)==-1234,"left frame channel differs");
        check(receiving.output_of<SignalType::I16>("rx",1)==2345,"right frame channel differs");
        check(receiving.output_of<SignalType::I16>("gain")==-2468,"downstream scalar result differs");
        check(receiving.output_of<SignalType::I16>("delay")== (n ? 2345:0),"frame boundary changed Delay causality");
    }
    RuntimeNetContext::set_endpoint(nullptr);
}
void failure() {
    auto endpoint=std::make_shared<transport::InMemoryRuntimeNetEndpoint>(); RuntimeNetContext::set_endpoint(endpoint);
    auto p=params();p["type"]="I16";
    transport::FrameStreamConfig c; c.descriptor.stream_id=wire::parse_stream_id(id);c.descriptor.type=SignalType::I16;
    c.descriptor.channels={"left","right"};c.descriptor.max_frames_per_packet=7;c.descriptor.window_packets=2;c.descriptor.window_frames=14;
    const auto tx=endpoint->open_frame_producer(c);const std::int16_t values[]{10,20,30,40};
    check(endpoint->publish_frames(tx,values,2).frames == 2, "failure fixture publish failed");
    Schema schema{"failure"};schema.set_rate(Rate::from_hz(48000));add(schema,"rx","NetFrameSource",p);
    schema.add_element("fail",[]{return std::make_unique<Thrower>();});schema.connect("rx",0,"fail",0);
    NodeRuntime runtime{Compiler::compile(schema)};Thrower::fail=true;
    try { runtime.step();throw std::logic_error("downstream failure was ignored"); } catch(const std::runtime_error&) {}
    check(runtime.step_count()==0 && runtime.output_of<SignalType::I16>("rx")==0,"failed frame outputs were not restored");
    Thrower::fail=false;bool latched=false;
    try {runtime.step();}catch(const std::runtime_error& e){latched=std::string(e.what()).find("latched")!=std::string::npos;}
    check(latched,"retry consumed another frame after graph failure");RuntimeNetContext::set_endpoint(nullptr);
}
void endpoint_limits_and_close() {
    transport::InMemoryRuntimeNetEndpoint local;
    transport::FrameStreamConfig config;
    config.descriptor.stream_id=wire::parse_stream_id(id);
    config.descriptor.max_frames_per_packet=1;config.descriptor.window_packets=1;config.descriptor.window_frames=1;
    config.max_wait=std::chrono::seconds(5);
    const auto source=local.open_frame_consumer(config);
    auto waiting=std::async(std::launch::async,[&]{float value;return local.receive_frames(source,&value,1);});
    check(waiting.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout,"empty frame queue was mistaken for a stream fault");
    local.close_stream(source);
    check(waiting.get().status==frames::Status::Closed,"stream close did not release blocked receive");
    check(local.stream_statuses().front().fault==stream::FaultCode::None,"healthy stream close was reported as a fault");
    check(!local.recover_stream(config.descriptor.stream_id),"legacy recovery cleared a frame boundary");
    transport::BeastRuntimeNetEndpoint limited(transport::TransportLimits{.frame_buffer_bytes=1024});
    bool denied=false;try{static_cast<void>(limited.open_frame_producer(config));}catch(const std::exception&){denied=true;}
    check(denied,"node frame budget was ignored");
    transport::BeastRuntimeNetEndpoint producer,consumer;
    const auto port=producer.configure_server(0);
    consumer.set_node_uuid("11111111-1111-4111-8111-111111111111");
    consumer.configure_client("ws://127.0.0.1:"+std::to_string(port));
    config.consumer_nodes={"22222222-2222-4222-8222-222222222222"};
    const auto sink=producer.open_frame_producer(config);
    config.consumer_nodes.clear();config.max_wait=std::chrono::milliseconds(500);
    const auto refused=consumer.open_frame_consumer(config);
    const float value=2;check(producer.publish_frames(sink,&value,1).frames==1,"restricted producer failed");
    float output=0;check(consumer.receive_frames(refused,&output,1).status==frames::Status::SinkUnderflow,"unauthorized frame consumer received data");
    check(producer.stream_statuses().front().unauthorized>0,"frame consumer refusal was not reported");
}
void socket(const char* encoding, bool tls) {
    auto producer=std::make_shared<transport::BeastRuntimeNetEndpoint>();
    auto consumer=std::make_shared<transport::BeastRuntimeNetEndpoint>();
    transport::TlsConfig security;
    if(tls) {
        security.enabled=true;const std::string root=S7_TEST_CERT_DIR;
        security.cert_file=root+"/s7_runtime_node.cert.pem";security.key_file=root+"/s7_runtime_node.key.pem";security.ca_file=root+"/s7_root_ca.cert.pem";
    }
    const auto port=producer->configure_server(0,security);
    consumer->configure_client(std::string(tls?"wss://127.0.0.1:":"ws://127.0.0.1:")+std::to_string(port),security);
    Schema tx{"tx"};tx.set_rate(Rate::from_hz(48000));
    add(tx,"l","Constant",{{"type","I16"},{"value",-321}});add(tx,"r","Constant",{{"type","I16"},{"value",456}});
    add(tx,"left_sum","Accumulator",{{"type","I16"},{"initial_value",0}});
    add(tx,"right_sum","Accumulator",{{"type","I16"},{"initial_value",0}});
    tx.connect("l",0,"left_sum",0);tx.connect("r",0,"right_sum",0);
    add(tx,"out","NetFrameSink",params(encoding));tx.connect("left_sum",0,"out",0);tx.connect("right_sum",0,"out",1);
    auto p=params(encoding);p["type"]="I16";
    Schema rx{"rx"};rx.set_rate(Rate::from_hz(48000));add(rx,"in","NetFrameSource",p);
    NodeRuntime sending{Compiler::compile(tx)},receiving{Compiler::compile(rx)};
    RuntimeNetContext::set_endpoint(producer);sending.step(); // cache the producer endpoint before changing context
    RuntimeNetContext::set_endpoint(consumer);
    auto reader=std::async(std::launch::async,[&]{for(int n=0;n<29;++n){receiving.step();check(receiving.output_of<SignalType::I16>("in",0)==-321*(n+1) && receiving.output_of<SignalType::I16>("in",1)==456*(n+1),"socket frame channels differ");}});
    for(int n=1;n<29;++n) sending.step();
    reader.get();
    const auto status = consumer->stream_statuses().front();
    check(status.elements == 58 && status.packets > 0, "frame stream counters do not describe delivered samples");
    RuntimeNetContext::set_endpoint(nullptr);
}
}
int main()try {
    auto& registry=ElementRegistry::instance();register_builtin_elements(registry);register_net_elements(registry);register_net_elements(registry);
    check(registry.find("NetFrameSource")->metadata().variadic_outputs.enabled,"frame source channel metadata absent");
    ElementCatalogService catalog(registry);
    check(catalog.find_element("NetFrameSource")->output_count == 2 && catalog.find_element("NetFrameSink")->input_count == 2,
        "catalog does not show default channel counts");
    for(const auto& [name,encoding]: {std::pair{"raw",frames::Encoding::Raw},{"lz4",frames::Encoding::Lz4},{"zstd",frames::Encoding::Zstd}}) {
        if(!frames::encoding_available(encoding))continue;
        local(name); socket(name,false); socket(name,true);
    }
    failure(); endpoint_limits_and_close();
    for(auto update : {json::object{{"channels",json::array{"x","x"}}}, json::object{{"channels",json::array{}}},
                       json::object{{"frame_rate",json::array{0,1}}},json::object{{"type","I12"}},json::object{{"encoding","lossy"}},
                       json::object{{"max_frames_per_packet",4294967296ULL}}}) {
        auto p=params();p["type"]="I16";for(auto& item:update)p[item.key()]=item.value();
        Schema bad{"bad"};add(bad,"rx","NetFrameSource",p);bool rejected=false;
        try {static_cast<void>(Compiler::compile(bad));}catch(const std::exception&){rejected=true;}
        check(rejected,"bad frame descriptor accepted");
    }
    std::cout<<"frame elements: typed channels, batching, raw/codecs, WebSocket/TLS, Delay and failed-step retry passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
