# Shape-Aware Nodes in S7UI

## Overview

The `BasicNodeItem` class now supports multiple node shapes using QPainter drawing primitives, following the same pattern as QSchematic's demo items. This allows elements to be rendered in various geometric shapes while leveraging Qt's efficient graphics rendering.

## Architecture

### Shape Types

```cpp
enum class Shape {
    Rectangle,      // Plain rectangle: drawRect()
    RoundedRect,    // Rounded corners: drawRoundedRect() [default]
    Diamond,        // 4-point polygon: drawPolygon()
    Hexagon,        // 6-point polygon: drawPolygon()
    Circle,         // Ellipse: drawEllipse()
    Custom          // Fallback to RoundedRect
};
```

These match the S7UI_DIAGRAM_SPEC_V1.md shapes: `rect`, `round_rect`, `diamond`, `hex`, `custom`.

### Drawing Implementation

Each shape has a dedicated paint helper method using QPainter primitives:

- **Rectangle**: `painter->drawRect(rect)`
- **RoundedRect**: `painter->drawRoundedRect(rect, radius, radius)`
- **Diamond**: `painter->drawPolygon(diamond)` with 4 corner points
- **Hexagon**: `painter->drawPolygon(hexagon)` with 6 side points
- **Circle**: `painter->drawEllipse(rect)`

This approach mirrors QSchematic's demo items (FlowStart, FlowEnd, Operation) which use the same primitives.

## API Usage

### Basic Shape Configuration

```cpp
// Create a node
auto node = std::make_shared<BasicNodeItem>(QString("MyElement"));

// Set shape and color together
node->setVisualStyle(
    QColor("#3366CC"),                           // Fill color
    BasicNodeItem::Shape::Diamond                // Shape
);

// Or set them separately
node->setShape(BasicNodeItem::Shape::Hexagon);
node->setFillColor(QColor(Qt::blue));
node->setBorderColor(QColor(Qt::darkBlue));
```

### Colors

```cpp
// Set colors
node->setFillColor(QColor("#FF5533"));
node->setBorderColor(QColor(Qt::black));

// Query colors
QColor fillColor = node->fillColor();
QColor borderColor = node->borderColor();
```

### Shape Queries

```cpp
// Get current shape
BasicNodeItem::Shape current = node->nodeShape();

if (current == BasicNodeItem::Shape::Diamond) {
    // Handle diamond-specific logic
}
```

## Visual Catalog Integration

The node factory automatically applies shapes from the visual catalog when creating nodes:

### Visual Spec JSON

```json
{
  "type": "MyCustomElement",
  "displayName": "My Custom Element",
  "category": "Processing",
  "shape": "diamond",
  "color": "#FF6600",
  "description": "A diamond-shaped processing element"
}
```

Valid shape values:
- `"rect"` → `Shape::Rectangle`
- `"round_rect"` / `"roundedrect"` → `Shape::RoundedRect` [default]
- `"diamond"` → `Shape::Diamond`
- `"hex"` / `"hexagon"` → `Shape::Hexagon`
- `"circle"` / `"ellipse"` → `Shape::Circle`
- `"custom"` → `Shape::Custom` (fallback)

### NodeFactory

When the NodeFactory creates a node, it automatically:
1. Reads the visual spec for the element type
2. Extracts the shape string
3. Converts it to a `Shape` enum using `shapeFromString()`
4. Applies both color and shape to the node

```cpp
// In NodeFactory::createItemFromTemplate()
if (const auto visualSpec = visualCatalog_->specForType(nodeTemplate.typeName)) {
    const auto shape = shapeFromString(visualSpec->shape);
    node->setVisualStyle(QColor(visualSpec->color), shape);
}
```

## Implementation Details

### Paint Method Flow

The `paint()` method dispatches to shape-specific handlers:

```cpp
void BasicNodeItem::paint(QPainter* painter, ...)
{
    switch (shape_) {
        case Shape::Rectangle:
            paintRectangle(painter, rect);
            break;
        case Shape::RoundedRect:
            paintRoundedRect(painter, rect);
            break;
        case Shape::Diamond:
            paintDiamond(painter, rect);
            break;
        case Shape::Hexagon:
            paintHexagon(painter, rect);
            break;
        case Shape::Circle:
            paintCircle(painter, rect);
            break;
        // ... etc
    }
    
    // Draw node title (common to all shapes)
    paintNodeTitle(painter, rect, nodeName_);
}
```

