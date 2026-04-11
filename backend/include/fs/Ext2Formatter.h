#ifndef FS_EXT2FORMATTER_H
#define FS_EXT2FORMATTER_H

#include <string>
#include <cstdint>  // <-- recomendado para int64_t

class Ext2Formatter {
public:
    Ext2Formatter() = default;

    // Formatea una partición en diskPath, iniciando en partStart y con tamaño partSize
    // Devuelve mensaje listo para imprimir: OK: ... o ERROR: ...
    std::string format(const std::string& diskPath, int64_t partStart, int64_t partSize, int fsType = 2);

private:
    int calculateN(int64_t partSize, int fsType) const;
};

#endif
