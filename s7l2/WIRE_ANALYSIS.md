# QSchematic Demo Analysis & s7studio Diagram Editor Usability

## Executive Summary

The QSchematic demo is a fully-featured schematic editor with robust wire drawing, item management, and serialization. Our `s7studio` diagram editor is a **simplified integration** that works but lacks several critical features for usability. This analysis identifies the gaps and provides implementation guidance.

---

## 1. QSchematic Demo Architecture

### 1.1 Core Classes

| Class | Role | Key Feature |
|-------|------|-------------|
| **Scene** | Diagram container, mode manager | Owns undo stack, emit mode/netlist signals |
| **View** | Graphics viewport | Handles zoom, pan, rendering |
| **Wire** | Connection line | Points (relative), snap points, serializable |
| **Connector** | Connection port on node | Snap policies, connection detection |
| **Node** | Diagram element (Operation, etc.) | Custom rendering, context menu |
| **Item** | Base for all diagram objects | GPDS serialization, undo/redo support |

### 1.2 Interaction Modes

**Normal Mode:**
- Select, move, delete items
- Context menu on right-click
- No wire drawing

**Wire Mode:**
- Left-click on connector → start wire
- Left-click on canvas → add wire point
- Left-click on connector → end wire
- Escape → cancel wire
- Scene handles all wire creation via undo stack

### 1.3 Wire Factory Pattern

```cpp
_scene->setWireFactory([]{ return std::make_shared<FancyWire>(); });
```

Allows custom wire subclasses (visual styling, behavior) while Scene owns creation logic.

### 1.4 Undo/Redo Integration

- Scene owns `QUndoStack`
- All item add/remove creates `ItemAdd`/`ItemRemove` commands
- Commands pushed to stack before execution
- Library widget signals item click → app decides to add via `ItemAdd` command
- Undo/redo actions created from stack: `_scene->undoStack()->createUndoAction(...)`

### 1.5 Serialization Pipeline

**Custom Item Factory (deserialization only):**
```cpp
auto func = std::bind(&CustomItemFactory::from_container, std::placeholders::_1);
QSchematic::Items::Factory::instance().setCustomItemsFactory(func);
```

**Scene serialization:**
```cpp
const auto& [success, message] = gpds::to_file<gpds::archiver_xml>(path, *_scene);
```

Each Item implements:
- `to_container()` → serialize to GPDS container
- `from_container()` → deserialize from GPDS container
- `deepCopy()` → for undo/redo cloning

---

## 2. Current s7studio Integration

### 2.1 What We're Doing Right

✅ Basic scene/view setup
✅ Page layout model (mm-based, fixed-size)
✅ Zoom/pan controls
✅ Ruler widgets (new)
✅ Scope visualization as diagram elements
✅ Runtime schema → diagram deserialization
✅ Mode awareness (using QSchematic view)

### 2.2 Critical Gaps

❌ **No wire drawing mode** - Scene is stuck in Normal mode, user cannot create wires interactively
❌ **No node creation UI** - No way to add new diagram elements to a schema
❌ **No undo/redo** - Scene undo stack exists but we don't use it
❌ **No item context menu** - No right-click actions on nodes/wires/connectors
❌ **Limited serialization** - We use JSON (not GPDS), no custom factory
❌ **Hardcoded node creation** - Elements come only from runtime schema deserialize, not user action
❌ **No connector visualization** - Nodes exist but their ports/connectors aren't visible
❌ **No wire validation** - Connecting wires to random scene points, not connectors
❌ **No netlist generation** - Scene tracks connections but we don't use it

---

## 3. Wire Drawing Interaction (Key Missing Feature)

### 3.1 Demo Wire Mode Flow

1. **User clicks on connector** (in Wire mode)
   - Scene detects `mousePressEvent` on connector
   - Calls `finishCurrentWire()` to complete any prior wire
   - Creates new wire via `make_wire()`
   - Starts building wire points

2. **User clicks on canvas** (in Wire mode)
   - Scene appends wire point at click position
   - Wire is drawn on screen in real-time

3. **User clicks on target connector** (in Wire mode)
   - Wire snaps to connector position
   - Scene calls `finishCurrentWire()` → updates netlist
   - Wire becomes part of scene via undo stack

4. **User presses Escape** (in Wire mode)
   - Wire mode calls `removeLastWirePoint()` or clears `_newWire`
   - User returns to adding points or cancels entirely

### 3.2 Why This Matters for s7studio

Our diagram nodes (synthesized from schema) have no visible connectors. The user cannot:
- See where wires can connect
- Draw connections interactively
- Modify schema structure visually