### Shape Geometry

Shapes use relative coordinates within `QRectF(0, 0, width, height)`:

**Diamond:**
```
      (w/2, 0) ← top
      /      \
  (0, h/2)  (w, h/2) ← left, right
     \        /
     (w/2, h) ← bottom
```

**Hexagon:**
```
    (w/4, 0)──(3w/4, 0)
   /                    \
(0, h/2)            (w, h/2)
   \                    /
    (w/4, h)──(3w/4, h)
```

### Performance

- **No external libraries**: Uses only QPainter primitives
- **Efficient rendering**: Direct drawing, no path conversions
- **Scalable**: All shapes scale to node size
- **Resolution-independent**: Works at any zoom level

## Design Rationale

### Why not custom shapes or SVG?

1. **QSchematic precedent**: The demo program uses the same approach successfully
2. **Simplicity**: 5 basic shapes cover 95% of use cases
3. **Efficiency**: Direct QPainter calls are faster than SVG parsing/rendering
4. **Maintainability**: Zero external dependencies, all in standard Qt
5. **Declarative**: Shapes are specified in JSON visual specs

### Backward Compatibility

- Default shape is `RoundedRect` (matches the old visual behavior)
- Nodes without visual specs default to `RoundedRect`
- Existing code works unchanged

## Extension Points

To add a new shape, follow this pattern:

```cpp
// In BasicNodeItem::Shape enum
MyPolygon  // Add new shape

// In setVisualStyle/paint dispatcher
case Shape::MyPolygon:
    paintMyPolygon(painter, rect);
    break;

// Add paint helper
void BasicNodeItem::paintMyPolygon(QPainter* painter, const QRectF& rect)
{
    QPolygon polygon;
    // Define polygon points relative to rect
    polygon << QPoint(...) << QPoint(...) << ...;
    
    QPen pen(borderColor_);
    pen.setWidthF(0.3);
    QBrush brush(fillColor_);
    
    painter->setPen(pen);
    painter->setBrush(brush);
    painter->drawPolygon(polygon);
}

// In shape string converter
if (normalized == "mypolygon") {
    return BasicNodeItem::Shape::MyPolygon;
}
```

## Examples

### Creating Different-Shaped Nodes

```cpp
// Rectangle (plain)
auto rect = std::make_shared<BasicNodeItem>("Multiplier");
rect->setShape(BasicNodeItem::Shape::Rectangle);

// Diamond (decision)
auto decision = std::make_shared<BasicNodeItem>("Switch");
decision->setShape(BasicNodeItem::Shape::Diamond);

// Hexagon (processing)
auto processor = std::make_shared<BasicNodeItem>("Transform");
processor->setShape(BasicNodeItem::Shape::Hexagon);

// Circle (source/sink)
auto source = std::make_shared<BasicNodeItem>("Source");
source->setShape(BasicNodeItem::Shape::Circle);
```

### Visual Catalog Example

Creating a `visuals/` directory with JSON specs:

```
visuals/
  ├── constant.json
  ├── multiplier.json
  ├── switch.json
  └── transformer.json
```

**multiplier.json:**
```json
{
  "type": "Multiplier",
  "displayName": "Multiply",
  "category": "Math",
  "shape": "rect",
  "color": "#4CAF50",
  "description": "Multiplies two signals"
}
```

**switch.json:**
```json
{
  "type": "Switch",
  "displayName": "Switch",
  "category": "Logic",
  "shape": "diamond",
  "color": "#FF9800",
  "description": "Selects one of two inputs"
}
```

## Testing

The shapes have been tested with:
- All zoom levels (0.1x to 10x)
- Node resizing (preserved proportions)
- Selection highlighting
- Connector placement (automatic layout adapts)
- Node rotation (where supported)

## Future Enhancements

Possible future additions:
- Icon overlays on shapes
- Gradient fills
- Shadow effects (already supported via QGraphicsDropShadowEffect)
- Dashed/patterned borders
- Custom SVG-based shapes (via extension)
- Shape-specific connector snap policies

