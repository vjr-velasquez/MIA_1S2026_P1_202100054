#ifndef FS_EXT2PATHS_H
#define FS_EXT2PATHS_H

#include <string>
#include <vector>
#include <cstdint>

#include "fs/Ext2IO.h"
#include "structs/SuperBlock.h"
#include "structs/Inode.h"
#include "structs/FolderBlock.h"
#include "structs/FileBlock.h"

struct DirEntryRef {
    std::string name;
    int32_t inodeIndex;
    int32_t blockPtrSlot;
    int32_t contentSlot;
};

class Ext2Paths {
public:
    explicit Ext2Paths(const std::string& diskPath, int64_t partStart);

    // Lee superbloque desde el inicio de la partición
    SuperBlock loadSuper();

    // Lee inodo por índice
    Inode readInode(const SuperBlock& sb, int32_t inodeIndex);

    // Escribe inodo por índice
    void writeInode(const SuperBlock& sb, int32_t inodeIndex, const Inode& inode);

    // Lee bloque de carpeta (FolderBlock) por índice
    FolderBlock readFolderBlock(const SuperBlock& sb, int32_t blockIndex);

    // Escribe bloque de carpeta (FolderBlock) por índice
    void writeFolderBlock(const SuperBlock& sb, int32_t blockIndex, const FolderBlock& blk);

    // Lee bloque de archivo (FileBlock) por índice
    FileBlock readFileBlock(const SuperBlock& sb, int32_t blockIndex);

    // Escribe bloque de archivo (FileBlock) por índice
    void writeFileBlock(const SuperBlock& sb, int32_t blockIndex, const FileBlock& blk);

    // Buscar en una carpeta (inodeIndex) una entrada con nombre => retorna inodeIndex o -1
    int32_t findInDirectory(const SuperBlock& sb, int32_t dirInodeIndex, const std::string& name);

    // Resolver ruta absoluta: /a/b/c
    // Retorna inode final o -1 si no existe.
    int32_t resolvePath(const SuperBlock& sb, const std::string& absPath);

    // Divide "/home/docs" => ["home","docs"]
    static std::vector<std::string> splitPath(const std::string& absPath);

    // Retorna el último segmento de la ruta absoluta, o vacío si inválida.
    static std::string basename(const std::string& absPath);

    // Buscar primer inodo libre según bitmap (retorna index o -1)
    int32_t allocateInode(SuperBlock& sb);

    // Buscar primer bloque libre según bitmap (retorna index o -1)
    int32_t allocateBlock(SuperBlock& sb);

    // insertar entrada en un directorio (crea nuevo bloque si está lleno)
    bool addDirEntry(SuperBlock& sb, int32_t parentInodeIndex, const std::string& name, int32_t childInodeIndex);

    // Lista entradas válidas del directorio
    std::vector<DirEntryRef> listDirectory(const SuperBlock& sb, int32_t dirInodeIndex);

    // Elimina una entrada dentro de un directorio
    bool removeDirEntry(const SuperBlock& sb, int32_t parentInodeIndex, const std::string& name);

    // Cambia el nombre de una entrada dentro de un directorio
    bool renameDirEntry(const SuperBlock& sb, int32_t parentInodeIndex, const std::string& oldName, const std::string& newName);

    // Libera estructuras del bitmap y limpia el contenido
    bool freeInode(SuperBlock& sb, int32_t inodeIndex);
    bool freeBlock(SuperBlock& sb, int32_t blockIndex);

    // Guarda superbloque actualizado
    void writeSuper(const SuperBlock& sb);

private:
    Ext2IO io_;
    int64_t partStart_;

    static bool validName(const std::string& name);
};

#endif
