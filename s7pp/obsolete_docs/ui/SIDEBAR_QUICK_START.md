# UI Sidebar Redesign - Quick Reference

## What Changed?

The S7UI interface has been redesigned to **maximize the diagram working area** by replacing fixed dock panels with a **collapsible sidebar on the left**.

## Visual Layout

**Before:**
```
┌─────────────────────────────────────────────┐
│  Menu | Toolbar                             │
├──────────────────┬─────────────────────────┤
│ Palette (right)  │                         │
│ Remote (left)    │   Main Diagram Area     │
│ Properties (left)│   (Limited space)       │
├─────────────────┼─────────────────────────┤
│ Log (bottom)                                │
└─────────────────────────────────────────────┘
```

**After:**
```
┌────────────────────────────────────────────────┐
│ Menu | Toolbar                                 │
├─┬────────────────────────────────────────────┤
│ │ ⊕ ⚙ ⚡ 📋                                    │
│ │                                             │
│ │  [Panel content when selected]              │
│ │  or                                         │
│ │  [MAXIMIZED Diagram Area]                   │
│ │  [When panels are closed]                   │
│ │                                             │
└─┴────────────────────────────────────────────┘
```

## Sidebar Icons

| Icon | Name | Color | Function |
|------|------|-------|----------|
| **⊕** | Palette | Blue | Add nodes to diagram |
| **⚙** | Remote | Green | Manage remote models |
| **⚡** | Properties | Orange | Diagram settings (name, dt) |
| **📋** | Log | Cyan | Runtime logs & output |

## How to Use

### View a Panel
- **Click** any icon in the left sidebar to show that panel
- The icon becomes **highlighted in blue** when active

### Hide All Panels
- **Click** the active (highlighted) icon again, OR
- **Click anywhere on the diagram**

### Result
- Maximum editing space when panels are closed
- Quick one-click access to tools when needed
- Modern, clean interface

## Example Workflow

1. **Start**: Diagram area is maximized, sidebar shows 4 icons
2. **Add nodes**: Click ⊕ icon → Palette panel opens → Pick a node → Panel auto-closes on diagram interaction
3. **Set properties**: Click ⚡ icon → Properties panel opens → Edit name/dt → Done
4. **Monitor**: Click 📋 icon → Log panel opens → Watch execution → Click diagram to close

## Technical Details

### Files Modified
- `s7ui/src/main_window.hpp` - Added sidebar widget
- `s7ui/src/main_window.cpp` - Implemented sidebar layout
- `s7ui/CMakeLists.txt` - Added new files to build

### Files Created
- `s7ui/src/sidebar_widget.hpp` - Sidebar widget class
- `s7ui/src/sidebar_widget.cpp` - Sidebar implementation

### Key Features
- Icon-based navigation (no text labels)
- Automatic panel hiding on diagram interaction
- Persistent sidebar visibility
- Visual feedback (icons change color when active)
- Pictogram icons with Unicode symbols

## Building & Running

```bash
cd /home/al/projects/s7/s7pp/build
cmake ..
make -j4
./s7ui/s7ui
```

## Future Enhancements

- [ ] Smooth slide-in/out animations for panels
- [ ] Resizable panel width
- [ ] Keyboard shortcuts (Ctrl+1, Ctrl+2, etc.)
- [ ] Persistent panel state between sessions
- [ ] Draggable sidebar icons to reorder
- [ ] Custom icon theme support
- [ ] Bottom dock panel support

---

**Status**: ✅ Fully working and integrated  
**Build**: Successful  
**Executable**: `/home/al/projects/s7/s7pp/build/s7ui/s7ui`

