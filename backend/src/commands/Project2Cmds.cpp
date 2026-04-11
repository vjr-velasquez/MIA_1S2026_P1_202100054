#include "commands/Project2Cmds.h"

#include "disk/MountManager.h"
#include "fs/Ext2Paths.h"
#include "fs/FsFileOps.h"
#include "fs/JournalManager.h"
#include "session/SessionManager.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstring>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

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
            if ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')) {
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

    static bool resolve_active_fs(Ext2Paths*& fsPtr, SuperBlock& sb, MountEntry& mounted, std::string& error) {
        if (!SessionManager::instance().isActive()) {
            error = "no hay una sesión activa";
            return false;
        }
        if (!MountManager::instance().getById(SessionManager::instance().current().id, mounted)) {
            error = "no existe el montaje de la sesión";
            return false;
        }

        fsPtr = new Ext2Paths(mounted.path, mounted.start);
        sb = fsPtr->loadSuper();
        if (sb.s_magic != 0xEF53) {
            delete fsPtr;
            fsPtr = nullptr;
            error = "la partición no está formateada como EXT2/EXT3";
            return false;
        }
        return true;
    }

    static int perm_digit_for(const Inode& inode) {
        const auto& session = SessionManager::instance().current();
        int owner = (inode.i_perm / 100) % 10;
        int group = (inode.i_perm / 10) % 10;
        int other = inode.i_perm % 10;

        if (session.username == "root") return 7;
        if (session.uid == inode.i_uid) return owner;
        if (session.gid == inode.i_gid) return group;
        return other;
    }

    static bool can_read(const Inode& inode) { return (perm_digit_for(inode) & 4) != 0; }
    static bool can_write(const Inode& inode) { return (perm_digit_for(inode) & 2) != 0; }

    static std::string parent_path_of(const std::string& path) {
        if (path == "/") return "";
        auto parts = Ext2Paths::splitPath(path);
        if (parts.empty()) return "";
        if (parts.size() == 1) return "/";

        std::string parent;
        for (size_t i = 0; i + 1 < parts.size(); ++i) parent += "/" + parts[i];
        return parent.empty() ? "/" : parent;
    }

    static std::string name_of(const std::string& path) {
        return Ext2Paths::basename(path);
    }

    static bool create_directory_node(Ext2Paths& fs, SuperBlock& sb, int32_t parentIdx, const std::string& name, const Inode& templateNode, int32_t& outIdx, std::string& error) {
        int32_t inodeIdx = fs.allocateInode(sb);
        if (inodeIdx < 0) { error = "no hay inodos libres"; return false; }

        int32_t blockIdx = fs.allocateBlock(sb);
        if (blockIdx < 0) { error = "no hay bloques libres"; return false; }

        Inode dir = templateNode;
        for (int i = 0; i < 15; ++i) dir.i_block[i] = -1;
        dir.i_size = 0;
        dir.i_block[0] = blockIdx;
        dir.i_type = '0';
        dir.i_ctime = dir.i_mtime = dir.i_atime = static_cast<int64_t>(std::time(nullptr));
        fs.writeInode(sb, inodeIdx, dir);

        FolderBlock fb{};
        std::memset(&fb, 0, sizeof(FolderBlock));
        std::strncpy(fb.b_content[0].b_name, ".", 11);
        fb.b_content[0].b_inodo = inodeIdx;
        std::strncpy(fb.b_content[1].b_name, "..", 11);
        fb.b_content[1].b_inodo = parentIdx;
        fb.b_content[2].b_name[0] = '\0'; fb.b_content[2].b_inodo = -1;
        fb.b_content[3].b_name[0] = '\0'; fb.b_content[3].b_inodo = -1;
        fs.writeFolderBlock(sb, blockIdx, fb);

        if (!fs.addDirEntry(sb, parentIdx, name, inodeIdx)) {
            error = "carpeta padre sin espacio";
            return false;
        }

        outIdx = inodeIdx;
        return true;
    }

    static bool create_file_node(Ext2Paths& fs, SuperBlock& sb, int32_t parentIdx, const std::string& name, const Inode& templateNode, const std::string& content, int32_t& outIdx, std::string& error) {
        if (name.size() > 11) { error = "nombre inválido"; return false; }
        int32_t inodeIdx = fs.allocateInode(sb);
        if (inodeIdx < 0) { error = "no hay inodos libres"; return false; }

        Inode file = templateNode;
        for (int i = 0; i < 15; ++i) file.i_block[i] = -1;
        file.i_type = '1';
        file.i_size = 0;
        file.i_ctime = file.i_mtime = file.i_atime = static_cast<int64_t>(std::time(nullptr));
        fs.writeInode(sb, inodeIdx, file);

        if (!fs.addDirEntry(sb, parentIdx, name, inodeIdx)) {
            error = "carpeta padre sin espacio";
            return false;
        }

        if (!FsFileOps::writeFile(fs, sb, inodeIdx, content, error)) return false;
        outIdx = inodeIdx;
        return true;
    }

    static bool delete_inode_recursive(Ext2Paths& fs, SuperBlock& sb, int32_t inodeIdx, std::string& error) {
        Inode inode = fs.readInode(sb, inodeIdx);

        if (inode.i_type == '0') {
            auto entries = fs.listDirectory(sb, inodeIdx);
            for (const auto& entry : entries) {
                if (entry.name == "." || entry.name == "..") continue;
                if (!delete_inode_recursive(fs, sb, entry.inodeIndex, error)) return false;
            }
        }

        for (int i = 0; i < 12; ++i) {
            if (inode.i_block[i] >= 0) fs.freeBlock(sb, inode.i_block[i]);
        }
        fs.freeInode(sb, inodeIdx);
        return true;
    }

    static bool wildcard_match(const std::string& pattern, const std::string& text, size_t pi = 0, size_t ti = 0) {
        if (pi == pattern.size()) return ti == text.size();
        if (pattern[pi] == '*') {
            for (size_t skip = ti; skip <= text.size(); ++skip) {
                if (wildcard_match(pattern, text, pi + 1, skip)) return true;
            }
            return false;
        }
        if (ti == text.size()) return false;
        if (pattern[pi] == '?') return wildcard_match(pattern, text, pi + 1, ti + 1);
        if (pattern[pi] == text[ti]) return wildcard_match(pattern, text, pi + 1, ti + 1);
        return false;
    }

    static bool clone_inode_recursive(Ext2Paths& fs, SuperBlock& sb, int32_t srcIdx, int32_t destParentIdx, const std::string& newName, std::string& error) {
        Inode src = fs.readInode(sb, srcIdx);
        if (!can_read(src)) return true;

        if (src.i_type == '1') {
            std::string content;
            if (!FsFileOps::readFile(fs, sb, "/__dummy__", content, nullptr)) {
                // no-op fallback
            }
            content.clear();
            int32_t remaining = src.i_size;
            for (int b = 0; b < 12 && remaining > 0; ++b) {
                if (src.i_block[b] < 0) break;
                FileBlock fb = fs.readFileBlock(sb, src.i_block[b]);
                int32_t chunk = std::min<int32_t>(remaining, 64);
                content.append(fb.b_content, fb.b_content + chunk);
                remaining -= chunk;
            }

            int32_t newIdx = -1;
            return create_file_node(fs, sb, destParentIdx, newName, src, content, newIdx, error);
        }

        int32_t newDirIdx = -1;
        if (!create_directory_node(fs, sb, destParentIdx, newName, src, newDirIdx, error)) return false;

        auto entries = fs.listDirectory(sb, srcIdx);
        for (const auto& entry : entries) {
            if (entry.name == "." || entry.name == "..") continue;
            if (!clone_inode_recursive(fs, sb, entry.inodeIndex, newDirIdx, entry.name, error)) return false;
        }
        return true;
    }
}

