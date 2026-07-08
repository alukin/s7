#pragma once

#include <string>
#include <vector>

#include "s7/core.hpp"

namespace s7 {

struct IoChannel {
    std::string name;
    std::string signal_kind;
};

class IoCatalog {
public:
    void register_input(IoChannel channel);
    void register_output(IoChannel channel);

    [[nodiscard]] const std::vector<IoChannel>& inputs() const;
    [[nodiscard]] const std::vector<IoChannel>& outputs() const;

private:
    std::vector<IoChannel> inputs_;
    std::vector<IoChannel> outputs_;
};

} // namespace s7

