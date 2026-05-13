# Oscilloscope Integration Guide

This guide documents integration patterns that match the current `s7ui` codebase.

## Scope

- `OscilloscopeWidget` is a reusable Qt widget for live probe visualization.
- `SinkNodeItem` is the in-canvas visual sink node that renders a small preview and opens a detail window.
- `SchemaSession` is the runtime bridge that provides `probe(id)` and `complexProbe(id)`.

## Minimal Example

```cpp
#include "oscilloscope_widget.hpp"
#include <s7probe.hpp>

auto* oscilloscope = new OscilloscopeWidget(parentWidget);

// For real-valued signals
std::shared_ptr<s7::Probe> probe = /* obtain from SchemaSession */;
oscilloscope->attachProbe(probe);

// Optional display tuning
oscilloscope->setSampleRate(1000.0);   // Hz
oscilloscope->setTimeWindow(5.0);      // seconds

oscilloscope->show();
```

## Current MainWindow Integration

`MainWindow` already wires visual sinks to runtime probes through:

- `MainWindow::linkSinkProbes()`
- `MainWindow::unlinkSinkProbes()`

Current behavior:

1. Iterate scene nodes.
2. Keep nodes tagged with visual-sink role.
3. Resolve node id from item data.
4. For `ComplexScopeSink` call `schemaSession_->complexProbe(id)` and attach via `SinkNodeItem::setComplexProbe(...)`.
5. Otherwise call `schemaSession_->probe(id)` and attach via `SinkNodeItem::setProbe(...)`.

This means the common path for the schema editor is already implemented in `s7ui/src/main_window.cpp`.

## Pattern 1: Standalone Oscilloscope Window

Use this when you want a detached instrument window.

```cpp
class OscilloscopeWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit OscilloscopeWindow(std::shared_ptr<s7::Probe> probe, QWidget* parent = nullptr)
        : QMainWindow(parent)
    {
        auto* osc = new OscilloscopeWidget(this);
        setCentralWidget(osc);
        osc->attachProbe(std::move(probe));
        resize(900, 600);
    }
};
```

For complex probes, use `attachComplexProbe(...)` instead.

## Pattern 2: Docked Instrument Panel

Use this when the editor should keep one persistent oscilloscope panel.

```cpp
auto* dock = new QDockWidget("Instrument", this);
auto* osc = new OscilloscopeWidget(dock);
dock->setWidget(osc);
addDockWidget(Qt::RightDockWidgetArea, dock);

// Later, when selection changes:
if (auto probe = schemaSession_->probe(nodeId.toStdString())) {
    osc->attachProbe(probe);
} else if (auto cprobe = schemaSession_->complexProbe(nodeId.toStdString())) {
    osc->attachComplexProbe(cprobe);
} else {
    osc->clearProbe();
}
```

## Pattern 3: Keep In-Canvas Sink Nodes Only

If you do not need a separate instrument dock/window, rely only on `SinkNodeItem`:

- In-canvas preview updates at ~30 Hz when probe is attached.
- Double-click opens a larger detail window.
- `SinkNodeItem::setInputCount(...)` controls number of inputs for real sinks.

This is the current default UX in `s7ui`.

## Lifecycle and Threading Notes

- `SchemaSession` runs compute work asynchronously.
- GUI reads probe buffers from the Qt GUI thread.
- `OscilloscopeWidget` and `SinkNodeItem` poll via `QTimer` (~30 Hz).
- Probe reads are thread-safe through probe internals.

## Performance Notes

- Lower sample cost:

```cpp
oscilloscope->setSampleCount(256);
```

- Prefer time-window mode for user-facing UI:

```cpp
oscilloscope->setSampleRate(1.0 / dt);
oscilloscope->setTimeWindow(5.0);
```

## Common Pitfalls

- Do not call UI methods from worker threads.
- Attach either a real probe or a complex probe to a widget at a time.
- For sink nodes with embedded preview widgets, keep preview widgets transparent to mouse events so node dragging remains usable.

## File References

- Runtime session: `s7ui/src/schema_session.hpp`
- Main editor integration: `s7ui/src/main_window.cpp`
- Sink node item: `s7ui/src/elements/sink_node_item.cpp`
- Widget API: `s7ui/src/oscilloscope_widget.hpp`
- Widget implementation: `s7ui/src/oscilloscope_widget.cpp`
