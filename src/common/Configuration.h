#pragma once

#include <variant>
#include <array>
#include <optional>

namespace Neb
{

    enum class EConfigKey
    {
        EnableDebugLayer = 0,
        EnableGpuValidation,
        EnableDeviceDebugging,
        NumConfigKeys
    };

    struct ConfigValue
    {
        ConfigValue() = default;
        ConfigValue(bool value)
            : m_val(value)
        {
        }

        template<typename T>
        T Get() const { return std::get<T>(m_val); }

        template<typename T>
        T Get(T defaultValue) const { return std::holds_alternative<std::monostate>(m_val) ? defaultValue : Get<T>(); }

    private:
        std::variant<std::monostate, bool> m_val;
    };

    class Config
    {
    private:
        Config() = default;

        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;

    public:
        static Config* Get()
        {
            static Config instance;
            return &instance;
        }

        template<typename T>
        static void SetValue(EConfigKey key, T value)
        {
            Config* cfg = Config::Get();
            cfg->m_valueArray.at(static_cast<size_t>(key)) = ConfigValue(value);
        }

        template<typename T>
        static T GetValue(EConfigKey key, std::optional<T> defaultValue = {})
        {
            const Config* cfg = Config::Get();
            const ConfigValue& value = cfg->m_valueArray.at(static_cast<size_t>(key));
            return defaultValue.has_value() ? value.Get<T>(*defaultValue) : value.Get<T>();
        }

    private:
        std::array<ConfigValue, static_cast<size_t>(EConfigKey::NumConfigKeys)> m_valueArray;
    };

}