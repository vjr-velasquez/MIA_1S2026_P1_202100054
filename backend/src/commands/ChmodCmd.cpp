#include "commands/ChmodCmd.h"
#include "disk/MountManager.h"
#include "fs/Ext2Paths.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
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

    static bool is_perm_valid(const std::string& p) {
        if (p.size() != 3) return false;
        for (char c : p) {
            if (c < '0' || c > '7') return false;
        }
        return true;
    }
}

std::string ChmodCmd::exec(const std::string& line) {
    auto params = parse_params(line);

    if (!params.count("id"))   return "ERROR: chmod -> falta -id\n";
    if (!params.count("path")) return "ERROR: chmod -> falta -path\n";
    if (!params.count("perm")) return "ERROR: chmod -> falta -perm\n";

    const std::string id = params["id"];
    const std::string path = params["path"];
    const std::string permRaw = params["perm"];

    if (path.empty() || path[0] != '/') {
        return "ERROR: chmod -> -path debe ser absoluta e iniciar con '/'\n";
    }
    if (!is_perm_valid(permRaw)) {
        return "ERROR: chmod -> -perm inválido (use formato ### octal, ej: 664)\n";
    }

    int perm = 0;
    try {
        perm = std::stoi(permRaw);
    } catch (...) {
        return "ERROR: chmod -> -perm inválido\n";
    }

    MountEntry mounted;
    if (!MountManager::instance().getById(id, mounted)) {
        return "ERROR: chmod -> no existe montaje con id=" + id + "\n";
    }

    Ext2Paths fs(mounted.path, mounted.start);
    SuperBlock sb = fs.loadSuper();
    if (sb.s_magic != 0xEF53) {
        return "ERROR: chmod -> la partición no está formateada como EXT2 (ejecute mkfs)\n";
    }

    int32_t inodeIdx = fs.resolvePath(sb, path);
    if (inodeIdx < 0) {
        return "ERROR: chmod -> no existe: " + path + "\n";
    }

    Inode inode = fs.readInode(sb, inodeIdx);
    inode.i_perm = perm;
    inode.i_mtime = static_cast<int64_t>(std::time(nullptr));
    fs.writeInode(sb, inodeIdx, inode);

    return "OK: chmod -> permisos actualizados: " + path + " perm=" + std::to_string(perm) + "\n";
}
