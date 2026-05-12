# Oscilloscope Widget Integration Guide

## Quick Start

### Minimal Example

```cpp
#include "oscilloscope_widget.hpp"
#include <s7probe.hpp>

// Create widget
auto oscilloscope = new OscilloscopeWidget(parentWidget);

// Attach a probe from the schema
std::shared_ptr<s7::Probe> probe = /* from schema */;
oscilloscope->attachProbe(probe);

// Show it
oscilloscope->show();

// Widget automatically polls probe at 30 Hz
// Data is displayed in real-time
```

## Integration Patterns

### Pattern 1: Standalone Window

Simplest approach for a single oscilloscope device:

```cpp
class OscilloscopeWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit OscilloscopeWindow(std::shared_ptr<s7::Probe> probe, QWidget* parent = nullptr)
        : QMainWindow(parent)
    {
        auto* oscilloscope = new OscilloscopeWidget(this);
        setCentralWidget(oscilloscope);
        oscilloscope->attachProbe(probe);
        setWindowTitle("Oscilloscope: " + QString::fromStdString(probe->getId()));
        resize(800, 600);
    }
};

// Usage in MainWindow:
void MainWindow::openOscilloscope(const std::string& probe_id)
{
    auto probe = schemaSession_->probe(probe_id);
    if (!probe) return;
    
    auto* window = new OscilloscopeWindow(probe);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}
```

### Pattern 2: Dock Widget in Main UI

Multiple instruments in a dockable interface:

```cpp
class InstrumentDock : public QDockWidget
{
    Q_OBJECT
public:
    InstrumentDock(QWidget* parent = nullptr)
        : QDockWidget("Instruments", parent)
    {
        auto* container = new QWidget();
        auto* layout = new QVBoxLayout(container);
        
        setupInstrumentList();
        oscilloscope_ = new OscilloscopeWidget();
        
        layout->addWidget(instrumentList_);
        layout->addWidget(oscilloscope_);
        setWidget(container);
        
        connect(instrumentList_, &QListWidget::itemClicked, this, &InstrumentDock::onInstrumentSelected);
    }
    
    void setSchemaSession(std::shared_ptr<SchemaSession> session) {
        session_ = session;
        populateInstrumentList();
    }

private:
    void populateInstrumentList() {
        instrumentList_->clear();
        if (!session_) return;
        
        for (const auto& id : session_->getSinkItemIds()) {
            instrumentList_->addItem(QString::fromStdString(id));
        }
    }
    
    void onInstrumentSelected(QListWidgetItem* item) {
        auto probe = session_->probe(item->text().toStdString());
        if (!probe) {
            probe = session_->complexProbe(item->text().toStdString());
        }
        
        if (probe) oscilloscope_->attachProbe(probe);
        else oscilloscope_->clearProbe();
    }

private:
    QListWidget* instrumentList_;
    OscilloscopeWidget* oscilloscope_;
    std::shared_ptr<SchemaSession> session_;
};

// In MainWindow::setupUI():
auto* dock = new InstrumentDock();
addDockWidget(Qt::RightDockWidgetArea, dock);
```

### Pattern 3: Tab Interface

Multiple oscilloscopes in parallel:

```cpp
class OscilloscopeTabWidget : public QTabWidget
{
    Q_OBJECT
public:
    OscilloscopeTabWidget(std::shared_ptr<SchemaSession> session, QWidget* parent = nullptr)
        : QTabWidget(parent), session_(session)
    {
        connect(this, &QTabWidget::tabCloseRequested, this, &OscilloscopeTabWidget::closeTab);
        setTabsClosable(true);
    }
    
    void addProbe(const std::string& probe_id) {
        auto probe = session_->probe(probe_id);
        if (!probe) {
            auto cprobe = session_->complexProbe(probe_id);
            if (!cprobe) return;
            addComplexProbe(cprobe);
            return;
        }
        
        auto* osc = new OscilloscopeWidget();
        osc->attachProbe(probe);
        int index = addTab(osc, QString::fromStdString(probe_id));
        probes_[index] = probe;
    }
    
    void addComplexProbe(std::shared_ptr<s7::ComplexProbe> cprobe) {
        auto* osc = new OscilloscopeWidget();
        osc->attachComplexProbe(cprobe);
        int index = addTab(osc, QString::fromStdString(cprobe->getId()));
        complexProbes_[index] = cprobe;
    }

private:
    void closeTab(int index) {
        removeTab(index);
        probes_.erase(index);
        complexProbes_.erase(index);
    }
    
    std::shared_ptr<SchemaSession> session_;
    std::map<int, std::shared_ptr<s7::Probe>> probes_;
    std::map<int, std::shared_ptr<s7::ComplexProbe>> complexProbes_;
};
```

