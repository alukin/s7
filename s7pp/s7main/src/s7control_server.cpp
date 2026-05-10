#include "s7control_server.hpp"

#include "s7node_responder.hpp"

#include <folly/SocketAddress.h>
#include <rsocket/RSocket.h>
#include <rsocket/transports/tcp/TcpConnectionAcceptor.h>

#include <iostream>
#include <thread>

namespace s7 {

auto runControlServer(const std::shared_ptr<NodeRuntimeService>& service,
                      const int port) -> int
{
    rsocket::TcpConnectionAcceptor::Options opts;
    opts.address = folly::SocketAddress("::", static_cast<uint16_t>(port));
    opts.threads = 2;

    auto server = rsocket::RSocket::createServer(
        std::make_unique<rsocket::TcpConnectionAcceptor>(std::move(opts)));

    auto* raw = server.get();
    std::thread server_thread([raw, service] {
        raw->startAndPark([service](const rsocket::SetupParameters&) {
            return std::make_shared<S7NodeResponder>(service);
        });
    });

    std::cout << "Control server listening on port " << port << " (RSocket over TCP)\n";
    std::cout << "Press Enter to stop server...\n";
    std::string line;
    std::getline(std::cin, line);

    server->unpark();
    server_thread.join();
    return 0;
}

} // namespace s7