**Solution path:**
1. Make connectors visible on nodes (render small circles/rectangles at port positions)
2. Add wire mode toggle to s7studio (View menu or toolbar)
3. Wire mode entry point: connect scene mode signal to UI actions
4. Scene already handles the rest

---

## 4. Node Creation & Library (Secondary Feature)

### 4.1 Demo Library Pattern

- **Library Widget** (dock on left) displays item catalog
- User clicks item in library → emits `itemClicked(item)` signal
- Main window slot receives signal → creates `ItemAdd` command
- Command pushed to stack → undoable add

### 4.2 For s7studio

We could support two workflows:

**Workflow A: Visual node creation (future)**
- Toolbar palette of element types
- Click palette item → cursor changes
- Click canvas → place node at position
- Uses `ItemAdd` command for undo

**Workflow B: Edit schema, regenerate** (current)
- Edit runtime schema JSON
- Reload → deserialization creates nodes
- No undo (schema not undoable yet)

**Recommendation:** Start with Workflow B (no new code), enable Workflow A later.

---

## 5. Connector Visibility & Snap Behavior

### 5.1 Demo Approach

Each Node has Connectors as children:
```cpp
for (int i = 0; i < inputCount; ++i) {
    auto connector = std::make_shared<Connector>();
    connector->setPos(0.0, portOffsetY(...));
    node->addConnector(connector);  // Sets parent item
}
```

Connector paints itself at its local position, snap policy determines where wire can connect.

### 5.2 For s7studio

**Our nodes:** Currently just rectangles with text labels.
**Connectors:** Exist on nodes but don't render.

**Implementation:**
1. In `SceneSerializer::deserialize()`, where we create nodes:
   ```cpp
   for (int i = 0; i < inputPortCount; ++i) {
       auto connector = std::make_shared<Connector>();
       connector->setPos(0.0, portOffsetY(...));
       node->addConnector(connector);
   }
   ```
2. Set snap policy (e.g., `NodeSizerectOutline`) so wires snap to node boundary
3. Connectors paint automatically once added to node

This requires no new UI, just populating connectors during deserialization.

---

## 6. Undo/Redo Integration

### 6.1 Demo Pattern

Every user action that changes the scene is a command:

```cpp
// User adds item from library
auto itemAddCmd = std::make_unique<Commands::ItemAdd>(_scene, newItem);
_scene->undoStack()->push(itemAddCmd.release());

// User deletes item
auto itemRemoveCmd = std::make_unique<Commands::ItemRemove>(_scene, item);
_scene->undoStack()->push(itemRemoveCmd.release());
```

### 6.2 For s7studio

We create a scene and have `_scene->undoStack()` available, but never use it.

**Quick win:**
- Connect scene undo/redo actions to File menu (or existing View menu)
- Demo does this in `createActions()`:
  ```cpp
  _actionUndo = _scene->undoStack()->createUndoAction(this, "Undo");
  _actionRedo = _scene->undoStack()->createRedoAction(this, "Redo");
  ```
- Add keyboard shortcuts (Ctrl+Z / Ctrl+Y)

**Caveat:** Our schema JSON isn't undoable yet. Undo/redo only works on visual diagram layout (node positions, wires), not the runtime schema itself.

---

## 7. Mode Switching & Keyboard/Toolbar Integration

### 7.1 Demo Mode Switching

```cpp
// Mode actions
_actionModeNormal = new QAction("Normal Mode", this);
_actionModeNormal->setCheckable(true);
_actionModeNormal->setChecked(true);
connect(_actionModeNormal, &QAction::triggered, [this]{ 
    _scene->setMode(QSchematic::Scene::NormalMode); 
});

_actionModeWire = new QAction("Wire Mode", this);
_actionModeWire->setCheckable(true);
connect(_actionModeWire, &QAction::triggered, [this]{ 
    _scene->setMode(QSchematic::Scene::WireMode); 
});

// Group them
QActionGroup* modeGroup = new QActionGroup(this);
modeGroup->addAction(_actionModeNormal);
modeGroup->addAction(_actionModeWire);
```

Scene emits `modeChanged(int)` → actions update checked state.

### 7.2 For s7studio

**Current state:** View menu exists, but no mode actions.

**Implementation:**
1. In `MainWindow::buildActions()`:
   ```cpp
   modeNormalAction_ = new QAction("Normal Mode", this);
   modeNormalAction_->setCheckable(true);
   modeNormalAction_->setChecked(true);
   connect(modeNormalAction_, &QAction::triggered, [this]{
       diagramEditor_->scene()->setMode(QSchematic::Scene::NormalMode);
   });
   
   modeWireAction_ = new QAction("Wire Mode", this);
   modeWireAction_->setCheckable(true);
   connect(modeWireAction_, &QAction::triggered, [this]{
       diagramEditor_->scene()->setMode(QSchematic::Scene::WireMode);
   });
   ```