std::string UnmountCmd::exec(const std::string& line) {
    auto params = parse_params(line);
    if (!params.count("id")) return "ERROR: unmount -> falta -id\n";

    const std::string id = params["id"];
    if (SessionManager::instance().isActive() && SessionManager::instance().current().id == id) {
        SessionManager::instance().clear();
    }
    return MountManager::instance().unmount(id);
}

std::string RemoveCmd::exec(const std::string& line) {
    auto params = parse_params(line);
    if (!params.count("path")) return "ERROR: remove -> falta -path\n";

    Ext2Paths* fs = nullptr;
    SuperBlock sb{};
    MountEntry mounted{};
    std::string error;
    if (!resolve_active_fs(fs, sb, mounted, error)) return "ERROR: remove -> " + error + "\n";

    const std::string path = params["path"];
    const std::string parentPath = parent_path_of(path);
    const std::string name = name_of(path);
    int32_t parentIdx = fs->resolvePath(sb, parentPath);
    int32_t inodeIdx = fs->resolvePath(sb, path);
    if (parentIdx < 0 || inodeIdx < 0 || name.empty()) {
        delete fs;
        return "ERROR: remove -> no existe la ruta\n";
    }

    Inode parent = fs->readInode(sb, parentIdx);
    Inode target = fs->readInode(sb, inodeIdx);
    if (!can_write(parent) || !can_write(target)) {
        delete fs;
        return "ERROR: remove -> permisos insuficientes\n";
    }

    if (!delete_inode_recursive(*fs, sb, inodeIdx, error) || !fs->removeDirEntry(sb, parentIdx, name)) {
        delete fs;
        return "ERROR: remove -> no se pudo eliminar\n";
    }

    fs->writeSuper(sb);
    JournalManager::append(mounted.path, mounted.start, sb, "remove", path, "");
    delete fs;
    return "OK: remove -> eliminado: " + path + "\n";
}

