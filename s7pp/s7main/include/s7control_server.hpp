#pragma once

#include "s7runtime.hpp"

#include <memory>

namespace s7 {

auto runControlServer(const std::shared_ptr<NodeRuntimeService>& service, int port) -> int;

} // namespace s7

