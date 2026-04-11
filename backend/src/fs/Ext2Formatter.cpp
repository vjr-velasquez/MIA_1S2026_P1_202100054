#include "fs/Ext2Formatter.h"
#include "fs/Ext2IO.h"
#include "fs/JournalManager.h"

#include "structs/SuperBlock.h"
#include "structs/Inode.h"
#include "structs/FolderBlock.h"
#include "structs/FileBlock.h"

#include <cstring>
#include <ctime>
#include <sstream>
#include <stdexcept>

namespace {
    constexpr int32_t EXT2_MAGIC = 0xEF53;
    constexpr int32_t EXT2_TYPE  = 2;
}

int Ext2Formatter::calculateN(int64_t partSize, int fsType) const {
    int64_t journalBytes = (fsType == 3)
        ? static_cast<int64_t>(JournalManager::kJournalCount) * static_cast<int64_t>(sizeof(JournalEntry))
        : 0;

    int64_t numerator = partSize - static_cast<int64_t>(sizeof(SuperBlock)) - journalBytes;
    int64_t denominator =
        1 +
        3 +
        static_cast<int64_t>(sizeof(Inode)) +
        3 * static_cast<int64_t>(sizeof(FileBlock));

    if (denominator <= 0) return 0;
    int64_t n = numerator / denominator;
    if (n < 2) return 0;
    if (n > 1000000) n = 1000000;
    return static_cast<int>(n);
}

