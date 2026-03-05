#ifndef STRUCTS_FOLDERBLOCK_H
#define STRUCTS_FOLDERBLOCK_H

#include <cstdint>

#pragma pack(push, 1)
struct Content {
    char b_name[12];
    int32_t b_inodo;
};

struct FolderBlock {
    Content b_content[4];
};
#pragma pack(pop)

#endif