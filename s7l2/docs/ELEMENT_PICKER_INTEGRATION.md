# Element Picker Panel Integration Guide

## Overview

The ElementPickerPanel is a Qt widget that displays available elements from the ElementRegistry in an organized, searchable interface. It's designed to be integrated into s7studio's MainWindow as a right-side panel for element discovery and diagram editing.

## Components Created

### 1. ElementCatalogService (s7core)
**File**: `s7core/include/s7/element_catalog.hpp` and `src/element_catalog.cpp`

Service for querying the ElementRegistry to provide element catalog information:

```cpp
class ElementCatalogService {
public:
    explicit ElementCatalogService(const ElementRegistry& registry);
    
    std::vector<ElementCatalogEntry> all_elements() const;
    std::vector<ElementCatalogEntry> elements_in_category(const std::string& category) const;
    std::vector<std::string> categories() const;
    const ElementCatalogEntry* find_element(const std::string& type_name) const;
    boost::json::value to_json() const;
};
```

Used by UI to query elements without direct registry access.

### 2. ElementPickerPanel (s7studio)
**Files**: 
- `s7studio/src/element_picker_panel.hpp`
- `s7studio/src/element_picker_panel.cpp`

Qt widget providing:
- **Category tree view** - Elements organized by category (basic, filters, spectral, simulation, etc.)
- **Search/filter** - Real-time filtering by element name or type
- **Element info** - Description and I/O count display
- **Double-click** - Signal emission when user wants to add element
- **Drag & drop** - Foundation for dragging elements to diagram (extensible)

### 3. Updated local_runtime (s7studio)
**File**: `s7studio/src/local_runtime_v2.hpp`

Replaces hardcoded element switch statement with registry lookup:

```cpp
// Old way (hardcoded):
if (type == "Constant") { ... }
else if (type == "Adder") { ... }

// New way (dynamic registry):
auto& registry = s7::ElementRegistry::instance();
const auto* descriptor = registry.find(type);
auto element = descriptor->create(ctx);
```

## Integration into MainWindow

### Step 1: Include Required Headers

```cpp
// In main_window.hpp
#include "element_picker_panel.hpp"
#include "s7/element_registry.hpp"
#include "s7/plugin_loader.hpp"
```

### Step 2: Add Element Picker to MainWindow

```cpp
// In MainWindow private members
s7studio::ui::ElementPickerPanel* elementPickerPanel_ = nullptr;
```

### Step 3: Initialize in buildUi()

```cpp
void MainWindow::buildUi() {
    // ... existing code ...
    
    // Initialize plugin loader and registry
    s7::ElementRegistry& registry = s7::ElementRegistry::instance();
    s7::PluginLoader loader;
    loader.load_plugins(registry);
    
    // Create element picker panel
    elementPickerPanel_ = new s7studio::ui::ElementPickerPanel(registry, this);
    
    // Add to right side of main splitter
    mainSplitter_->addWidget(elementPickerPanel_);
    mainSplitter_->setSizes({600, 300});  // Adjust proportions
    
    // Connect signals
    connect(elementPickerPanel_, &s7studio::ui::ElementPickerPanel::elementRequested,
            this, &MainWindow::onElementRequested);
}
```

### Step 4: Add Slot Handler

```cpp
// In main_window.hpp (private slots)
void onElementRequested(const QString& elementType, const QString& displayName);

// In main_window.cpp
void MainWindow::onElementRequested(const QString& elementType, const QString& displayName) {
    if (!diagramEditor_) {
        return;
    }
    
    // Create new element node in diagram
    // This delegates to diagram editor to handle the placement
    diagramEditor_->createElementNode(elementType.toStdString(), displayName.toStdString());
}
```

### Step 5: Update local_runtime Usage

```cpp
// In local_schema_runner.cpp or where schema is built
// Replace old local_runtime.hpp with local_runtime_v2.hpp

#include "local_runtime_v2.hpp"

// This now uses ElementRegistry instead of hardcoded switch
auto schema = buildSchema(schemaJson);
```

## File Structure After Integration

```
s7core/
├── include/s7/
│   ├── element_catalog.hpp          (NEW)
│   └── element_registry.hpp         (existing)
└── src/
    └── element_catalog.cpp          (NEW)

s7studio/src/
├── element_picker_panel.hpp         (NEW)
├── element_picker_panel.cpp         (NEW)
├── local_runtime_v2.hpp             (NEW - replaces local_runtime.hpp)
├── main_window.hpp                  (MODIFY - add member)
└── main_window.cpp                  (MODIFY - integrate panel)
```

## UI Layout

