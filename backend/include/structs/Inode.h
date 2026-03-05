#ifndef STRUCTS_INODE_H
#define STRUCTS_INODE_H

#include <cstdint>

#pragma pack(push, 1)
struct Inode {
    int32_t i_uid;           // usuario propietario
    int32_t i_gid;           // grupo propietario
    int32_t i_size;          // tamaño en bytes

    int64_t i_atime;         // última lectura
    int64_t i_ctime;         // creación
    int64_t i_mtime;         // última modificación

    int32_t i_block[15];     // punteros a bloques
    char i_type;             // '0' carpeta, '1' archivo
    int32_t i_perm;          // permisos, ej. 664
};
#pragma pack(pop)

#endif