### Pattern 4: Grid Layout (Multiple Scopes at Once)

Display many small oscilloscopes in a grid:

```cpp
class OscilloscopeGrid : public QWidget
{
    Q_OBJECT
public:
    OscilloscopeGrid(std::shared_ptr<SchemaSession> session, QWidget* parent = nullptr)
        : QWidget(parent), session_(session)
    {
        auto* layout = new QGridLayout(this);
        layout->setSpacing(10);
        
        int row = 0, col = 0;
        const int colsPerRow = 2;
        
        for (const auto& probe_id : session->getSinkItemIds()) {
            auto probe = session->probe(probe_id);
            if (!probe) continue;
            
            auto* osc = new OscilloscopeWidget();
            osc->attachProbe(probe);
            osc->setMinimumSize(400, 300);
            
            layout->addWidget(osc, row, col);
            
            col++;
            if (col >= colsPerRow) {
                col = 0;
                row++;
            }
        }
    }

private:
    std::shared_ptr<SchemaSession> session_;
};
```

## Advanced Usage

### Custom Display Parameters

```cpp
// Adjust time window
oscilloscope->setTimeWindow(10.0);  // Show 10 seconds

// Or fixed sample count
oscilloscope->setSampleCount(2048);

// Force immediate redraw (normally automatic)
oscilloscope->redraw();
```

### Handling Probe Lifecycle

```cpp
class OscilloscopeController
{
public:
    void attachProbe(std::shared_ptr<s7::Probe> probe) {
        if (!oscilloscope_) {
            oscilloscope_ = new OscilloscopeWidget();
            oscilloscope_->show();
        }
        oscilloscope_->attachProbe(probe);
    }
    
    void detachProbe() {
        if (oscilloscope_) {
            oscilloscope_->clearProbe();
        }
    }
    
    void shutdown() {
        if (oscilloscope_) {
            oscilloscope_->clearProbe();
            delete oscilloscope_;
            oscilloscope_ = nullptr;
        }
    }

private:
    OscilloscopeWidget* oscilloscope_ = nullptr;
};
```

### Multi-Probe Comparison View

Display two probes side-by-side for comparison:

```cpp
class OscilloscopeComparison : public QWidget
{
    Q_OBJECT
public:
    OscilloscopeComparison(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* layout = new QHBoxLayout(this);
        osc1_ = new OscilloscopeWidget();
        osc2_ = new OscilloscopeWidget();
        layout->addWidget(osc1_);
        layout->addWidget(osc2_);
    }
    
    void setProbes(std::shared_ptr<s7::Probe> probe1,
                   std::shared_ptr<s7::Probe> probe2) {
        osc1_->attachProbe(probe1);
        osc2_->attachProbe(probe2);
        
        // Optionally sync time windows
        osc1_->setTimeWindow(5.0);
        osc2_->setTimeWindow(5.0);
    }

private:
    OscilloscopeWidget* osc1_;
    OscilloscopeWidget* osc2_;
};
```

## Schema Session Integration

### In SchemaSession (Recommended Changes)

