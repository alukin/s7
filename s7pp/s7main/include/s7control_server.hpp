#pragma once

#include "s7runtime.hpp"

#include <memory>

namespace s7 {

// Starts control server and blocks until stopped.
// Throws std::runtime_error when server mode is unavailable.
auto runControlServer(const std::shared_ptr<NodeRuntimeService>& service,
                      int port) -> int;

} // namespace s7

