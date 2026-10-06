#pragma once

#include <QPoint>
#include <QRect>
#include <QVector>

#include <vector>

namespace s7studio::diagram {

// Orthogonal wire routing on the diagram grid (all coordinates in grid steps;
// STUDIO_DIAGRAM_WIRING_DESIGN.md, section 6). A* over grid points with
// costs for length, bends, crossing other nets, running next to nodes and
// overlapping own-net wires; nodes, overlapping or touching other nets are
// so expensive that they are only used when nothing else is possible.
class WireRouter
{
public:
    // `bounds`: the grid area routes stay in.
    explicit WireRouter(QRect bounds);

    // A node (or text frame) occupying `cells` (inclusive grid rectangle).
    void addObstacle(const QRect& cells);
    // A routed wire of net `net` (a polyline through grid points).
    void addWire(const QVector<QPoint>& points, int net);

    struct Request {
        QVector<QPoint> anchors; // start, user bends, end
        int net = -1;
        bool leaveEast = false;  // the start is an output port: the wire leaves to the right
        bool enterEast = false;  // the end is an input port: the wire arrives from the left
    };
    // The route through every anchor, corners only (start and end included).
    [[nodiscard]] auto route(const Request& request) const -> QVector<QPoint>;

private:
    enum Dir { East, West, North, South, None };

    [[nodiscard]] bool inside(QPoint point) const;
    [[nodiscard]] int cell(QPoint point) const;
    [[nodiscard]] int horizontalNet(QPoint point) const; // net of a horizontal segment through `point`, -1 none
    [[nodiscard]] int verticalNet(QPoint point) const;
    void markSegment(QPoint from, QPoint to, int net);
    [[nodiscard]] auto leg(QPoint start, QPoint goal, Dir startDir, int net, bool leaveEast, bool enterEast, Dir* endDir) const
        -> QVector<QPoint>;

    QRect bounds_;
    int width_;
    int height_;
    std::vector<unsigned char> node_; // 0 free, 1 next to a node, 2 node
    std::vector<int> hNet_;           // net of the unit segment (x,y)-(x+1,y); -1 none, -2 several
    std::vector<int> vNet_;           // net of the unit segment (x,y)-(x,y+1)
    std::vector<int> vertexNet_;      // net having a bend or an end at the point
};

} // namespace s7studio::diagram
