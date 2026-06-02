# UI Redesign: Maximized Working Area with Collapsible Sidebar

## Overview

The S7UI has been redesigned with a **modern collapsible sidebar** on the left that maximizes the central diagram editing area. This follows modern IDE patterns similar to VS Code, providing a clean and spacious working environment.

## Key Features

### 1. **Collapsible Left Sidebar**
- **Vertical icon bar** (48px wide) always visible on the left
- Contains pictograms (icons with Unicode symbols) for quick access to different panels
- Each icon has a tooltip explaining its function

### 2. **Icon-Based Navigation**
The sidebar contains four main service panels accessible via icons:

| Icon | Panel | Function | Tooltip |
|------|-------|----------|---------|
| ⊕ (Blue) | **Node Palette** | Add nodes to the diagram | Add nodes to diagram |
| ⚙ (Green) | **Remote Models** | Manage remote node connections | Manage remote nodes |
| ⚡ (Orange) | **Diagram Properties** | Set schema name and dt (sampling interval) | Name, dt, etc. |
| 📋 (Cyan) | **Runtime Log** | View model execution output | Model execution output |

### 3. **Smart Show/Hide Behavior**
- **Show**: Click an icon to expand the corresponding panel
- **Indicator**: The active icon is highlighted in blue (#0e639c)
- **Toggle**: Click the active icon again to hide all panels
- **Auto-hide**: Click anywhere on the diagram to collapse all panels
- **Maximize**: When a panel is hidden, the diagram gets maximum working space

### 4. **Visual Design**
- **Icon bar**: Dark theme (#2d2d2d) with subtle border
- **Panel area**: Light theme (#f5f5f5) for clear readability
- **Buttons**: Hover effect with background highlight
- **Icons**: Unicode pictograms with colored backgrounds for visual distinction

## Technical Implementation

### New Files Created

1. **`src/sidebar_widget.hpp`** - Sidebar widget header
2. **`src/sidebar_widget.cpp`** - Sidebar widget implementation

### Modified Files

1. **`src/main_window.hpp`**
   - Added `SidebarWidget* sidebar_` member
   - Renamed `createDocks()` to `createSidebar()`
   - Added `sidebar_widget.hpp` include

2. **`src/main_window.cpp`**
   - Replaced dock widget layout with sidebar + view horizontal layout
   - Implemented `createSidebar()` with four panel items
   - Added `ViewClickFilter` event filter to hide sidebar on diagram clicks
   - Improved icon creation with pictograms and colors

3. **`s7ui/CMakeLists.txt`**
   - Added `sidebar_widget.cpp` and `sidebar_widget.hpp` to build targets

## Usage Guide

### For Users

1. **Adding Nodes**: Click the ⊕ icon on the left sidebar to open the Node Palette, then double-click a node type to add it to the diagram.

2. **Remote Connection**: Click the ⚙ icon to open Remote Models and configure connections to remote S7 instances.

3. **Diagram Settings**: Click the ⚡ icon to modify diagram name and sampling interval (dt).

4. **Monitoring**: Click the 📋 icon to view runtime logs and model execution output.

5. **Maximum Working Space**: Click anywhere on the diagram to hide all panels and get maximum editing space.

### For Developers

#### Adding a New Sidebar Panel

```cpp
// Create the panel widget
auto* myPanel = new QWidget();
auto* layout = new QVBoxLayout(myPanel);
// ... add widgets to panel ...
myPanel->setLayout(layout);

// Add to sidebar
sidebar_->addItem({
    .id = "my_panel",
    .icon = makePictogramIcon("🔧", QColor("#color"), Qt::white),
    .tooltip = "My Panel Description",
    .panel = myPanel
});
```

#### Removing a Sidebar Panel

Simply don't call `sidebar_->addItem()` for that panel.

## Benefits

1. **Maximized Diagram Area**: More space for CAD-style diagram editing
2. **Modern UX**: Familiar sidebar pattern from VS Code and other modern IDEs
3. **Clean Interface**: Icon-only navigation when panels are hidden
4. **Visual Organization**: Color-coded icons aid quick visual identification
5. **Easy Access**: Each function one click away
6. **Responsive**: Panels automatically hide on diagram interaction

## Future Enhancements

Potential improvements for future iterations:

1. **Smooth Animations**: Add slide-in/slide-out animations for panels
2. **Resizable Panels**: Allow users to customize panel width
3. **Draggable Panels**: Allow users to reorder sidebar icons
4. **Keyboard Shortcuts**: Add keyboard shortcuts to toggle panels (e.g., Ctrl+1, Ctrl+2, etc.)
5. **Panel State Persistence**: Remember which panel was open between sessions
6. **Custom Icons**: Allow users to customize icon appearance
7. **Bottom Panels**: Add support for panels docked at the bottom (like debugger consoles)
8. **Search**: Add a quick search/command palette in the sidebar

## Architecture

### SidebarWidget Class

```cpp
class SidebarWidget : public QWidget {
    // Add items (icon + panel)
    void addItem(const SidebarItem& item);
    
    // Hide all panels
    void hideAll();
    
    // Get current active panel ID
    QString currentActiveId() const;
};
```

### SidebarItem Structure

```cpp
struct SidebarItem {
    QString id;            // Unique identifier
    QIcon icon;           // Icon to display
    QString tooltip;      // Hover tooltip
    QWidget* panel;       // Panel widget (owned by caller)
};
```

## Integration with Existing Features

- **Palette Tree**: Moved from right dock to left sidebar panel
- **Remote Nodes Widget**: Moved from left dock to sidebar panel
- **Schema Properties**: Moved from left dock to sidebar panel
- **Log View**: Moved from bottom dock to sidebar panel
- **Central View**: Now takes full width below toolbar, with sidebar on the side

All existing functionality is preserved; only the layout and presentation has changed.

