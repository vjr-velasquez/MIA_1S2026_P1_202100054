#pragma once
#include <cstdint>

#pragma pack(push, 1)

struct Partition {
    char part_status;   // '0' libre, '1' ocupada
    char part_type;     // 'P' primaria, 'E' extendida
    char part_fit;      // 'B','F','W'
    int32_t part_start; // byte inicio
    int32_t part_size;  // bytes
    char part_name[16]; // nombre
};

struct MBR {
    int32_t mbr_tamano;         // bytes totales del disco
    int64_t mbr_fecha_creacion; // timestamp unix
    int32_t mbr_disk_signature; // random
    char mbr_fit;               // 'B','F','W'
    Partition mbr_partitions[4];
};

#pragma pack(pop)
