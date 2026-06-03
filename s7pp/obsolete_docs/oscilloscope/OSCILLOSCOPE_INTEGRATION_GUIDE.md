eptable:p# Integration Guide: Multi-Probe Oscilloscope in MainWindow

## Quick Start Integration

### 1. Add to MainWindow Header

Add the oscilloscope widget member to `MainWindow`:

```cpp
// In main_window.hpp

class MainWindow : public QMainWindow {
    Q_OBJECT

private:
    // ... existing members ...
    
    // NEW: Oscilloscope instrument
    OscilloscopeWidget* oscilloscope_ = nullptr;
    QDockWidget* oscilloscopeDock_ = nullptr;
};
```

### 2. Include Oscilloscope Header

```cpp
// In main_window.cpp
#include "oscilloscope_widget.hpp"
```

### 3. Create UI Dock Widget

Add to `MainWindow::setupViewMenu()` or similar:

```cpp
void MainWindow::setupViewMenu() {
    auto viewMenu = menuBar()->addMenu(tr("View"));
    
    // ... existing menu items ...
    
    // Oscilloscope dock
    oscilloscopeDock_ = new QDockWidget(tr("Oscilloscope"), this);
    oscilloscope_ = new OscilloscopeWidget(this);
    oscilloscopeDock_->setWidget(oscilloscope_);
    
    // Configure dock appearance
    oscilloscopeDock_->setAllowedAreas(
        Qt::RightDockWidgetArea | 
        Qt::LeftDockWidgetArea | 
        Qt::BottomDockWidgetArea
    );
    
    addDockWidget(Qt::RightDockWidgetArea, oscilloscopeDock_);
    
    // Add toggle action to View menu
    auto toggleAction = oscilloscopeDock_->toggleViewAction();
    toggleAction->setText(tr("Oscilloscope"));
    viewMenu->addAction(toggleAction);
}
```

### 4. Wire Probes on Runtime Execution

Add to `MainWindow::onRunSchema()`:

```cpp
void MainWindow::onRunSchema() {
    // ... existing run setup ...
    
    // Clear previous probes from oscilloscope
    if (oscilloscope_) {
        oscilloscope_->clearAllProbes();
    }
    
    // Start simulation
    schemaRunner_->run();
    
    // After simulation starts, link probes to oscilloscope
    linkOscilloscopeProbes();
}

void MainWindow::linkOscilloscopeProbes() {
    if (!schemaRunner_ || !oscilloscope_) {
        return;
    }
    
    // Query all available probes from runtime
    const auto& probeIds = schemaRunner_->probeIds();
    for (const auto& probeId : probeIds) {
        // Real probe
        if (auto probe = schemaRunner_->probe(probeId)) {
            oscilloscope_->attachProbe(
                probe, 
                QString::fromStdString(probeId)
            );
        }
        // Complex probe
        else if (auto cprobe = schemaRunner_->complexProbe(probeId)) {
            oscilloscope_->attachComplexProbe(
                cprobe, 
                QString::fromStdString(probeId)
            );
        }
    }
    
    // Configure oscilloscope display
    oscilloscope_->setTimeWindow(5.0);      // 5 second window
    oscilloscope_->setSampleRate(1000.0);   // 1 kHz
}
```

### 5. Clean Up on Reset

Add to `MainWindow::onResetSimulation()`:

```cpp
void MainWindow::onResetSimulation() {
    // ... existing reset logic ...
    
    // Clear probes from oscilloscope
    if (oscilloscope_) {
        oscilloscope_->clearAllProbes();
    }
}
```

---

## Advanced Integration Patterns

### Pattern 1: User-Selectable Probes

Allow user to pick which probes to display:

```cpp
void MainWindow::onAddProbeToOscilloscope() {
    if (!schemaRunner_) return;
    
    // Show dialog to select which probe to add
    QStringList probeList;
    for (const auto& id : schemaRunner_->probeIds()) {
        probeList << QString::fromStdString(id);
    }
    
    bool ok;
    QString selected = QInputDialog::getItem(
        this, 
        "Select Probe",
        "Add probe to oscilloscope:",
        probeList,
        0, 
        false,
        &ok
    );
    
    if (!ok) return;
    
    // Attach selected probe
    if (auto probe = schemaRunner_->probe(selected.toStdString())) {
        oscilloscope_->attachProbe(probe, selected);
    }
}

void MainWindow::onRemoveProbeFromOscilloscope() {
    if (!oscilloscope_) return;
    
    // Show context menu or dialog to remove
    // This would require tracking probe labels
    // For now, users can click oscilloscope context menu
    // or use clearAllProbes() + re-add
}
```

### Pattern 2: Context Menu Integration

Add right-click on sink nodes to probe:

```cpp
// In SinkNodeItem or equivalent
void SinkNodeItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QMenu menu;
    
    // Existing items...
    
    if (auto mainWindow = getMainWindow()) {
        auto addToOscAction = menu.addAction("Add to Oscilloscope");
        connect(addToOscAction, &QAction::triggered, 
                [this, mainWindow]() {
            // Add this node's probe to oscilloscope
            auto probe = this->getProbe();
            if (probe) {
                mainWindow->getOscilloscope()->attachProbe(
                    probe,
                    this->nodeName()
                );
            }
        });
    }
    
    menu.exec(event->screenPos());
}
```

### Pattern 3: Probe Groups/Tabs

For very large schemas with many probes:

```cpp
// Create multiple oscilloscopes for different signal groups
struct ProbeGroup {
    QString name;
    OscilloscopeWidget* widget;
    QStringList probeIds;
};

class MainWindow {
private:
    std::vector<ProbeGroup> probeGroups_;
    
    void createProbeGroups() {
        // Group 1: Input stages
        addProbeGroup("Input Stage", {"input_1", "input_2"});
        
        // Group 2: Processing
        addProbeGroup("DSP Processing", {"filter_output", "amplifier_out"});
        
        // Group 3: Output
        addProbeGroup("Output Stage", {"final_output", "error_signal"});
    }
    
    void addProbeGroup(const QString& name, 
                      const QStringList& probeIds) {
        auto widget = new OscilloscopeWidget();
        auto dock = new QDockWidget(name, this);
        dock->setWidget(widget);
        
        ProbeGroup group;
        group.name = name;
        group.widget = widget;
        group.probeIds = probeIds;
        probeGroups_.push_back(group);
        
        addDockWidget(Qt::RightDockWidgetArea, dock);
    }
    
    void linkGroupedProbes() {
        for (auto& group : probeGroups_) {
            group.widget->clearAllProbes();
            
            for (const auto& id : group.probeIds) {
                if (auto probe = schemaRunner_->probe(id)) {
                    group.widget->attachProbe(
                        probe, 
                        QString::fromStdString(id)
                    );
                }
            }
        }
    }
};
```

### Pattern 4: Automatic Probe Creation for Outputs

Automatically attach probes to all schema outputs:

```cpp
void MainWindow::createProbesForOutputs() {
    // Iterate schema nodes and create probes for outputs
    for (const auto& [nodeId, node] : schemaRunner_->nodes()) {
        const auto& outputs = node->outputs();
        
        for (size_t i = 0; i < outputs.size(); ++i) {
            std::string probeId = nodeId + "_out_" + std::to_string(i);
            
            // Create probe in schema
            auto probe = schemaRunner_->createProbe(
                probeId,
                outputs[i].at(0).elementType,  // Get data type
                outputs[i].at(0).channels,     // Get channel count
                1024  // Buffer depth
            );
            
            // Attach to oscilloscope
            if (probe && oscilloscope_) {
                oscilloscope_->attachProbe(
                    probe,
                    QString::fromStdString(probeId)
                );
            }
        }
    }
}
```

---

## Connecting to File Menu

Add export/print functionality to menu:

```cpp
void MainWindow::setupFileMenu() {
    auto fileMenu = menuBar()->addMenu(tr("&File"));
    
    // ... existing items ...
    
    // Oscilloscope export
    fileMenu->addSeparator();
    auto exportOscAction = fileMenu->addAction(tr("Export Oscilloscope to PDF..."));
    connect(exportOscAction, &QAction::triggered,
            [this]() {
        if (!oscilloscope_) return;
        
        QString fileName = QFileDialog::getSaveFileName(
            this,
            tr("Export Oscilloscope"),
            "",
            tr("PDF Files (*.pdf)")
        );
        
        if (!fileName.isEmpty()) {
            if (oscilloscope_->exportPdf(fileName)) {
                QMessageBox::information(this, 
                    tr("Export Successful"),
                    tr("Oscilloscope exported to: %1").arg(fileName)
                );
            } else {
                QMessageBox::warning(this,
                    tr("Export Failed"),
                    tr("Could not export oscilloscope")
                );
            }
        }
    });
    
    auto printOscAction = fileMenu->addAction(tr("Print Oscilloscope..."));
    connect(printOscAction, &QAction::triggered,
            [this]() {
        if (oscilloscope_) {
            oscilloscope_->print(this);
        }
    });
}
```

---

## Status Bar Integration

Show hint about available probes:

```cpp
void MainWindow::setupStatusBar() {
    auto statusBar = this->statusBar();
    
    // ... existing status items ...
    
    // Probe status label
    auto probeLabel = new QLabel(this);
    statusBar->addPermanentWidget(probeLabel);
    
    // Update when simulation runs
    connect(this, &MainWindow::simulationStarted,
            [this, probeLabel]() {
        int probeCount = schemaRunner_->probeIds().size();
        probeLabel->setText(
            tr("Probes: %1 available").arg(probeCount)
        );
    });
}
```

---

## Settings Persistence

Save oscilloscope settings to project file:

