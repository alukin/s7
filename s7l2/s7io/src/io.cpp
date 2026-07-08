#include "s7/io.hpp"

#include <utility>

namespace s7 {

void IoCatalog::register_input(IoChannel channel) {
    inputs_.push_back(std::move(channel));
}

void IoCatalog::register_output(IoChannel channel) {
    outputs_.push_back(std::move(channel));
}

const std::vector<IoChannel>& IoCatalog::inputs() const {
    return inputs_;
}

const std::vector<IoChannel>& IoCatalog::outputs() const {
    return outputs_;
}

} // namespace s7