2. In `DiagramEditor` constructor, connect scene mode signal to a slot that updates actions.

3. Add to View menu or Diagram menu.

---

## 8. Serialization Strategy: JSON vs GPDS

### 8.1 Demo Uses GPDS

- XML-based serialization framework
- Extensible via `to_container()`/`from_container()` protocol
- Type information encoded in XML
- Deep copy support for undo/redo

### 8.2 We Use JSON + GPDS Mismatch

- Runtime schema is JSON (s7 runtime expects it)
- Diagram layout is also JSON (DiagramDocument)
- Items deserialize via custom factory that expects GPDS-like structure
- No deep copy support → undo/redo doesn't work for visual edits

### 8.3 Recommendation

**Do NOT switch to GPDS.** Instead:

1. Keep JSON for runtime schema (non-negotiable, s7 core needs it)
2. Keep JSON for diagram layout (page size, node positions, wire routes)
3. When deserializing, `new Node()` doesn't parse JSON—use programmatic API:
   ```cpp
   auto node = std::make_shared<QSchematic::Items::Node>();
   node->setPos(nodeData.x, nodeData.y);
   node->setSize(nodeData.width, nodeData.height);
   // ... connectors, labels, etc.
   ```
4. When serializing, walk the scene and extract to JSON:
   ```cpp
   for (const auto& node : scene->nodes()) {
       json nodeJson;
       nodeJson["id"] = node->data(kNodeIdRole).toString();
       nodeJson["x"] = node->pos().x();
       nodeJson["y"] = node->pos().y();
       // ... etc.
   }
   ```

This keeps JSON throughout but avoids GPDS complexity. Scene undo/redo still works for visual edits.

---

## 9. Implementation Priority for Usability

### Phase 1 (Core Wire Drawing)
1. **Connector rendering** (5 min change)
   - In `SceneSerializer::deserialize()`, create connectors for each port
   
2. **Wire mode toggle** (10 min)
   - Add "Wire Mode" / "Normal Mode" actions to View or Diagram menu
   - Connect to `scene->setMode()`

3. **Test interactive wire drawing**
   - Open diagram in GUI
   - Toggle Wire Mode
   - Click connector → draw wire → click target connector
   - Verify wire appears

**Impact:** Diagram becomes interactive. User can see connections and add new wires without code changes.

### Phase 2 (Node Creation UI)
1. Element palette in toolbar or right panel
2. Click palette item → `ItemAdd` command
3. New node appears in diagram

**Impact:** User can modify schema visually.

### Phase 3 (Full Undo/Redo & Serialization)
1. Undo/redo actions for layout edits
2. JSON export of modified diagram
3. Import modified diagram → regenerate runtime schema

**Impact:** Complete diagram editor workflow.

---

## 10. Quick Reference: Key QSchematic API

```cpp
// Scene mode
scene->setMode(QSchematic::Scene::NormalMode);
scene->setMode(QSchematic::Scene::WireMode);
connect(scene, &Scene::modeChanged, ...);

// Wire factory
scene->setWireFactory([] { return std::make_shared<CustomWire>(); });

// Item creation (undo-aware)
auto cmd = std::make_unique<QSchematic::Commands::ItemAdd>(scene, item);
scene->undoStack()->push(cmd.release());

// Item queries
auto nodes = scene->nodes();
auto wires = scene->items<QSchematic::Items::Wire>();
auto selected = scene->selectedTopLevelItems();

// Undo/redo
scene->undoStack()->createUndoAction(parent, "Undo");
scene->undoStack()->createRedoAction(parent, "Redo");

// Serialization (in from_container callback)
auto type = QSchematic::Items::Factory::extractType(container);
auto item = std::make_shared<CustomItem>();
item->from_container(container);

// Netlist generation
QSchematic::Netlist<NodeT, ConnectorT> netlist;
QSchematic::NetlistGenerator::generate(netlist, *scene);
```

---

## 11. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|-----------|
| Wire drawing breaks existing diagrams | Medium | Add mode toggle (disabled by default) |
| Connectors paint incorrectly | Medium | Test with demo first |
| Undo/redo doesn't cover schema changes | Low | Document as layout-only undo |
| Serialization mismatch (JSON vs GPDS) | Low | Keep JSON throughout, add JSON→Item factory |
| Performance with large diagrams | Low | Use existing scene/view optimizations |

---

## 12. Next Steps

1. **Read this analysis thoroughly** to understand the architecture
2. **Run the QSchematic demo** (`s7l2/external/QSchematic/demo/main`) to see features in action
3. **Implement Phase 1** (connectors + wire mode toggle)
4. **Test interactively** in s7studio GUI
5. **Iterate based on usability feedback**


