#include "fs/Ext2Paths.h"
#include <sstream>
#include <cstring>
#include <ctime>

Ext2Paths::Ext2Paths(const std::string& diskPath, int64_t partStart)
    : io_(diskPath), partStart_(partStart) {}

SuperBlock Ext2Paths::loadSuper() {
    return io_.readStruct<SuperBlock>(partStart_);
}

void Ext2Paths::writeSuper(const SuperBlock& sb) {
    io_.writeStruct(partStart_, sb);
}

Inode Ext2Paths::readInode(const SuperBlock& sb, int32_t inodeIndex) {
    int64_t off = sb.s_inode_start + static_cast<int64_t>(inodeIndex) * sizeof(Inode);
    return io_.readStruct<Inode>(off);
}

void Ext2Paths::writeInode(const SuperBlock& sb, int32_t inodeIndex, const Inode& inode) {
    int64_t off = sb.s_inode_start + static_cast<int64_t>(inodeIndex) * sizeof(Inode);
    io_.writeStruct(off, inode);
}

FolderBlock Ext2Paths::readFolderBlock(const SuperBlock& sb, int32_t blockIndex) {
    int64_t off = sb.s_block_start + static_cast<int64_t>(blockIndex) * sizeof(FolderBlock);
    return io_.readStruct<FolderBlock>(off);
}

void Ext2Paths::writeFolderBlock(const SuperBlock& sb, int32_t blockIndex, const FolderBlock& blk) {
    int64_t off = sb.s_block_start + static_cast<int64_t>(blockIndex) * sizeof(FolderBlock);
    io_.writeStruct(off, blk);
}

FileBlock Ext2Paths::readFileBlock(const SuperBlock& sb, int32_t blockIndex) {
    int64_t off = sb.s_block_start + static_cast<int64_t>(blockIndex) * sizeof(FileBlock);
    return io_.readStruct<FileBlock>(off);
}

void Ext2Paths::writeFileBlock(const SuperBlock& sb, int32_t blockIndex, const FileBlock& blk) {
    int64_t off = sb.s_block_start + static_cast<int64_t>(blockIndex) * sizeof(FileBlock);
    io_.writeStruct(off, blk);
}

std::vector<std::string> Ext2Paths::splitPath(const std::string& absPath) {
    std::vector<std::string> parts;
    if (absPath.empty() || absPath[0] != '/') return parts;

    std::string cur;
    for (size_t i = 1; i < absPath.size(); ++i) {
        char c = absPath[i];
        if (c == '/') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

std::string Ext2Paths::basename(const std::string& absPath) {
    if (absPath.empty() || absPath[0] != '/' || absPath == "/") return "";
    auto parts = splitPath(absPath);
    if (parts.empty()) return "";
    return parts.back();
}

bool Ext2Paths::validName(const std::string& name) {
    if (name.empty()) return false;
    if (name.size() > 11) return false; // Content.b_name[12] => 11 + null
    if (name == "." || name == "..") return false;
    for (char c : name) {
        if (c == '/' || c == '\0') return false;
    }
    return true;
}

int32_t Ext2Paths::findInDirectory(const SuperBlock& sb, int32_t dirInodeIndex, const std::string& name) {
    Inode dir = readInode(sb, dirInodeIndex);
    if (dir.i_type != '0') return -1;

    // Buscar en bloques directos 0..11
    for (int b = 0; b < 12; ++b) {
        if (dir.i_block[b] < 0) continue;

        FolderBlock fb = readFolderBlock(sb, dir.i_block[b]);
        for (int i = 0; i < 4; ++i) {
            if (fb.b_content[i].b_inodo < 0) continue;

            char tmp[12];
            std::memset(tmp, 0, sizeof(tmp));
            std::memcpy(tmp, fb.b_content[i].b_name, 11);
            std::string entryName(tmp);

            if (entryName == name) {
                return fb.b_content[i].b_inodo;
            }
        }
    }

    return -1;
}

int32_t Ext2Paths::resolvePath(const SuperBlock& sb, const std::string& absPath) {
    if (absPath == "/") return 0;

    auto parts = splitPath(absPath);
    if (parts.empty()) return -1;

    int32_t curInode = 0; // raíz
    for (const auto& p : parts) {
        int32_t next = findInDirectory(sb, curInode, p);
        if (next < 0) return -1;
        curInode = next;
    }
    return curInode;
}

int32_t Ext2Paths::allocateInode(SuperBlock& sb) {
    for (int32_t i = 0; i < sb.s_inodes_count; ++i) {
        char v = io_.readByte(sb.s_bm_inode_start + i);
        if (v == '0') {
            io_.writeByte(sb.s_bm_inode_start + i, '1');
            sb.s_free_inodes_count--;
            sb.s_first_ino = i + 1;
            return i;
        }
    }
    return -1;
}

int32_t Ext2Paths::allocateBlock(SuperBlock& sb) {
    for (int32_t i = 0; i < sb.s_blocks_count; ++i) {
        char v = io_.readByte(sb.s_bm_block_start + i);
        if (v == '0') {
            io_.writeByte(sb.s_bm_block_start + i, '1');
            sb.s_free_blocks_count--;
            sb.s_first_blo = i + 1;
            return i;
        }
    }
    return -1;
}

static void setName12(char dst[12], const std::string& name) {
    std::memset(dst, 0, 12);
    std::strncpy(dst, name.c_str(), 11);
}

bool Ext2Paths::addDirEntry(SuperBlock& sb, int32_t parentInodeIndex, const std::string& name, int32_t childInodeIndex) {
    if (!validName(name)) return false;

    Inode parent = readInode(sb, parentInodeIndex);
    if (parent.i_type != '0') return false;

    // 1) Intentar insertar en bloques existentes
    for (int b = 0; b < 12; ++b) {
        if (parent.i_block[b] < 0) continue;

        FolderBlock fb = readFolderBlock(sb, parent.i_block[b]);
        for (int i = 0; i < 4; ++i) {
            if (fb.b_content[i].b_inodo < 0) {
                setName12(fb.b_content[i].b_name, name);
                fb.b_content[i].b_inodo = childInodeIndex;
                writeFolderBlock(sb, parent.i_block[b], fb);
                return true;
            }
        }
    }

    // 2) No había espacio: asignar un nuevo bloque al directorio
    int freeSlot = -1;
    for (int b = 0; b < 12; ++b) {
        if (parent.i_block[b] < 0) { freeSlot = b; break; }
    }
    if (freeSlot < 0) return false;

    int32_t newBlockIdx = allocateBlock(sb);
    if (newBlockIdx < 0) return false;

    // Bloque carpeta vacío
    FolderBlock newFb{};
    std::memset(&newFb, 0, sizeof(FolderBlock));
    for (int i = 0; i < 4; ++i) {
        newFb.b_content[i].b_inodo = -1;
        newFb.b_content[i].b_name[0] = '\0';
    }

    // Insertar nueva entrada
    setName12(newFb.b_content[0].b_name, name);
    newFb.b_content[0].b_inodo = childInodeIndex;

    writeFolderBlock(sb, newBlockIdx, newFb);

    // Conectar nuevo bloque al inodo padre
    parent.i_block[freeSlot] = newBlockIdx;
    parent.i_mtime = static_cast<int64_t>(std::time(nullptr));
    writeInode(sb, parentInodeIndex, parent);

    return true;
}