```cpp
// In schema_session.hpp:
class SchemaSession
{
public:
    /// Get list of all probe IDs in schema
    [[nodiscard]] std::vector<std::string> getSinkItemIds() const {
        return sink_item_ids_;  // Already exists
    }
    
    /// Create and show oscilloscope for a probe
    void showOscilloscope(const std::string& probe_id) {
        auto probe = this->probe(probe_id);
        if (!probe) probe = this->complexProbe(probe_id);
        if (!probe) return;
        
        // Factory method for UI
        Q_EMIT oscilloscopeRequested(probe_id);
    }
};
```

### In MainWindow

```cpp
void MainWindow::linkSinkProbes()
{
    if (!schemaSession_) return;
    
    // Populate oscilloscope list
    for (const auto& probe_id : schemaSession_->getSinkItemIds()) {
        auto probe = schemaSession_->probe(probe_id);
        if (!probe) probe = schemaSession_->complexProbe(probe_id);
        if (!probe) continue;
        
        // Add to instrument selector
        instrumentList_->addItem(QString::fromStdString(probe_id));
    }
}

void MainWindow::onInstrumentSelected(const QString& probe_id) {
    auto probe = schemaSession_->probe(probe_id.toStdString());
    if (!probe) {
        auto cprobe = schemaSession_->complexProbe(probe_id.toStdString());
        if (!cprobe) return;
        oscilloscope_->attachComplexProbe(cprobe);
    } else {
        oscilloscope_->attachProbe(probe);
    }
}
```

## Performance Tuning

### For Many Channels (>8)

```cpp
// Reduce sample count for faster updates
oscilloscope->setSampleCount(256);  // vs default 512

// Disable legend if too crowded
plot_->legend->setVisible(false);
```

### For High-Frequency Display

```cpp
// Enable QCustomPlot's adaptive rendering
plot_->setNotAntialiasedElements(QCP::aeAll);

// Reduce poll rate if 30 Hz is too much
// (Would require extending widget API)
```

### Memory-Efficient Reading

The probe's `readChannel()` is already optimized:
- Lock held only during memcpy
- Circular buffer prevents unnecessary copies
- No unbounded allocations

## Error Handling

```cpp
try {
    auto probe = schemaSession_->probe(probe_id);
    if (!probe) {
        qWarning() << "Probe not found:" << probe_id;
        return false;
    }
    
    oscilloscope_->attachProbe(probe);
    return true;
} catch (const std::exception& e) {
    qCritical() << "Error attaching probe:" << e.what();
    return false;
}
```

## Thread Safety Notes

- **OscilloscopeWidget** itself is NOT thread-safe
  - Create and show only from Qt GUI thread
  - Call `attachProbe()` only from GUI thread

- **s7::Probe** IS thread-safe
  - Compute thread writes via `tick()`
  - GUI thread reads via `readChannel()`
  - Internal mutex guards access

- **Polling loop** is internal
  - QTimer ensures GUI thread execution
  - No manual synchronization needed

## Build Integration

The widget is already included in the s7ui executable:

```cmake
# In s7ui/CMakeLists.txt (already updated)
add_executable(s7ui
    src/oscilloscope_widget.cpp
    src/oscilloscope_widget.hpp
    # ... other sources
)
```

To use in your UI code:
```cpp
#include "oscilloscope_widget.hpp"

// Automatically finds the header via s7ui/src/ in target_include_directories
```

## Debugging Tips

### Check Widget Status
```cpp
// Verify probe attachment
if (auto probe = oscilloscope_->probe()) {
    qDebug() << "Probe attached:" << probe->getId();
    qDebug() << "Channels:" << probe->channelCount();
}
```

### Monitor Data Flow
```cpp
// In updateRealData():
qDebug() << "Sample count:" << samples.size();
qDebug() << "Y range:" << minY << "to" << maxY;
qDebug() << "Graph count:" << graphs_.size();
```

### Check Polling
```cpp
// Verify timer is running
qDebug() << "Poll timer active:" << pollTimer_->isActive();
qDebug() << "Update interval:" << pollTimer_->interval() << "ms";
```

---

**Version**: 1.0  
**Last Updated**: May 12, 2026  

For API details, see `oscilloscope_widget.hpp` header documentation.

