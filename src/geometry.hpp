#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace hypr_edgehover {

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Rect {
    double x      = 0.0;
    double y      = 0.0;
    double width  = 0.0;
    double height = 0.0;

    [[nodiscard]] double left() const;
    [[nodiscard]] double right() const;
    [[nodiscard]] double top() const;
    [[nodiscard]] double bottom() const;
    [[nodiscard]] double centerX() const;
    [[nodiscard]] double centerY() const;
    [[nodiscard]] bool   contains(const Point& point) const;
};

struct WindowCandidate {
    std::size_t index = 0;
    Rect        box;
};

struct GeometryConfig {
    std::string edges       = "lrtb";
    double      inset       = 1.0;
    double      maxDistance = 0.0;
};

enum class Edge {
    Left,
    Right,
    Top,
    Bottom,
};

struct PickResult {
    std::size_t windowIndex    = 0;
    Edge        edge           = Edge::Left;
    Point       targetPoint;
    double      distanceToEdge = 0.0;
};

[[nodiscard]] std::optional<PickResult> pickTarget(const Rect& monitor, const std::vector<WindowCandidate>& windows, const Point& coords,
                                                   const GeometryConfig& config);

} // namespace hypr_edgehover
