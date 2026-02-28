#pragma once
#include <string>
#include <cstddef>
#include <cstdint>

namespace BinaryIO {
    bool writeAt0(const std::string& path, const void* data, size_t size);
    bool readAt0(const std::string& path, void* data, size_t size);

    // NUEVO: offsets arbitrarios (para EBR y reportes)
    bool writeAt(const std::string& path, int64_t offset, const void* data, size_t size);
    bool readAt(const std::string& path, int64_t offset, void* data, size_t size);
}