#include "s7/core.hpp"

#include <stdexcept>
#include <utility>

namespace s7 {
Schema::Schema(std::string name)
    : Element("Schema", false),  // No state
      name_(std::move(name))
{
    // Schema has no ports - they're registered by subclass if needed
}

void Schema::step() {
    throw std::logic_error("Schema::step is not implemented yet; compile the schema into a runtime graph instead.");
}

void Schema::add_element(std::string id, ElementFactory factory) {
    elements_.push_back(ElementSpec{std::move(id), std::move(factory)});
}

void Schema::connect(std::string from_element,
                     std::size_t from_port,
                     std::string to_element,
                     std::size_t to_port) {
    connections_.push_back(Connection{std::move(from_element), from_port, std::move(to_element), to_port});
}

const std::string& Schema::name() const {
    return name_;
}

const std::vector<ElementSpec>& Schema::elements() const {
    return elements_;
}

const std::vector<Connection>& Schema::connections() const {
    return connections_;
}

bool ValidationResult::ok() const {
    return errors.empty();
}

} // namespace s7

