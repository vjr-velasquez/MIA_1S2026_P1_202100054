#pragma once
#include <cstdint>

#pragma pack(push, 1)

struct EBR {
    char part_status;   // '0' libre, '1' ocupada
    char part_fit;      // 'B','F','W'
    int32_t part_start; // byte inicio del área de datos de la lógica
    int32_t part_size;  // bytes del área de datos
    int32_t part_next;  // offset del siguiente EBR, -1 si no hay
    char part_name[16]; // nombre
};

#pragma pack(pop)