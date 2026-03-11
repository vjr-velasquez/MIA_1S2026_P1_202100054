#include "commands/MkFileCmd.h"
#include "disk/MountManager.h"
#include "fs/Ext2Paths.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <ctime>

namespace {
    static inline std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    static inline std::string tolower_str(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static inline std::string strip_quotes(std::string v) {
        v = trim(v);
        if (v.size() >= 2) {
            if ((v.front() == '"' && v.back() == '"') ||
                (v.front() == '\'' && v.back() == '\'')) {
                return v.substr(1, v.size() - 2);
            }
        }
        return v;
    }

    static std::vector<std::string> tokenize_keep_quotes(const std::string& line) {
        std::vector<std::string> tokens;
        std::string cur;
        char quote = '\0';

        for (char ch : line) {
            if (quote != '\0') {
                cur.push_back(ch);
                if (ch == quote) quote = '\0';
                continue;
            }

            if (ch == '"' || ch == '\'') {
                quote = ch;
                cur.push_back(ch);
                continue;
            }

            if (std::isspace(static_cast<unsigned char>(ch))) {
                if (!cur.empty()) {
                    tokens.push_back(cur);
                    cur.clear();
                }
                continue;
            }

            cur.push_back(ch);
        }

        if (!cur.empty()) tokens.push_back(cur);
        return tokens;
    }

    static std::unordered_map<std::string, std::string> parse_params(const std::string& line) {
        std::unordered_map<std::string, std::string> p;
        auto tokens = tokenize_keep_quotes(line);
        if (tokens.empty()) return p;

        for (size_t i = 1; i < tokens.size(); ++i) {
            const std::string& token = tokens[i];
            if (token.rfind("-", 0) != 0) continue;

            auto eq = token.find('=');
            if (eq == std::string::npos) {
                p[tolower_str(token.substr(1))] = "true";
            } else {
                std::string key = tolower_str(token.substr(1, eq - 1));
                std::string val = token.substr(eq + 1);
                p[key] = strip_quotes(val);
            }
        }
        return p;
    }

    static bool directory_has_free_entry(Ext2Paths& fs, const SuperBlock& sb, const Inode& parent) {
        for (int b = 0; b < 12; ++b) {
            if (parent.i_block[b] < 0) continue;
            FolderBlock fb = fs.readFolderBlock(sb, parent.i_block[b]);
            for (int i = 0; i < 4; ++i) {
                if (fb.b_content[i].b_inodo < 0) return true;
            }
        }
        return false;
    }
}

std::string MkFileCmd::exec(const std::string& line) {
    auto params = parse_params(line);

    if (!params.count("id"))   return "ERROR: mkfile -> falta -id\n";
    if (!params.count("path")) return "ERROR: mkfile -> falta -path\n";

    const std::string id = params["id"];
    const std::string path = params["path"];
    const std::string content = params.count("cont") ? params["cont"] : "";

    if (path.empty() || path[0] != '/') {
        return "ERROR: mkfile -> -path debe ser absoluta e iniciar con '/'\n";
    }
    if (path == "/") {
        return "ERROR: mkfile -> ruta inválida\n";
    }

    MountEntry mounted;
    if (!MountManager::instance().getById(id, mounted)) {
        return "ERROR: mkfile -> no existe montaje con id=" + id + "\n";
    }

    Ext2Paths fs(mounted.path, mounted.start);
    SuperBlock sb = fs.loadSuper();

    if (sb.s_magic != 0xEF53) {
        return "ERROR: mkfile -> la partición no está formateada como EXT2 (ejecute mkfs)\n";
    }

    auto parts = Ext2Paths::splitPath(path);
    if (parts.empty()) return "ERROR: mkfile -> ruta inválida\n";

    const std::string fileName = parts.back();
    if (fileName.empty() || fileName.size() > 11 || fileName == "." || fileName == "..") {
        return "ERROR: mkfile -> nombre de archivo inválido (máximo 11 caracteres)\n";
    }

    std::string parentPath = "/";
    if (parts.size() > 1) {
        parentPath.clear();
        for (size_t i = 0; i + 1 < parts.size(); ++i) {
            parentPath += "/" + parts[i];
        }
    }

    int32_t parentIdx = fs.resolvePath(sb, parentPath);
    if (parentIdx < 0) {
        return "ERROR: mkfile -> no existe la carpeta padre\n";
    }

    Inode parent = fs.readInode(sb, parentIdx);
    if (parent.i_type != '0') {
        return "ERROR: mkfile -> la ruta padre no es carpeta\n";
    }

    if (fs.findInDirectory(sb, parentIdx, fileName) >= 0) {
        return "ERROR: mkfile -> ya existe: " + path + "\n";
    }

    const int32_t blocksNeeded = std::max<int32_t>(1, static_cast<int32_t>((content.size() + 63) / 64));
    if (blocksNeeded > 12) {
        return "ERROR: mkfile -> contenido demasiado grande (máximo 12 bloques directos)\n";
    }

    bool parentNeedsNewBlock = !directory_has_free_entry(fs, sb, parent);
    if (parentNeedsNewBlock) {
        bool hasDirectPtrSlot = false;
        for (int b = 0; b < 12; ++b) {
            if (parent.i_block[b] < 0) { hasDirectPtrSlot = true; break; }
        }
        if (!hasDirectPtrSlot) {
            return "ERROR: mkfile -> carpeta padre sin espacio\n";
        }
    }

    int32_t totalBlocksNeeded = blocksNeeded + (parentNeedsNewBlock ? 1 : 0);
    if (sb.s_free_inodes_count <= 0) {
        return "ERROR: mkfile -> no hay inodos libres\n";
    }
    if (sb.s_free_blocks_count < totalBlocksNeeded) {
        return "ERROR: mkfile -> no hay bloques libres\n";
    }

    int32_t inodeIdx = fs.allocateInode(sb);
    if (inodeIdx < 0) return "ERROR: mkfile -> no hay inodos libres\n";

    Inode fileInode{};
    fileInode.i_uid = 1;
    fileInode.i_gid = 1;
    fileInode.i_size = static_cast<int32_t>(content.size());
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    fileInode.i_atime = now;
    fileInode.i_ctime = now;
    fileInode.i_mtime = now;
    for (int i = 0; i < 15; ++i) fileInode.i_block[i] = -1;
    fileInode.i_type = '1';
    fileInode.i_perm = 664;

    for (int32_t i = 0; i < blocksNeeded; ++i) {
        int32_t blkIdx = fs.allocateBlock(sb);
        if (blkIdx < 0) return "ERROR: mkfile -> no hay bloques libres\n";
        fileInode.i_block[i] = blkIdx;

        FileBlock fb{};
        std::memset(fb.b_content, 0, sizeof(fb.b_content));

        const size_t start = static_cast<size_t>(i) * 64;
        if (start < content.size()) {
            const size_t len = std::min<size_t>(64, content.size() - start);
            std::memcpy(fb.b_content, content.data() + start, len);
        }

        fs.writeFileBlock(sb, blkIdx, fb);
    }

    fs.writeInode(sb, inodeIdx, fileInode);

    if (!fs.addDirEntry(sb, parentIdx, fileName, inodeIdx)) {
        return "ERROR: mkfile -> carpeta padre sin espacio\n";
    }

    fs.writeSuper(sb);
    return "OK: mkfile -> archivo creado: " + path + "\n";
}
