#include "fs/FsFileOps.h"

#include <algorithm>
#include <ctime>
#include <cstring>

namespace FsFileOps {
    bool readFile(Ext2Paths& fs, const SuperBlock& sb, const std::string& path, std::string& outContent, int32_t* outInodeIndex) {
        int32_t inodeIndex = fs.resolvePath(sb, path);
        if (inodeIndex < 0) return false;

        Inode inode = fs.readInode(sb, inodeIndex);
        if (inode.i_type != '1') return false;

        outContent.clear();
        int32_t remaining = inode.i_size;
        for (int b = 0; b < 12 && remaining > 0; ++b) {
            if (inode.i_block[b] < 0) break;

            FileBlock fb = fs.readFileBlock(sb, inode.i_block[b]);
            int32_t chunk = std::min<int32_t>(remaining, 64);
            outContent.append(fb.b_content, fb.b_content + chunk);
            remaining -= chunk;
        }

        if (outInodeIndex) *outInodeIndex = inodeIndex;
        return true;
    }

    bool writeFile(Ext2Paths& fs, SuperBlock& sb, int32_t inodeIndex, const std::string& content, std::string& error) {
        Inode inode = fs.readInode(sb, inodeIndex);
        if (inode.i_type != '1') {
            error = "el inodo no es un archivo";
            return false;
        }

        const int32_t blocksNeeded = std::max<int32_t>(1, static_cast<int32_t>((content.size() + 63) / 64));
        if (blocksNeeded > 12) {
            error = "contenido demasiado grande (máximo 12 bloques directos)";
            return false;
        }

        int32_t allocated = 0;
        for (int b = 0; b < 12; ++b) {
            if (inode.i_block[b] >= 0) allocated++;
        }

        int32_t extraBlocksNeeded = std::max<int32_t>(0, blocksNeeded - allocated);
        if (sb.s_free_blocks_count < extraBlocksNeeded) {
            error = "no hay bloques libres";
            return false;
        }

        for (int b = 0; b < blocksNeeded; ++b) {
            if (inode.i_block[b] < 0) {
                int32_t blkIdx = fs.allocateBlock(sb);
                if (blkIdx < 0) {
                    error = "no hay bloques libres";
                    return false;
                }
                inode.i_block[b] = blkIdx;
            }

            FileBlock fb{};
            std::memset(fb.b_content, 0, sizeof(fb.b_content));

            const size_t start = static_cast<size_t>(b) * 64;
            if (start < content.size()) {
                const size_t len = std::min<size_t>(64, content.size() - start);
                std::memcpy(fb.b_content, content.data() + start, len);
            }

            fs.writeFileBlock(sb, inode.i_block[b], fb);
        }

        inode.i_size = static_cast<int32_t>(content.size());
        inode.i_mtime = static_cast<int64_t>(std::time(nullptr));
        fs.writeInode(sb, inodeIndex, inode);
        fs.writeSuper(sb);
        return true;
    }
}