std::string Ext2Formatter::format(const std::string& diskPath, int64_t partStart, int64_t partSize, int fsType) {
    try {
        if (partStart < 0 || partSize <= 0) {
            return "ERROR: mkfs -> partición inválida\n";
        }

        int n = calculateN(partSize, fsType);
        if (n <= 0) {
            return "ERROR: mkfs -> tamaño insuficiente para formatear EXT2\n";
        }

        const int32_t inodeCount = n;
        const int32_t blockCount = n * 3;

        const int64_t journalBytes = (fsType == 3)
            ? static_cast<int64_t>(JournalManager::kJournalCount) * static_cast<int64_t>(sizeof(JournalEntry))
            : 0;

        const int64_t bmInodeStart = partStart + static_cast<int64_t>(sizeof(SuperBlock)) + journalBytes;
        const int64_t bmBlockStart = bmInodeStart + inodeCount;
        const int64_t inodeStart   = bmBlockStart + blockCount;
        const int64_t blockStart   = inodeStart + static_cast<int64_t>(inodeCount) * sizeof(Inode);

        Ext2IO io(diskPath);

        // ---------------------------------------------------------------------
        // 1) Inicializar superbloque
        // ---------------------------------------------------------------------
        SuperBlock sb{};
        sb.s_filesystem_type   = fsType;
        sb.s_inodes_count      = inodeCount;
        sb.s_blocks_count      = blockCount;
        sb.s_free_inodes_count = inodeCount - 2; // raíz + users.txt
        sb.s_free_blocks_count = blockCount - 2; // bloque raíz + bloque users.txt

        sb.s_mtime             = static_cast<int64_t>(std::time(nullptr));
        sb.s_umtime            = 0;
        sb.s_mnt_count         = 1;
        sb.s_magic             = EXT2_MAGIC;

        sb.s_inode_size        = static_cast<int32_t>(sizeof(Inode));
        sb.s_block_size        = static_cast<int32_t>(sizeof(FileBlock));
        sb.s_first_ino         = 2;
        sb.s_first_blo         = 2;

        sb.s_bm_inode_start    = bmInodeStart;
        sb.s_bm_block_start    = bmBlockStart;
        sb.s_inode_start       = inodeStart;
        sb.s_block_start       = blockStart;

        io.writeStruct(partStart, sb);

        if (fsType == 3) {
            JournalManager::initialize(diskPath, partStart);
        }

        // ---------------------------------------------------------------------
        // 2) Inicializar bitmaps
        // ---------------------------------------------------------------------
        for (int32_t i = 0; i < inodeCount; ++i) {
            io.writeByte(sb.s_bm_inode_start + i, '0');
        }

        for (int32_t i = 0; i < blockCount; ++i) {
            io.writeByte(sb.s_bm_block_start + i, '0');
        }

        // Marcar usados:
        // inodo 0 = raíz
        // inodo 1 = users.txt
        io.writeByte(sb.s_bm_inode_start + 0, '1');
        io.writeByte(sb.s_bm_inode_start + 1, '1');

        // bloque 0 = carpeta raíz
        // bloque 1 = contenido users.txt
        io.writeByte(sb.s_bm_block_start + 0, '1');
        io.writeByte(sb.s_bm_block_start + 1, '1');

        // ---------------------------------------------------------------------
        // 3) Inicializar tabla de inodos vacía
        // ---------------------------------------------------------------------
        Inode emptyInode{};
        for (int i = 0; i < 15; ++i) {
            emptyInode.i_block[i] = -1;
        }
        emptyInode.i_uid   = -1;
        emptyInode.i_gid   = -1;
        emptyInode.i_size  = 0;
        emptyInode.i_atime = 0;
        emptyInode.i_ctime = 0;
        emptyInode.i_mtime = 0;
        emptyInode.i_type  = '0';
        emptyInode.i_perm  = 0;

        for (int32_t i = 0; i < inodeCount; ++i) {
            io.writeStruct(sb.s_inode_start + static_cast<int64_t>(i) * sizeof(Inode), emptyInode);
        }

        // ---------------------------------------------------------------------
        // 4) Inicializar bloques vacíos
        // ---------------------------------------------------------------------
        FileBlock emptyBlock{};
        std::memset(emptyBlock.b_content, 0, sizeof(emptyBlock.b_content));

        for (int32_t i = 0; i < blockCount; ++i) {
            io.writeStruct(sb.s_block_start + static_cast<int64_t>(i) * sizeof(FileBlock), emptyBlock);
        }

        // ---------------------------------------------------------------------
        // 5) Crear inodo raíz
        // ---------------------------------------------------------------------
        const int64_t now = static_cast<int64_t>(std::time(nullptr));

        Inode root{};
        root.i_uid   = 1;
        root.i_gid   = 1;
        root.i_size  = 0;
        root.i_atime = now;
        root.i_ctime = now;
        root.i_mtime = now;
        for (int i = 0; i < 15; ++i) {
            root.i_block[i] = -1;
        }
        root.i_block[0] = 0;
        root.i_type     = '0';   // carpeta
        root.i_perm     = 664;

        io.writeStruct(sb.s_inode_start + 0 * static_cast<int64_t>(sizeof(Inode)), root);

        // ---------------------------------------------------------------------
        // 6) Crear bloque raíz
        // ---------------------------------------------------------------------
        FolderBlock rootBlock{};
        std::memset(&rootBlock, 0, sizeof(FolderBlock));

        std::strncpy(rootBlock.b_content[0].b_name, ".", sizeof(rootBlock.b_content[0].b_name) - 1);
        rootBlock.b_content[0].b_inodo = 0;

        std::strncpy(rootBlock.b_content[1].b_name, "..", sizeof(rootBlock.b_content[1].b_name) - 1);
        rootBlock.b_content[1].b_inodo = 0;

        std::strncpy(rootBlock.b_content[2].b_name, "users.txt", sizeof(rootBlock.b_content[2].b_name) - 1);
        rootBlock.b_content[2].b_inodo = 1;

        rootBlock.b_content[3].b_name[0] = '\0';
        rootBlock.b_content[3].b_inodo = -1;

        io.writeStruct(sb.s_block_start + 0 * static_cast<int64_t>(sizeof(FolderBlock)), rootBlock);

        // ---------------------------------------------------------------------
        // 7) Crear inodo de users.txt
        // ---------------------------------------------------------------------
        const char* usersText = "1,G,root\n1,U,root,root,123\n";

        Inode usersInode{};
        usersInode.i_uid   = 1;
        usersInode.i_gid   = 1;
        usersInode.i_size  = static_cast<int32_t>(std::strlen(usersText));
        usersInode.i_atime = now;
        usersInode.i_ctime = now;
        usersInode.i_mtime = now;
        for (int i = 0; i < 15; ++i) {
            usersInode.i_block[i] = -1;
        }
        usersInode.i_block[0] = 1;
        usersInode.i_type     = '1'; // archivo
        usersInode.i_perm     = 664;

        io.writeStruct(sb.s_inode_start + 1 * static_cast<int64_t>(sizeof(Inode)), usersInode);

        // ---------------------------------------------------------------------
        // 8) Crear bloque de users.txt
        // ---------------------------------------------------------------------
        FileBlock usersBlock{};
        std::memset(usersBlock.b_content, 0, sizeof(usersBlock.b_content));
        std::strncpy(usersBlock.b_content, usersText, sizeof(usersBlock.b_content) - 1);

        io.writeStruct(sb.s_block_start + 1 * static_cast<int64_t>(sizeof(FileBlock)), usersBlock);

        return std::string("OK: mkfs -> partición formateada como ") + (fsType == 3 ? "EXT3\n" : "EXT2\n");
    } catch (const std::exception& ex) {
        std::ostringstream out;
        out << "ERROR: mkfs -> " << ex.what() << "\n";
        return out.str();
    }
}
