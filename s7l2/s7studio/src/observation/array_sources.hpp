// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "diagram/element_ports.hpp"

#include <QHash>
#include <s7/array_mailbox.hpp>

namespace s7studio::observation {

struct ArraySource {
    diagram::ArrayObserverKind kind;
    std::shared_ptr<s7::ArrayMailbox> mailbox;
};

// Keyed by the full probe name in the frozen run configuration.
using ArraySources = QHash<QString, ArraySource>;

} // namespace s7studio::observation
