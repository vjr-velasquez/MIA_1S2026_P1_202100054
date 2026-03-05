#ifndef FS_EXT2IO_H
#define FS_EXT2IO_H

#include <string>
#include <fstream>
#include <stdexcept>

class Ext2IO {
public:
    explicit Ext2IO(const std::string& diskPath);

    template <typename T>
    void writeStruct(int64_t offset, const T& value) {
        std::fstream file(diskPath_, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("no se pudo abrir el disco: " + diskPath_);
        }

        file.seekp(offset);
        file.write(reinterpret_cast<const char*>(&value), sizeof(T));

        if (!file) {
            throw std::runtime_error("error escribiendo estructura en disco");
        }
    }

    template <typename T>
    T readStruct(int64_t offset) {
        std::fstream file(diskPath_, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("no se pudo abrir el disco: " + diskPath_);
        }

        T value{};
        file.seekg(offset);
        file.read(reinterpret_cast<char*>(&value), sizeof(T));

        if (!file) {
            throw std::runtime_error("error leyendo estructura en disco");
        }

        return value;
    }

    void writeBytes(int64_t offset, const char* data, size_t size);
    void readBytes(int64_t offset, char* buffer, size_t size);

    void writeByte(int64_t offset, char value);
    char readByte(int64_t offset);

private:
    std::string diskPath_;
};

#endif