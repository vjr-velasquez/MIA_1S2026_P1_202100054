#pragma once

#include <string>

#include "fs/Ext2Paths.h"

namespace FsFileOps {
    bool readFile(Ext2Paths& fs, const SuperBlock& sb, const std::string& path, std::string& outContent, int32_t* outInodeIndex = nullptr);
    bool writeFile(Ext2Paths& fs, SuperBlock& sb, int32_t inodeIndex, const std::string& content, std::string& error);
}
