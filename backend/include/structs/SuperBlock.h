#ifndef STRUCTS_SUPERBLOCK_H
#define STRUCTS_SUPERBLOCK_H

#include <cstdint>

#pragma pack(push, 1)
struct SuperBlock {
    int32_t s_filesystem_type;   // 2 = EXT2
    int32_t s_inodes_count;      // total de inodos
    int32_t s_blocks_count;      // total de bloques
    int32_t s_free_blocks_count; // bloques libres
    int32_t s_free_inodes_count; // inodos libres

    int64_t s_mtime;             // fecha de montaje
    int64_t s_umtime;            // fecha de desmontaje
    int32_t s_mnt_count;         // conteo de montajes
    int32_t s_magic;             // 0xEF53

    int32_t s_inode_size;        // sizeof(Inode)
    int32_t s_block_size;        // tamaño de bloque
    int32_t s_first_ino;         // primer inodo libre
    int32_t s_first_blo;         // primer bloque libre

    int64_t s_bm_inode_start;    // inicio bitmap de inodos
    int64_t s_bm_block_start;    // inicio bitmap de bloques
    int64_t s_inode_start;       // inicio tabla de inodos
    int64_t s_block_start;       // inicio tabla de bloques
};
#pragma pack(pop)

#endif