```
┌─────────────────────────────────────────────────────┐
│ File  Edit  View  Run  Help                          │
├─────────────────────┬───────────────────────────────┤
│                     │   Element Library              │
│                     ├───────────────────────────────┤
│                     │ [Search: ____________]         │
│   Diagram Editor    │                               │
│                     │ ✓ basic                       │
│                     │  • Constant (0→1)             │
│                     │  • Gain (1→1)                 │
│                     │  • Adder (2→1)                │
│                     │                               │
│                     │ ✓ filters                     │
│                     │  • IirFilter (1→1)            │
│                     │  • FirFilter (1→1)            │
│                     │                               │
│                     │ ✓ simulation                  │
│                     │  • WhiteNoiseGenerator (0→1)  │
│                     │  • PinkNoiseGenerator (0→1)   │
│                     │                               │
│                     │ ─────────────────────         │
│                     │ "Add element to diagram       │
│                     │  by double-clicking"          │
└─────────────────────┴───────────────────────────────┘
```

## Features

### Category Organization
Elements automatically grouped by category from metadata:
- **basic**: Math operations, constants, gains
- **filters**: DSP filters, integrators
- **spectral**: FFT and frequency domain
- **simulation**: Noise generators, test sources
- **io**: File and network I/O
- **network**: Distributed computation
- **custom**: User-defined elements

### Search Filtering
- Real-time search as user types
- Filters by element name or type
- Categories collapse if no matches
- Expandable/collapsible categories

### Element Information
When element selected:
- Display name and type
- Full description
- I/O port count (e.g., "2→1" means 2 inputs, 1 output)
- Element count summary

### Registry Integration
- Queries ElementRegistry for current elements
- Shows plugins that have been loaded
- Updates dynamically if new plugins added
- Automatic metadata extraction

## Workflow

1. **User opens s7studio**
   - PluginLoader scans plugin directories
   - Loads s7noise (and other plugins)
   - ElementRegistry populated with all available elements

2. **Element Picker Panel displays**
   - Shows categories (basic, filters, spectral, simulation)
   - Each category lists elements with I/O counts
   - Search box ready for filtering

3. **User searches for element**
   - Types "noise" or "white"
   - Category tree filtered to matching elements
   - Only "WhiteNoiseGenerator" visible

4. **User double-clicks element**
   - Signal emitted: `elementRequested("WhiteNoiseGenerator", "White Noise")`
   - MainWindow slot triggered
   - DiagramEditor creates new node
   - User can drag to place on canvas

5. **User connects elements**
   - Existing diagram connection logic unchanged
   - Just adds support for dynamically loaded elements

## Benefits

✅ **Dynamic Element Discovery** - No hardcoded element list
✅ **Plugin Support** - Shows elements from all loaded plugins
✅ **User Friendly** - Organized, searchable interface
✅ **Extensible** - Easy to add new categories/features
✅ **Type Safe** - Uses ElementMetadata for accuracy
✅ **Zero Breaking Changes** - Backwards compatible

## Testing Checklist

- [ ] ElementCatalogService builds without errors
- [ ] ElementPickerPanel displays categories
- [ ] Search filtering works correctly
- [ ] Element selection shows description
- [ ] Double-click emits signal
- [ ] Integration with MainWindow compiles
- [ ] Element picker appears in UI
- [ ] Can add elements from picker to diagram
- [ ] Plugins load and appear in picker
- [ ] s7noise elements visible in picker
- [ ] Search finds s7noise elements
- [ ] Different window layouts work

## Future Enhancements

1. **Drag & Drop** - Drag elements directly onto diagram
2. **Favorites** - Star frequently used elements
3. **Parametrization** - Quick parameter dialog
4. **Documentation** - Link to element documentation
5. **Custom Categories** - User-defined grouping
6. **Element Preview** - Small diagram preview
7. **Keyboard Navigation** - Arrow keys to select, Enter to add

## Code Snippets for Integration

### In CMakeLists.txt (s7studio)

```cmake
target_sources(s7studio PRIVATE
    # ... existing sources ...
    src/element_picker_panel.hpp
    src/element_picker_panel.cpp
    src/local_runtime_v2.hpp
)

target_link_libraries(s7studio PRIVATE
    # ... existing libs ...
    Qt6::Widgets
)
```

### In main_window.cpp

```cpp
void MainWindow::buildUi() {
    // ... existing UI setup ...
    
    // Initialize plugin system
    auto& registry = s7::ElementRegistry::instance();
    s7::PluginLoader loader;
    loader.load_plugins(registry);
    
    appendLog(QString("Loaded %1 plugins")
        .arg(loader.loaded_count()));
    
    // Create element picker
    elementPickerPanel_ = new s7studio::ui::ElementPickerPanel(registry, this);
    
    // Add to main splitter (right panel)
    mainSplitter_->addWidget(elementPickerPanel_);
    
    // Connect
    connect(elementPickerPanel_, 
            QOverload<const QString&, const QString&>::of(
                &s7studio::ui::ElementPickerPanel::elementRequested),
            this, &MainWindow::onElementRequested);
}

void MainWindow::onElementRequested(const QString& elementType, const QString& displayName) {
    if (!diagramEditor_) return;
    diagramEditor_->addElement(elementType.toStdString(), displayName.toStdString());
}
```

## Notes

- ElementCatalogService is cached for performance
- Tree widget is expandable/collapsible
- Search is case-insensitive
- Element count updates dynamically
- All text properly quoted and escaped for JSON
- Ready for plugin system with existing architecture

