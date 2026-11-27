// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/array_elements.hpp"

#include <array>
#include <utility>

namespace s7 {

ArrayDescriptor detail::vector_adapter_shape(SignalType type, std::int64_t components)
{
    if (components < 2) throw std::invalid_argument("A vector adapter requires at least two components.");
    return ArrayDescriptor(type, std::array{components});
}

VectorDelay::VectorDelay(ArrayDescriptor descriptor, ArrayPool::ReadLease initial)
    : ArrayElement("VectorDelay", true, Feedthrough::None),
      output_(register_array_output(port_names::out, descriptor))
{
    if (!initial) throw std::invalid_argument("VectorDelay requires an initialized array.");
    register_array_input(port_names::in, std::move(descriptor));
    static_cast<void>(output_.exchange({std::move(initial)}));
}

void VectorDelay::step()
{
    static_cast<void>(output_.exchange(array_input(0).share()));
}

} // namespace s7
