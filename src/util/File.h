#pragma once

#include <fstream>
#include <span>
#include <string>
#include <type_traits>
#include <format>

namespace Neb
{

    template<typename T>
    void WriteBinaryFile(const std::string& path,
        std::span<T> data,
        std::ios::openmode mode = std::ios::binary | std::ios::trunc)
    {
        static_assert(std::is_trivially_copyable_v<T>,
            "WriteBinaryFile() requires a trivially-copyable element type");

        std::ofstream out(path, mode);
        if (!out) // file couldn’t be created/opened
            throw std::ios_base::failure(std::format("Failed to open \"{}\"", path));

        std::span<const std::byte> bytes = std::as_bytes(data);
        const std::streamsize n = static_cast<std::streamsize>(data.size_bytes());

        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size_bytes()));

        // check every platform’s badbit/failbit
        if (!out)                                          
            throw std::ios_base::failure(std::format("Error while writing to \"{}\"", path));
    }

} // Neb namespace
