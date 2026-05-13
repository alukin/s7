# S7UI Editor Guide

## Overview

The S7UI editor is now fully functional with interactive wire drawing, node editing, and deletion capabilities.

## Key Features Implemented

### 1. **Wire Mode (Wire Drawing)**
The editor now supports interactive wire drawing between node connectors.

**How to use:**
- Press **Ctrl+W** to toggle Wire Mode
- Or select **Edit → Wire Mode** from the menu
- Or click the **Wire Mode** button in the toolbar
- When Wire Mode is active:
  - Click and drag from any output connector (right side of a node)
  - Drag to an input connector (left side of a node) to create a connection
  - The wire will automatically route around obstacles
  - You can shape wires by dragging waypoints

**Visual Feedback:**
- The "Wire Mode" action will be highlighted/checked when enabled
- The log panel will show "[editor] Wire mode enabled/disabled" messages

### 2. **Node Addition**
You can add new nodes to the schema in multiple ways:

- **Double-click** on a node type in the Node Palette (left dock)
- **Select a node type** in the palette and click "Add Selected Node"
- **Select a node type** and press the "Add Node" button in the toolbar

### 3. **Delete Selected Items**
Remove nodes, wires, or other items easily:

- **Select** items by clicking on them (or Ctrl+click for multi-select)
- Press **Delete** or **Backspace** key
- Selected items will be removed and a confirmation message appears in the log

### 4. **Node Editing**
Edit node properties:

- **Double-click** any node to open the properties dialog
- Edit:
  - **Name**: Display name for the node
  - **ID**: Unique identifier used in schemas
  - **Type**: Read-only field showing the node type
  - **Parameters**: JSON configuration for the node (validate on OK)

### 5. **Schema Management**

#### Save/Load Schemas
- **File → Open Schema** (Ctrl+O): Load an existing schema from JSON
- **File → Save Schema** (Ctrl+S): Save the current schema
- **File → Save Schema As** (Ctrl+Shift+S): Save with a new name

#### Schema Properties
Edit schema metadata in the "Schema Properties" dock:
- **Name**: Schema identifier
- **Sampling Interval (dt)**: Time step for simulation (in seconds)

### 6. **Model Execution**

The Model toolbar provides runtime controls:

- **Run**: Start simulation with specified duration
  - Set hours, minutes, seconds for run duration
  - Current state: idle, running, paused, or error
- **Pause**: Pause a running simulation
- **Resume**: Continue from pause
- **Stop**: Terminate the current run
- **Reset**: Clear session state

### 7. **Visual Elements (Sinks)**

The editor supports visual sink nodes for displaying simulation outputs:

- **ScopeSink**: Dual-channel oscilloscope visualization (2 inputs)
- **ComplexScopeSink**: Complex number visualization (real/imaginary)

Double-click sink nodes to:
- See a larger, detailed graph
- Adjust input channel count
- View live data during simulation

## Keyboard Shortcuts

| Action | Shortcut |
|--------|----------|
| Open Schema | Ctrl+O |
| Save Schema | Ctrl+S |
| Save As | Ctrl+Shift+S |
| Toggle Wire Mode | Ctrl+W |
| Delete Selected | Delete or Backspace |

## Menu Structure

### File
- Open Schema
- Save Schema
- Save Schema As

### Edit
- Wire Mode (Ctrl+W)

### Insert
- Add Node

### View
- Fit View (zoom to show all items)

### Model
- Run
- Pause
- Resume
- Stop
- Reset

## Workflow Example

1. **Build a schema:**
   - Add nodes from the palette (double-click or use "Add Node" button)
   - Arrange them on the canvas
   - Enter wire mode (Ctrl+W)
   - Draw connections between nodes
   - Double-click nodes to set parameters

2. **Configure and save:**
   - Edit schema name and dt in the properties dock
   - Save the schema (Ctrl+S)

