#pragma once

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace hypr_edgehover {

struct RuntimeConfig {
    bool        enabled       = true;
    std::string edges         = "lrtb";
    double      inset         = 1.0;
    double      maxDistance   = 0.0;
    long        keyboardFocus = -1;
    long        followMouse   = 0;
};

class EdgeHover {
  public:
    explicit EdgeHover(HANDLE handle);
    ~EdgeHover() = default;

    bool initialize();

  private:
    RuntimeConfig readConfig() const;
    void          handleMouseMove(const Vector2D& coords, Event::SCallbackInfo& info);

    HANDLE              m_handle = nullptr;
    CHyprSignalListener m_mouseMoveListener;
};

} // namespace hypr_edgehover
