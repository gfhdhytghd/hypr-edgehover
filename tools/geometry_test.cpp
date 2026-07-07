#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "geometry.hpp"

namespace {

using hypr_edgehover::Edge;
using hypr_edgehover::GeometryConfig;
using hypr_edgehover::Point;
using hypr_edgehover::Rect;
using hypr_edgehover::WindowCandidate;
using hypr_edgehover::pickTarget;

bool expect(bool condition, const char* message) {
    if (condition)
        return true;

    std::cerr << "FAIL: " << message << '\n';
    return false;
}

bool closeEnough(double actual, double expected, double epsilon = 1e-6) {
    return std::abs(actual - expected) <= epsilon;
}

bool expectPoint(const Point& actual, const Point& expected, const char* message) {
    return expect(closeEnough(actual.x, expected.x) && closeEnough(actual.y, expected.y), message);
}

} // namespace

int main() {
    bool ok = true;

    const Rect                       monitor{0, 0, 1000, 800};
    const std::vector<WindowCandidate> singleWindow = {
        {.index = 0, .box = {20, 30, 960, 740}},
    };

    {
        const GeometryConfig config{.edges = "lrtb", .inset = 1, .maxDistance = 0};

        const auto left = pickTarget(monitor, singleWindow, {0, 400}, config);
        ok &= expect(left.has_value(), "left edge should trigger");
        ok &= expect(left && left->edge == Edge::Left, "left edge should report left");
        ok &= left ? expectPoint(left->targetPoint, {21, 400}, "left edge should clamp x inside the window") : false;

        const auto right = pickTarget(monitor, singleWindow, {999, 400}, config);
        ok &= expect(right.has_value(), "right edge should trigger");
        ok &= expect(right && right->edge == Edge::Right, "right edge should report right");
        ok &= right ? expectPoint(right->targetPoint, {979, 400}, "right edge should clamp x inside the window") : false;

        const auto top = pickTarget(monitor, singleWindow, {500, 0}, config);
        ok &= expect(top.has_value(), "top edge should trigger");
        ok &= expect(top && top->edge == Edge::Top, "top edge should report top");
        ok &= top ? expectPoint(top->targetPoint, {500, 31}, "top edge should clamp y inside the window") : false;

        const auto bottom = pickTarget(monitor, singleWindow, {500, 799}, config);
        ok &= expect(bottom.has_value(), "bottom edge should trigger");
        ok &= expect(bottom && bottom->edge == Edge::Bottom, "bottom edge should report bottom");
        ok &= bottom ? expectPoint(bottom->targetPoint, {500, 769}, "bottom edge should clamp y inside the window") : false;
    }

    {
        const GeometryConfig config{.edges = "lrtb", .inset = 1, .maxDistance = 0};

        const auto topLeft = pickTarget(monitor, singleWindow, {0, 0}, config);
        ok &= expect(topLeft.has_value(), "top-left corner should trigger");
        ok &= topLeft ? expectPoint(topLeft->targetPoint, {21, 31}, "top-left corner should clamp both axes") : false;

        const auto topRight = pickTarget(monitor, singleWindow, {999, 0}, config);
        ok &= expect(topRight.has_value(), "top-right corner should trigger");
        ok &= topRight ? expectPoint(topRight->targetPoint, {979, 31}, "top-right corner should clamp both axes") : false;

        const auto bottomLeft = pickTarget(monitor, singleWindow, {0, 799}, config);
        ok &= expect(bottomLeft.has_value(), "bottom-left corner should trigger");
        ok &= bottomLeft ? expectPoint(bottomLeft->targetPoint, {21, 769}, "bottom-left corner should clamp both axes") : false;

        const auto bottomRight = pickTarget(monitor, singleWindow, {999, 799}, config);
        ok &= expect(bottomRight.has_value(), "bottom-right corner should trigger");
        ok &= bottomRight ? expectPoint(bottomRight->targetPoint, {979, 769}, "bottom-right corner should clamp both axes") : false;
    }

    {
        const auto result = pickTarget(monitor, singleWindow, {10, 400}, {.edges = "lrtb", .inset = 1, .maxDistance = 0});
        ok &= expect(result.has_value(), "mid-gap-band point should trigger");
        ok &= result ? expectPoint(result->targetPoint, {21, 400}, "mid-gap-band point should clamp to the inside edge") : false;
    }

    {
        const auto result = pickTarget(monitor, singleWindow, {10, 400}, {.edges = "lrtb", .inset = 1, .maxDistance = 5});
        ok &= expect(!result.has_value(), "point beyond max_distance should not trigger");
    }

    {
        const auto result = pickTarget(monitor, singleWindow, {0, 400}, {.edges = "rtb", .inset = 1, .maxDistance = 0});
        ok &= expect(!result.has_value(), "disabled left edge should not trigger");
    }

    {
        ok &= expect(!pickTarget(monitor, {{.index = 0, .box = {-20, 30, 960, 740}}}, {0, 400}, {.edges = "l", .inset = 1, .maxDistance = 0}).has_value(),
                     "left edge should reject windows extending past the monitor edge");
        ok &= expect(!pickTarget(monitor, {{.index = 0, .box = {20, 30, 1000, 740}}}, {999, 400}, {.edges = "r", .inset = 1, .maxDistance = 0}).has_value(),
                     "right edge should reject windows extending past the monitor edge");
        ok &= expect(!pickTarget(monitor, {{.index = 0, .box = {20, -30, 960, 740}}}, {500, 0}, {.edges = "t", .inset = 1, .maxDistance = 0}).has_value(),
                     "top edge should reject windows extending past the monitor edge");
        ok &= expect(!pickTarget(monitor, {{.index = 0, .box = {20, 30, 960, 800}}}, {500, 799}, {.edges = "b", .inset = 1, .maxDistance = 0}).has_value(),
                     "bottom edge should reject windows extending past the monitor edge");
    }

    {
        const std::vector<WindowCandidate> windows = {
            {.index = 0, .box = {-10, 100, 400, 200}},
            {.index = 1, .box = {20, 300, 400, 200}},
        };

        const auto result = pickTarget(monitor, windows, {0, 350}, {.edges = "l", .inset = 1, .maxDistance = 0});
        ok &= expect(result.has_value(), "wrong-side left candidate should not block valid candidates");
        ok &= expect(result && result->windowIndex == 1, "left edge should pick the valid candidate after rejecting the wrong-side one");
        ok &= result ? expectPoint(result->targetPoint, {21, 350}, "valid candidate target should clamp normally") : false;
    }

    {
        const std::vector<WindowCandidate> windows = {
            {.index = 0, .box = {20, 100, 400, 200}},
            {.index = 1, .box = {20, 500, 400, 200}},
        };

        const auto result = pickTarget(monitor, windows, {0, 450}, {.edges = "l", .inset = 1, .maxDistance = 0});
        ok &= expect(result.has_value(), "gap between two windows should still pick a target");
        ok &= expect(result && result->windowIndex == 1, "gap between two windows should pick the parallel-nearest window");
        ok &= result ? expectPoint(result->targetPoint, {21, 501}, "parallel-nearest target should clamp to that window") : false;
    }

    {
        const std::vector<WindowCandidate> tinyWindow = {
            {.index = 0, .box = {20, 30, 10, 10}},
        };

        const auto result = pickTarget(monitor, tinyWindow, {0, 35}, {.edges = "l", .inset = 7, .maxDistance = 0});
        ok &= expect(result.has_value(), "tiny window should still trigger");
        ok &= result ? expectPoint(result->targetPoint, {25, 35}, "oversized inset should clamp to the window center on that axis") : false;
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
