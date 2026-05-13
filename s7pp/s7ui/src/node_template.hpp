#pragma once

#include <QString>

namespace s7ui::ui {

/// Lightweight specification used by NodeFactory and MainWindow
/// to describe how to build a scene node item.
struct NodeTemplate
{
    QString category;
    QString typeName;
    QString defaultIdPrefix;
    int inputPorts  = 1;
    int outputPorts = 1;
    bool visualSink = false;
};

} // namespace s7ui::ui