3. **Run simulation:**
   - Set desired run duration in the toolbar spinners
   - Click "Run" to start
   - Monitor progress in the model status label
   - View outputs in visual sinks (double-click to open detail view)

4. **Debug and iterate:**
   - Pause/Resume to control execution
   - Stop to terminate
   - Reset to clear state
   - Make schema changes and re-run

## Node Types

### Core Nodes
- **Constant**: Outputs a fixed value
- **Ramp**: Linear ramp generator
- **Integrator**: Numerical integrator
- **Add**: Summation node
- **Multiply**: Multiplication node
- And more...

### File I/O Nodes
- **FileSource**: Read data from files
- **FileSink**: Write data to files

### Network Nodes
- **NetworkSource**: Receive data from network
- **NetworkSink**: Send data over network

### Memory Nodes
- **MemSource**: Access in-memory data
- **MemSink**: Store to in-memory buffers

### Visual Nodes
- **ScopeSink**: Real-time dual-channel display
- **ComplexScopeSink**: Complex number visualization

## Log Panel

The **Runtime Log** (bottom dock) shows all editor and simulation events:
- Schema loading/saving
- Node addition/deletion
- Wire mode changes
- Model execution status
- Probe linkage information
- Any errors or warnings

## Tips

1. **Organize your schema:**
   - Use descriptive node names and IDs
   - Arrange nodes spatially by function
   - Group related connections

2. **Use the palette:**
   - Expand categories to see available nodes
   - Hover over nodes for descriptions
   - Double-click to add nodes directly

3. **Wire management:**
   - Use Fit View (toolbar button) to see the entire schema
   - You can drag waypoints on wires to reshape them (in normal mode)
   - Delete unused wires with the Delete key

4. **Parameters:**
   - JSON parameters are validated on dialog close
   - Check the log for validation errors
   - Different node types expect different parameters

## Troubleshooting

**"Wire mode enabled - click and drag from connectors"**
- This message means wire mode is active. Click on an output connector and drag to an input connector.

**Cannot add nodes?**
- Make sure a node type is selected in the palette
- Check that element libraries are registered (check log)

**Simulation errors?**
- Check the Runtime Log for detailed error messages
- Verify all required connectors have wires
- Inspect parameters (double-click node) for validity

**Wires not connecting?**
- Ensure you're dragging from output (right side) to input (left side)
- White circular markers show connector endpoints
- Try zooming in with the mouse wheel for precision

## 5. Static and Dynamic Probe Insertion

The S7 UI provides powerful inspection capabilities through the use of `Probe` elements. Probes are special, non-invasive sink elements that capture signal data, allowing visualization tools like the Oscilloscope to display what's happening inside the simulation without altering the results.

### 5.1 Static Insertion

You can add probes to your diagram just like any other element while you are in **Edit Mode**.

1.  Drag a `Probe` or `ComplexProbe` from the element palette onto the canvas.
2.  Wire its inputs to the signals you wish to monitor.
3.  When the simulation runs, this probe will capture data from the start.

This is the standard way to set up monitoring for signals you know you want to observe ahead of time.

### 5.2 Dynamic (Hot-Patch) Insertion

One of the most powerful features of the S7 UI is the ability to **dynamically insert probes into a live, running simulation**. This allows you to debug unexpected behavior or explore signals without having to stop and restart your entire computation.

The process is as follows:

1.  **Pause the Simulation**: While the schema is running, click the "Pause" button in the toolbar. The simulation will halt at the end of the current tick.
2.  **Insert a Probe**: Even though the simulation is paused, you can enter a temporary editing mode. Add a `Probe` to the diagram and connect it to the signal you want to inspect.
3.  **Resume the Simulation**: Click the "Resume" button.

The simulation will continue from where it left off, and the newly added probe will immediately begin capturing data. You can then connect an oscilloscope or other analysis tool to this new probe to view the live signal. This "hot-patching" capability is essential for interactive debugging and analysis.

## 6. Next Steps

Once the diagram is complete, it can be compiled into a runtime schema and executed.
