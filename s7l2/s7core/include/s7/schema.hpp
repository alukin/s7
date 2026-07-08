#pragma once

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

    const std::string& name() const;
    const std::vector<ElementSpec>& elements() const;
    const std::vector<Connection>& connections() const;

private:
    std::string name_;
    std::vector<ElementSpec> elements_;
    std::vector<Connection> connections_;
};

} // namespace s7