std::string RenameCmd::exec(const std::string& line) {
    auto params = parse_params(line);
    if (!params.count("path")) return "ERROR: rename -> falta -path\n";
    if (!params.count("name")) return "ERROR: rename -> falta -name\n";

    Ext2Paths* fs = nullptr;
    SuperBlock sb{};
    MountEntry mounted{};
    std::string error;
    if (!resolve_active_fs(fs, sb, mounted, error)) return "ERROR: rename -> " + error + "\n";

    const std::string path = params["path"];
    const std::string parentPath = parent_path_of(path);
    const std::string oldName = name_of(path);
    const std::string newName = params["name"];
    int32_t parentIdx = fs->resolvePath(sb, parentPath);
    int32_t inodeIdx = fs->resolvePath(sb, path);
    if (parentIdx < 0 || inodeIdx < 0 || oldName.empty()) {
        delete fs;
        return "ERROR: rename -> no existe la ruta\n";
    }
    if (fs->findInDirectory(sb, parentIdx, newName) >= 0) {
        delete fs;
        return "ERROR: rename -> ya existe una entrada con ese nombre\n";
    }

    Inode target = fs->readInode(sb, inodeIdx);
    if (!can_write(target)) {
        delete fs;
        return "ERROR: rename -> permisos insuficientes\n";
    }

    bool ok = fs->renameDirEntry(sb, parentIdx, oldName, newName);
    if (ok) JournalManager::append(mounted.path, mounted.start, sb, "rename", path, newName);
    delete fs;
    if (!ok) return "ERROR: rename -> no se pudo cambiar el nombre\n";
    return "OK: rename -> " + path + " -> " + newName + "\n";
}

std::string CopyCmd::exec(const std::string& line) {
    auto params = parse_params(line);
    if (!params.count("path")) return "ERROR: copy -> falta -path\n";
    if (!params.count("destino")) return "ERROR: copy -> falta -destino\n";

    Ext2Paths* fs = nullptr;
    SuperBlock sb{};
    MountEntry mounted{};
    std::string error;
    if (!resolve_active_fs(fs, sb, mounted, error)) return "ERROR: copy -> " + error + "\n";

    const std::string srcPath = params["path"];
    const std::string dstPath = params["destino"];
    const std::string name = name_of(srcPath);
    int32_t srcIdx = fs->resolvePath(sb, srcPath);
    int32_t dstIdx = fs->resolvePath(sb, dstPath);
    if (srcIdx < 0) { delete fs; return "ERROR: copy -> no existe la ruta origen\n"; }
    if (dstIdx < 0) { delete fs; return "ERROR: copy -> no existe la carpeta destino\n"; }
    if (fs->findInDirectory(sb, dstIdx, name) >= 0) { delete fs; return "ERROR: copy -> ya existe una entrada con ese nombre en destino\n"; }

    Inode dst = fs->readInode(sb, dstIdx);
    if (dst.i_type != '0') { delete fs; return "ERROR: copy -> el destino no es una carpeta\n"; }
    if (!can_write(dst)) { delete fs; return "ERROR: copy -> permisos insuficientes en destino\n"; }

    if (!clone_inode_recursive(*fs, sb, srcIdx, dstIdx, name, error)) {
        delete fs;
        return "ERROR: copy -> " + error + "\n";
    }

    fs->writeSuper(sb);
    JournalManager::append(mounted.path, mounted.start, sb, "copy", srcPath, dstPath);
    delete fs;
    return "OK: copy -> copiado a: " + dstPath + "\n";
}

