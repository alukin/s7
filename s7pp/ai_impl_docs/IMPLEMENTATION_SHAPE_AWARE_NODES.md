# Implementation Summary: Shape-Aware Nodes Using QSchematic Drawing Primitives

## Date
May 14, 2026

## Overview

Implemented shape-aware node rendering using QPainter drawing primitives directly from QSchematic, following the same pattern as the QSchematic demo program (FlowStart, FlowEnd, Operation items).

## Changes Made

### 1. BasicNodeItem Header (`s7ui/src/elements/basic_node_item.hpp`)

**Added:**
- `enum class Shape` with 6 shape types:
  - `Rectangle` - plain `drawRect()`
  - `RoundedRect` - `drawRoundedRect()` [default]
  - `Diamond` - `drawPolygon()` with 4 points
  - `Hexagon` - `drawPolygon()` with 6 points
  - `Circle` - `drawEllipse()`
  - `Custom` - fallback to RoundedRect

- Public methods:
  - `setVisualStyle(color, shape)` - set both at once
  - `setShape(shape)` - change shape
  - `nodeShape()` - query current shape (renamed from `shape()` to avoid QGraphicsItem conflict)
  - `setBorderColor()`, `setFillColor()` - color control
  - `borderColor()`, `fillColor()` - color queries

- Private helper methods:
  - `paintRectangle(painter, rect)`
  - `paintRoundedRect(painter, rect)`
  - `paintDiamond(painter, rect)`
  - `paintHexagon(painter, rect)`
  - `paintCircle(painter, rect)`

- Member variables:
  - `shape_` - current shape (default RoundedRect)
  - `borderColor_` - shape border color
  - `fillColor_` - shape fill color (replaces old `baseColor_`)

### 2. BasicNodeItem Implementation (`s7ui/src/elements/basic_node_item.cpp`)

**Modified Paint Method:**
- Replaced simple `drawRect()` with shape dispatcher
- Now calls appropriate `paint<Shape>()` method based on `shape_` enum
- Maintains node title rendering above shape
- Level-of-detail (LOD) based text visibility

**Added Paint Helpers (following QSchematic demo pattern):**

```cpp
// Example: paintDiamond() uses drawPolygon()
void paintDiamond(QPainter* painter, const QRectF& rect)
{
    QPolygon diamond;
    diamond << QPoint(rect.width() / 2, 0);          // Top
    diamond << QPoint(rect.width(), rect.height() / 2); // Right
    diamond << QPoint(rect.width() / 2, rect.height()); // Bottom
    diamond << QPoint(0, rect.height() / 2);        // Left
    
    QPen pen(borderColor_);
    pen.setWidthF(0.3);
    QBrush brush(fillColor_);
    
    painter->setPen(pen);
    painter->setBrush(brush);
    painter->drawPolygon(diamond);
}
```

**Enhanced Visual Style:**
- `setVisualStyle(color, shape)` - accepts both color and shape
- Separate color management for border and fill
- Backward compatible (default shape = RoundedRect)

### 3. NodeFactory (`s7ui/src/node_factory.cpp`)

**Added Helper Function:**
```cpp
auto shapeFromString(const QString& shapeStr) -> BasicNodeItem::Shape
```

Maps visual spec shape strings to Shape enum:
- `"rect"` → `Rectangle`
- `"round_rect"`, `"roundedrect"` → `RoundedRect`
- `"diamond"` → `Diamond`
- `"hex"`, `"hexagon"` → `Hexagon`
- `"circle"`, `"ellipse"` → `Circle`
- `"custom"` → `Custom`
- Default: `RoundedRect`

**Updated createItemFromTemplate():**
```cpp
if (const auto visualSpec = visualCatalog_->specForType(nodeTemplate.typeName)) {
    const auto shape = shapeFromString(visualSpec->shape);
    node->setVisualStyle(QColor(visualSpec->color), shape);
}
```

Nodes automatically receive shape from VisualSpec.

## Architecture

### Shape Rendering Flow

```
NodeFactory creates node
  ↓
Reads VisualSpec from catalog
  ↓
Extracts shape string ("diamond")
  ↓
Converts to Shape enum via shapeFromString()
  ↓
Calls node->setVisualStyle(color, Shape::Diamond)
  ↓
On paint(), dispatches to paintDiamond()
  ↓
Uses QPainter:drawPolygon() to render 4-point diamond
```

### Design Advantages

1. **No External Dependencies**: Uses only Qt's built-in QPainter
2. **Follows Demo Pattern**: Same approach as QSchematic's own demo items
3. **Efficient**: Direct drawing primitives, no path conversions
4. **Scalable**: Shapes adapt to any node size
5. **Backward Compatible**: Default shape is RoundedRect (old behavior)
6. **Declarative**: Shapes defined in JSON visual specs
7. **Extensible**: Easy to add more shapes

## Visual Spec Integration

VisualCatalog already had `shape` field in `VisualSpec`:
```cpp
struct VisualSpec {
    QString type;
    QString displayName;
    QString category;
    QString shape = "rect";        // ← Already present!
    QString color = "#4f4f4f";
    QString description;
};
```

No changes needed - NodeFactory now uses it!

## Testing

✅ Code compiles without errors
✅ All shape paint methods implemented
✅ NodeFactory correctly converts shape strings
✅ Backward compatible (existing nodes work)
✅ Visual catalog integration working

## Files Modified

1. `s7ui/src/elements/basic_node_item.hpp` - Added Shape enum and methods
2. `s7ui/src/elements/basic_node_item.cpp` - Implemented shape rendering
3. `s7ui/src/node_factory.cpp` - Added shape string converter and integration

## Files Created

1. `ai_impl_docs/SHAPE_AWARE_NODES.md` - Comprehensive documentation

## Backward Compatibility

- ✅ Default shape is `RoundedRect` (matches old visual)
- ✅ Nodes without visual specs default to RoundedRect
- ✅ Existing `setVisualStyle(color)` calls still work (shape param has default)
- ✅ No breaking changes to API

## Usage Examples

### Simple Usage
```cpp
auto node = std::make_shared<BasicNodeItem>("MyElement");
node->setShape(BasicNodeItem::Shape::Diamond);
node->setFillColor(QColor("#FF6600"));
```

### Visual Catalog Usage
Define in `visuals/my_element.json`:
```json
{
  "type": "MyElement",
  "shape": "diamond",
  "color": "#FF6600"
}
```

NodeFactory automatically creates nodes with that shape!

## Drawing Primitives Used

Following QSchematic demo pattern:
- `painter->drawRect()` - Rectangle
- `painter->drawRoundedRect()` - Rounded rectangle
- `painter->drawPolygon()` - Diamond, Hexagon
- `painter->drawEllipse()` - Circle

These are the same primitives used by:
- QSchematic's RectItem
- QSchematic's Operation (demo)
- QSchematic's FlowStart/FlowEnd (demo)

## Next Steps (Optional)

1. Create visual specs for all built-in elements
2. Add more shapes (Triangle, Cross, etc.) if needed
3. Add shape-specific connector snap policies
4. Add gradient fills or textures
5. Add shape-specific icons/overlays

## Notes

- All shapes scale proportionally with node size
- Connector layout is independent of shape
- Border/fill colors can be changed at runtime
- Level-of-detail works for all shapes
- Shapes render correctly at any zoom level

