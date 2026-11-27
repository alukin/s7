// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_ARRAY_ELEMENTS_HPP
#define S7CORE_INCLUDE_S7_ARRAY_ELEMENTS_HPP

#include "s7/array_port.hpp"

namespace s7 {

/// One domain evaluation of causal memory for a whole array. The initializer
/// must be a sealed execution lease matching the port descriptor.
class VectorDelay final : public ArrayElement {
public:
    VectorDelay(ArrayDescriptor descriptor, ArrayPool::ReadLease initial);
    void step() override;

private:
    ArrayOutputSlot& output_;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ARRAY_ELEMENTS_HPP
