#pragma once

#include <Qt>

namespace s7studio::diagram {

// QGraphicsItem custom data roles used to preserve schema identity and typed parameters.
constexpr int kNodeIdRole = Qt::UserRole + 100;
constexpr int kNodeTypeRole = Qt::UserRole + 101;
constexpr int kNodeTitleRole = Qt::UserRole + 102;
constexpr int kNodeParamsRole = Qt::UserRole + 103;
constexpr int kNodeChannelCountRole = Qt::UserRole + 104;

} // namespace s7studio::diagram

