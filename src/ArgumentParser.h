#pragma once

#include <sstream>
#include <string_view>
#include <unordered_map>
#include <exception>
#include <format>
#include <vector>
#include <numeric>
#include <span>

namespace Neb
{

    /* clang-format off */
    struct ArgumentValue
    {
        ArgumentValue() = default;
        ArgumentValue(std::string_view value)
            : value(value)
        {
        }

        template<typename T>
        T As() const
        {
            std::stringstream conv = std::stringstream(std::string(value));
            
            T value;
            if (conv >> value)
                return  value;

            throw std::runtime_error("ArgumentValue::As => failed to convert value to specified type");
        }

        template<>
        bool As() const
        {
            std::string v(value);
            std::ranges::transform(v, v.begin(), [](unsigned char c){ return std::tolower(c); });
            std::stringstream conv = std::stringstream(v);
            
            bool value = false;
            if (conv >> std::boolalpha >> value)
                return value;

            throw std::runtime_error("ArgumentValue::As => failed to convert value to specified type");
        }

        template<>
        std::string_view As() const { return value; }

        template<typename T>
        operator T() const { return this->As<T>(); }

        std::string_view value;
    };
    /* clang-format on */

    struct ArgumentParser
    {
        ArgumentParser() = default;
        ArgumentParser(int32_t argc, char* argv[])
        {
            std::vector<std::string_view> args(argc);
            for (uint32_t i = 0; std::string_view & arg : args)
                arg = argv[i++];

            this->Parse(args);
        }
        ArgumentParser(std::span<std::string_view> args)
        {
            this->Parse(args);
        }

        inline void Parse(std::span<std::string_view> args)
        {
            argumentMap.clear();
            for (std::string_view arg : args)
            {
                size_t p = arg.find_first_of('=');
                if (p == std::string_view::npos)
                {
                    // if no '=', then just chuck the whole argument as a value
                    // (to be most likely used as a string-value)
                    this->Set(arg, arg);
                    continue;
                }

                // we found '=' -> now determine key/val pair substrings
                const std::string_view key = arg.substr(2, p - 2);
                const std::string_view val = arg.substr(p + 1);
                this->Set(key, val);
            }
        }

        inline void Set(std::string_view key, const ArgumentValue& value)
        {
            if (Contains(key))
            {
                throw std::runtime_error(
                    std::format("ArgumentParser::Set => map already contains value ('{}') for key '{}'",
                        argumentMap.at(key).As<std::string_view>(), key));
            }

            argumentMap[key] = value;
        }

        template<typename T>
        T Get(std::string_view key)
        {
            if (!Contains(key))
            {
                throw std::runtime_error(
                    std::format("ArgumentParser::Get => failed to get value for key '{}' as it was not found in argument map", key));
            }

            return argumentMap.at(key).As<T>();
        }

        template<typename T>
        T Get(std::string_view key, T defaultValue) noexcept
        {
            return (Contains(key)) ? argumentMap.at(key).As<T>() : defaultValue;
        }

        inline bool Contains(std::string_view key) const
        {
            return argumentMap.contains(key);
        }

        // Generally for arguments of type 'key=value' map would store them in that exact way,
        // but for argument of type 'value' map will basically use value as a key itself
        std::unordered_map<std::string_view, ArgumentValue> argumentMap;
    };

}
