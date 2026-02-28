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

bool readAt0(const std::string& path, void* data, size_t size) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(data),
              static_cast<std::streamsize>(size));
    bool ok = file.good() || file.eof();
    file.close();
    return ok;
}

// NUEVO
bool writeAt(const std::string& path, int64_t offset, const void* data, size_t size) {
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) return false;

    file.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file.good()) { file.close(); return false; }

    file.write(reinterpret_cast<const char*>(data),
               static_cast<std::streamsize>(size));
    file.flush();
    bool ok = file.good();
    file.close();
    return ok;
}

// NUEVO
bool readAt(const std::string& path, int64_t offset, void* data, size_t size) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file.good()) { file.close(); return false; }

    file.read(reinterpret_cast<char*>(data),
              static_cast<std::streamsize>(size));
    bool ok = file.good() || file.eof();
    file.close();
    return ok;
}

}