std::string MoveCmd::exec(const std::string& line) {
    auto params = parse_params(line);
    if (!params.count("path")) return "ERROR: move -> falta -path\n";
    if (!params.count("destino")) return "ERROR: move -> falta -destino\n";

    Ext2Paths* fs = nullptr;
    SuperBlock sb{};
    MountEntry mounted{};
    std::string error;
    if (!resolve_active_fs(fs, sb, mounted, error)) return "ERROR: move -> " + error + "\n";

    const std::string srcPath = params["path"];
    const std::string dstPath = params["destino"];
    const std::string srcParentPath = parent_path_of(srcPath);
    const std::string name = name_of(srcPath);
    int32_t srcIdx = fs->resolvePath(sb, srcPath);
    int32_t srcParentIdx = fs->resolvePath(sb, srcParentPath);
    int32_t dstIdx = fs->resolvePath(sb, dstPath);
    if (srcIdx < 0 || srcParentIdx < 0) { delete fs; return "ERROR: move -> no existe la ruta origen\n"; }
    if (dstIdx < 0) { delete fs; return "ERROR: move -> no existe la carpeta destino\n"; }
    if (fs->findInDirectory(sb, dstIdx, name) >= 0) { delete fs; return "ERROR: move -> ya existe una entrada con ese nombre en destino\n"; }

    Inode src = fs->readInode(sb, srcIdx);
    Inode srcParent = fs->readInode(sb, srcParentIdx);
    Inode dst = fs->readInode(sb, dstIdx);
    if (!can_write(src) || !can_write(srcParent) || !can_write(dst)) {
        delete fs;
        return "ERROR: move -> permisos insuficientes\n";
    }

    if (!fs->removeDirEntry(sb, srcParentIdx, name) || !fs->addDirEntry(sb, dstIdx, name, srcIdx)) {
        delete fs;
        return "ERROR: move -> no se pudo mover\n";
    }

    if (src.i_type == '0') {
        auto entries = fs->listDirectory(sb, srcIdx);
        for (const auto& entry : entries) {
            if (entry.name != "..") continue;
            FolderBlock fb = fs->readFolderBlock(sb, src.i_block[entry.blockPtrSlot]);
            fb.b_content[entry.contentSlot].b_inodo = dstIdx;
            fs->writeFolderBlock(sb, src.i_block[entry.blockPtrSlot], fb);
            break;
        }
    }

    fs->writeSuper(sb);
    JournalManager::append(mounted.path, mounted.start, sb, "move", srcPath, dstPath);
    delete fs;
    return "OK: move -> movido a: " + dstPath + "\n";
}

std::string FindCmd::exec(const std::string& line) {
    auto params = parse_params(line);
    if (!params.count("path")) return "ERROR: find -> falta -path\n";
    if (!params.count("name")) return "ERROR: find -> falta -name\n";

    Ext2Paths* fs = nullptr;
    SuperBlock sb{};
    MountEntry mounted{};
    std::string error;
    if (!resolve_active_fs(fs, sb, mounted, error)) return "ERROR: find -> " + error + "\n";

    const std::string startPath = params["path"];
    const std::string pattern = params["name"];
    int32_t startIdx = fs->resolvePath(sb, startPath);
    if (startIdx < 0) { delete fs; return "ERROR: find -> no existe la ruta\n"; }

    std::ostringstream out;
    std::function<void(int32_t,const std::string&,int)> walk = [&](int32_t inodeIdx, const std::string& path, int depth) {
        Inode inode = fs->readInode(sb, inodeIdx);
        if (!can_read(inode)) return;

        std::string currentName = path == "/" ? "/" : name_of(path);
        if (wildcard_match(pattern, currentName)) {
            for (int i = 0; i < depth; ++i) out << "  ";
            out << path << "\n";
        }

        if (inode.i_type != '0') return;
        auto entries = fs->listDirectory(sb, inodeIdx);
        for (const auto& entry : entries) {
            if (entry.name == "." || entry.name == "..") continue;
            const std::string childPath = path == "/" ? "/" + entry.name : path + "/" + entry.name;
            walk(entry.inodeIndex, childPath, depth + 1);
        }
    };

    walk(startIdx, startPath, 0);
    delete fs;
    std::string result = out.str();
    if (result.empty()) return "OK: find -> sin resultados\n";
    return "OK: find\n" + result;
}
