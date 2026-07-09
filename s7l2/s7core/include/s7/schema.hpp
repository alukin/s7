#ifndef S7CORE_INCLUDE_S7_SCHEMA_HPP
#define S7CORE_INCLUDE_S7_SCHEMA_HPP

#include "s7/element.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace s7 {

struct Connection {
    std::string from_element;
    std::size_t from_port{};
    std::string to_element;
    std::size_t to_port{};
};

struct ElementSpec {
    std::string id;
    ElementFactory factory;
};

class Schema : public Element {
public:
    explicit Schema(std::string name);

    void step() override;

    void add_element(std::string id, ElementFactory factory);
    void connect(std::string from_element,
                 std::size_t from_port,
                 std::string to_element,
                 std::size_t to_port);

    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] const std::vector<ElementSpec>& elements() const;
    [[nodiscard]] const std::vector<Connection>& connections() const;

private:
    std::string name_;
    std::vector<ElementSpec> elements_;
    std::vector<Connection> connections_;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_SCHEMA_HPP
