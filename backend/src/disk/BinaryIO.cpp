#include "disk/BinaryIO.h"
#include <fstream>

namespace BinaryIO {
bool writeAt0(const std::string& path, const void* data, size_t size) {
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) return false;

    file.seekp(0, std::ios::beg);
    file.write(reinterpret_cast<const char*>(data),
               static_cast<std::streamsize>(size));
    file.close();
    return true;
}
}