```cpp
void MainWindow::saveProjectSettings(QJsonObject& settings) {
    if (oscilloscope_) {
        QJsonObject oscSettings;
        oscSettings["timeWindow"] = oscilloscope_->getTimeWindow();
        oscSettings["sampleRate"] = oscilloscope_->getSampleRate();
        oscSettings["dockVisible"] = oscilloscopeDock_->isVisible();
        settings["oscilloscope"] = oscSettings;
    }
}

void MainWindow::loadProjectSettings(const QJsonObject& settings) {
    auto oscSettings = settings["oscilloscope"].toObject();
    
    if (oscilloscope_ && !oscSettings.isEmpty()) {
        oscilloscope_->setTimeWindow(
            oscSettings["timeWindow"].toDouble(5.0)
        );
        oscilloscope_->setSampleRate(
            oscSettings["sampleRate"].toDouble(1000.0)
        );
        oscilloscopeDock_->setVisible(
            oscSettings["dockVisible"].toBool(true)
        );
    }
}
```

---

## Minimal Complete Example

```cpp
// main_window.hpp
class MainWindow : public QMainWindow {
    Q_OBJECT
    
private:
    OscilloscopeWidget* oscilloscope_ = nullptr;
    QDockWidget* oscilloscopeDock_ = nullptr;
    
    void setupOscilloscope();
    void linkOscilloscopeProbes();
};

// main_window.cpp
void MainWindow::setupOscilloscope() {
    oscilloscopeDock_ = new QDockWidget("Oscilloscope", this);
    oscilloscope_ = new OscilloscopeWidget();
    oscilloscopeDock_->setWidget(oscilloscope_);
    
    addDockWidget(Qt::RightDockWidgetArea, oscilloscopeDock_);
    
    auto viewMenu = menuBar()->addMenu("View");
    viewMenu->addAction(oscilloscopeDock_->toggleViewAction());
}

void MainWindow::onRunSchema() {
    schemaRunner_->run();
    linkOscilloscopeProbes();
}

void MainWindow::linkOscilloscopeProbes() {
    oscilloscope_->clearAllProbes();
    
    for (const auto& id : schemaRunner_->probeIds()) {
        if (auto probe = schemaRunner_->probe(id)) {
            oscilloscope_->attachProbe(
                probe, 
                QString::fromStdString(id)
            );
        }
    }
}
```

---

## Testing Integration

Quick manual test:

```cpp
void testOscilloscope() {
    // Create test probes
    auto probe1 = std::make_shared<s7::Probe>("test1", 2, 512);
    auto probe2 = std::make_shared<s7::Probe>("test2", 1, 512);
    
    // Create oscilloscope
    auto osc = new OscilloscopeWidget();
    
    // Attach probes
    osc->attachProbe(probe1, "Signal 1");
    osc->attachProbe(probe2, "Signal 2");
    
    // Verify
    assert(osc->probeCount() == 2);  // If method exists
    
    // Simulate data (in real app, compute thread does this)
    for (int i = 0; i < 512; ++i) {
        probe1->writeSample(0, sin(2 * M_PI * i / 512));
        probe1->writeSample(1, cos(2 * M_PI * i / 512));
        probe2->writeSample(0, i / 512.0);
    }
    
    // Redraw
    osc->redraw();
    
    // Show window
    osc->resize(800, 600);
    osc->show();
}
```

---

## Troubleshooting Integration

**Q: Oscilloscope shows blank after running**
- A: Check that `schemaRunner_->probeIds()` returns non-empty list
- A: Verify probes have `channelCount() > 0`
- A: Ensure compute thread is actually writing samples

**Q: Dock doesn't appear in View menu**
- A: Call `setupOscilloscope()` before `setupViewMenu()`
- A: Add dock before setting up menu: `addDockWidget()` first

**Q: Colors are too similar**
- A: Use only 1-2 probes per oscilloscope for best contrast
- A: Group related signals into separate oscilloscope instances

**Q: Memory leak warning**
- A: Oscilloscope is owned by dock, dock by MainWindow
- A: Qt's parent-child hierarchy handles cleanup automatically

---

## Related Files to Modify

1. **`s7main/include/main_window.hpp`**
   - Add member variables

2. **`s7main/src/main_window.cpp`**
   - Add `#include "oscilloscope_widget.hpp"`
   - Implement `setupOscilloscope()`
   - Implement `linkOscilloscopeProbes()`
   - Wire signals/slots

3. **`s7ui/CMakeLists.txt`** (likely already has oscilloscope_widget)
   - Verify `oscilloscope_widget.cpp` is listed in sources

4. **`s7main/CMakeLists.txt`**
   - Link `s7ui` library (probably already done)

---

## Next Steps

1. ✅ Review and approve this integration plan
2. ✅ Add oscilloscope widget to MainWindow
3. ✅ Test with real probes during simulation
4. ✅ Fine-tune time window / sample rate
5. ✅ Add user controls for probe selection
6. ✅ Add export/print to File menu
7. ✅ Document final UX for end users


