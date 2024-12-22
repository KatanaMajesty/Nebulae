#pragma once


namespace Neb
{

    class ConfigParser
    {
    public:
        ConfigParser();
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

        // In Debug configuration GPU-based validation is always enabled, unless this option is specified
        //
        std::optional<bool> enableGpuBasedValidation;
    };

}