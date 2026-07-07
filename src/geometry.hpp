#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
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

struct ZoneRange {
    double start = 0.0;
    double end   = 0.0;
};

using ZoneRanges = std::vector<ZoneRange>;

struct EdgeZones {
    ZoneRanges left   = {{0.0, 100.0}};
    ZoneRanges right  = {{0.0, 100.0}};
    ZoneRanges top    = {{0.0, 100.0}};
    ZoneRanges bottom = {{0.0, 100.0}};
};

struct VisibleThickness {
    double left   = std::numeric_limits<double>::infinity();
    double right  = std::numeric_limits<double>::infinity();
    double top    = std::numeric_limits<double>::infinity();
    double bottom = std::numeric_limits<double>::infinity();
};

struct WindowCandidate {
    std::size_t      index = 0;
    Rect             box;
    VisibleThickness visibleThickness;
};

struct GeometryConfig {
    std::string edges       = "lrtb";
    double      inset       = 1.0;
    double      maxDistance = 0.0;
    double      overhangThreshold = 8.0;
    EdgeZones   zones;
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

struct EdgeHit {
    Edge   edge     = Edge::Left;
    double distance = 0.0;
};

[[nodiscard]] ZoneRanges parseZones(std::string_view spec);
[[nodiscard]] bool       zoneContains(const ZoneRanges& zones, double percent);
[[nodiscard]] bool       edgeZoneMatches(const Rect& monitor, const Point& coords, Edge edge, const EdgeZones& zones);
[[nodiscard]] std::optional<EdgeHit> nearestEnabledEdge(const Rect& monitor, const Point& coords, const GeometryConfig& config);
[[nodiscard]] std::optional<PickResult> pickTarget(const Rect& monitor, const std::vector<WindowCandidate>& windows, const Point& coords,
                                                   const GeometryConfig& config);

} // namespace hypr_edgehover
