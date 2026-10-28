#pragma once

#include <string>
#include <vector>
#include <src/config/ConfigManager.hpp>
#include <src/plugins/PluginAPI.hpp>

namespace hypr_edgehover {

// Full reset flushes CConfigValue caches before preReload can restore plugin
// options. These descriptors must not bind a cache into the old manager.
template <typename Value>
class PluginValue final : public Value {
  public:
    using Value::Value;
    void commence() override {}
};

class PluginConfig {
  public:
    bool add(HANDLE handle, SP<Config::Values::IValue> value) {
        if (!HyprlandAPI::addConfigValueV2(handle, value))
            return false;
        m_handle = handle;
        m_values.push_back(value);
        return true;
    }

    bool restore() const {
        if (!Config::mgr())
            return false;
        for (const auto& value : m_values) {
            // Check presence rather than manager address: an allocator can
            // reuse the old manager's address after a full reset.
            if (Config::mgr()->getConfigValue(value->name()).dataptr)
                continue;
            if (!Config::mgr()->registerPluginValue(m_handle, value))
                return false;
        }
        return true;
    }

    void clear() { m_values.clear(); }

  private:
    HANDLE m_handle = nullptr;
    std::vector<SP<Config::Values::IValue>> m_values;
};

// Keep only the name across reloads. Copy the value from the current manager
// on each read; a missing option disables delivery instead of dereferencing a
// pointer from a destroyed manager.
template <typename T>
class PluginConfigValue {
  public:
    explicit PluginConfigValue(const char* name) : m_name(name) {}
    T operator*() const {
        if (!Config::mgr())
            return {};
        const auto value = Config::mgr()->getConfigValue(m_name);
        if (!value.dataptr || !value.type || *value.type != typeid(T))
            return {};
        return **reinterpret_cast<const T* const*>(value.dataptr);
    }

  private:
    const char* m_name;
};

} // namespace hypr_edgehover
