#ifndef STRUCTS_FILEBLOCK_H
#define STRUCTS_FILEBLOCK_H

#pragma pack(push, 1)
struct FileBlock {
    char b_content[64];
};
#pragma pack(pop)

#endif