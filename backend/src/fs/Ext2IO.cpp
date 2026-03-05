#include "fs/Ext2IO.h"

Ext2IO::Ext2IO(const std::string& diskPath)
    : diskPath_(diskPath) {}

void Ext2IO::writeBytes(int64_t offset, const char* data, size_t size) {
    std::fstream file(diskPath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("no se pudo abrir el disco: " + diskPath_);
    }

    file.seekp(offset);
    file.write(data, static_cast<std::streamsize>(size));

    if (!file) {
        throw std::runtime_error("error escribiendo bytes en disco");
    }
}

void Ext2IO::readBytes(int64_t offset, char* buffer, size_t size) {
    std::fstream file(diskPath_, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("no se pudo abrir el disco: " + diskPath_);
    }

    file.seekg(offset);
    file.read(buffer, static_cast<std::streamsize>(size));

    if (!file) {
        throw std::runtime_error("error leyendo bytes en disco");
    }
}

void Ext2IO::writeByte(int64_t offset, char value) {
    writeBytes(offset, &value, 1);
}

char Ext2IO::readByte(int64_t offset) {
    char value = '\0';
    readBytes(offset, &value, 1);
    return value;
}
