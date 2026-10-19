#ifndef S7IO_INCLUDE_S7_IO_HPP
#define S7IO_INCLUDE_S7_IO_HPP

#include <string>
#include <vector>

#include "s7/core.hpp"
#include "s7/file_io.hpp"
#include "s7/recorded_source.hpp"

namespace s7 {

// Register built-in I/O elements; safe to call repeatedly.
void register_io_elements(ElementRegistry& registry);

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

#endif // S7IO_INCLUDE_S7_IO_HPP
