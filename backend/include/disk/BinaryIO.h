#pragma once
#include <string>
#include <cstddef>

namespace BinaryIO {
    bool writeAt0(const std::string& path, const void* data, size_t size);
}
