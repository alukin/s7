#pragma once

#include <Qt>

namespace s7studio::diagram {

// QGraphicsItem custom data roles used to preserve schema identity and typed parameters.
constexpr int kNodeIdRole = Qt::UserRole + 100;
constexpr int kNodeTypeRole = Qt::UserRole + 101;
constexpr int kNodeTitleRole = Qt::UserRole + 102;
constexpr int kNodeParamsRole = Qt::UserRole + 103;
constexpr int kNodeSchemaRole = Qt::UserRole + 104;
constexpr int kNodeChannelCountRole = Qt::UserRole + 105;
constexpr int kTextFrameTextRole = Qt::UserRole + 106;
constexpr int kTextFrameTextColorRole = Qt::UserRole + 107;
constexpr int kTextFrameLineColorRole = Qt::UserRole + 108;
constexpr int kTextFrameFillColorRole = Qt::UserRole + 109;
constexpr int kTextFrameFontFamilyRole = Qt::UserRole + 110;
constexpr int kTextFrameFontSizeRole = Qt::UserRole + 111;
// node_uuid of the runtime node the element is placed on (empty: not placed).
constexpr int kNodePlacementRole = Qt::UserRole + 112;

} // namespace s7studio::diagram

