// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "diagram/items.hpp"
#include "diagram/net_model.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace s7studio::diagram {

// A set of edits applied (and reverted) as one undo step.
struct DiagramChange {
    struct Rehang {
        std::shared_ptr<SignalWire> wire;
        WireEnd before;
        WireEnd after;
    };
    struct Bends {
        std::shared_ptr<SignalWire> wire;
        QVector<QPointF> before;
        QVector<QPointF> after;
    };
    struct ElementEdit {
        std::shared_ptr<ElementNode> node;
        ElementNode::Element before;
        ElementNode::Element after;
    };
    struct TextFrameEdit {
        std::shared_ptr<TextFrameNode> node;
        boost::json::object before;
        boost::json::object after;
    };
    struct SizeEdit {
        std::shared_ptr<ElementNode> node;
        QSize before;
        QSize after;
    };

    std::vector<std::shared_ptr<QSchematic::Items::Node>> addedNodes;
    std::vector<std::shared_ptr<QSchematic::Items::Node>> removedNodes;
    std::vector<std::shared_ptr<SignalWire>> addedWires;
    std::vector<std::shared_ptr<SignalWire>> removedWires;
    std::vector<Rehang> rehung;
    std::vector<Bends> bends;
    std::vector<ElementEdit> elements;
    std::vector<TextFrameEdit> textFrames;
    std::vector<SizeEdit> sizes;
    std::optional<std::pair<std::vector<DiagramProbe>, std::vector<DiagramProbe>>> probes;
    std::optional<std::pair<boost::json::array, boost::json::array>> observersPanel;

    [[nodiscard]] bool empty() const;
};

} // namespace s7studio::diagram
