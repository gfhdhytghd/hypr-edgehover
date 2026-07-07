#include <memory>
#include <string>

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "edgehover.hpp"

inline HANDLE                                  g_pluginHandle = nullptr;
inline std::unique_ptr<hypr_edgehover::EdgeHover> g_edgeHover;

namespace {

bool addConfigValue(SP<Config::Values::IValue> value) {
    const std::string name = value->name();

    if (HyprlandAPI::addConfigValueV2(g_pluginHandle, value))
        return true;

    HyprlandAPI::addNotification(
        g_pluginHandle,
        "[hypr-edgehover] failed to register config value " + name,
        CHyprColor(1.0, 0.2, 0.2, 1.0),
        5000);
    return false;
}

bool addIntConfig(const char* name, Config::INTEGER fallback) {
    return addConfigValue(makeShared<Config::Values::CIntValue>(name, "", fallback));
}

bool addStringConfig(const char* name, Config::STRING fallback) {
    return addConfigValue(makeShared<Config::Values::CStringValue>(name, "", fallback));
}

} // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    g_pluginHandle = handle;

#define INT_CONF(name, value) addIntConfig("plugin:hypr_edgehover:" name, Config::INTEGER{value})
#define STRING_CONF(name, value) addStringConfig("plugin:hypr_edgehover:" name, Config::STRING{value})
    INT_CONF("enabled", 1);
    STRING_CONF("edges", "lrtb");
    INT_CONF("inset", 1);
    INT_CONF("max_distance", 0);
    INT_CONF("keyboard_focus", -1);
    STRING_CONF("gap_pass", "hover,click,scroll,keyboard");
    STRING_CONF("layer_pass", "hover,keyboard");
    STRING_CONF("layer_namespaces", "");
    STRING_CONF("overhang_pass", "hover,keyboard");
    INT_CONF("overhang_threshold", 8);
    INT_CONF("overhang_edge_width", 0);
    INT_CONF("steal_edge_width", 2);
    STRING_CONF("zones_top", "0-100");
    STRING_CONF("zones_bottom", "0-100");
    STRING_CONF("zones_left", "0-100");
    STRING_CONF("zones_right", "0-100");
#undef STRING_CONF
#undef INT_CONF

    g_edgeHover = std::make_unique<hypr_edgehover::EdgeHover>(g_pluginHandle);
    if (!g_edgeHover->initialize()) {
        HyprlandAPI::addNotification(g_pluginHandle, "[hypr-edgehover] failed to initialize input listener", CHyprColor(1.0, 0.2, 0.2, 1.0), 5000);
    }

    if (!HyprlandAPI::reloadConfig()) {
        HyprlandAPI::addNotification(g_pluginHandle, "[hypr-edgehover] reloadConfig failed", CHyprColor(1.0, 0.2, 0.2, 1.0), 5000);
    }

    return {
        .name = "hypr-edgehover",
        .description = "Forward edge-gap pointer motion to adjacent windows",
        .author = "wilf",
        .version = "0.1.0",
    };
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_edgeHover.reset();
}
