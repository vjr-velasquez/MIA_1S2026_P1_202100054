#include "commands/ChownCmd.h"
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

    static bool is_int(const std::string& v) {
        if (v.empty()) return false;
        size_t start = (v[0] == '-') ? 1 : 0;
        if (start >= v.size()) return false;
        for (size_t i = start; i < v.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(v[i]))) return false;
        }
        return true;
    }
}

std::string ChownCmd::exec(const std::string& line) {
    auto params = parse_params(line);

    if (!params.count("id"))   return "ERROR: chown -> falta -id\n";
    if (!params.count("path")) return "ERROR: chown -> falta -path\n";
    if (!params.count("uid"))  return "ERROR: chown -> falta -uid\n";

    const std::string id = params["id"];
    const std::string path = params["path"];
    const std::string uidRaw = params["uid"];
    const bool hasGid = params.count("gid");
    const std::string gidRaw = hasGid ? params["gid"] : "";

    if (path.empty() || path[0] != '/') {
        return "ERROR: chown -> -path debe ser absoluta e iniciar con '/'\n";
    }
    if (!is_int(uidRaw)) {
        return "ERROR: chown -> -uid inválido\n";
    }
    if (hasGid && !is_int(gidRaw)) {
        return "ERROR: chown -> -gid inválido\n";
    }

    int uid = 0;
    int gid = 0;
    try {
        uid = std::stoi(uidRaw);
        gid = hasGid ? std::stoi(gidRaw) : 0;
    } catch (...) {
        return "ERROR: chown -> uid/gid inválidos\n";
    }

    MountEntry mounted;
    if (!MountManager::instance().getById(id, mounted)) {
        return "ERROR: chown -> no existe montaje con id=" + id + "\n";
    }

    Ext2Paths fs(mounted.path, mounted.start);
    SuperBlock sb = fs.loadSuper();
    if (sb.s_magic != 0xEF53) {
        return "ERROR: chown -> la partición no está formateada como EXT2 (ejecute mkfs)\n";
    }

    int32_t inodeIdx = fs.resolvePath(sb, path);
    if (inodeIdx < 0) {
        return "ERROR: chown -> no existe: " + path + "\n";
    }

    Inode inode = fs.readInode(sb, inodeIdx);
    inode.i_uid = uid;
    if (hasGid) inode.i_gid = gid;
    inode.i_mtime = static_cast<int64_t>(std::time(nullptr));
    fs.writeInode(sb, inodeIdx, inode);

    const int outGid = hasGid ? gid : inode.i_gid;
    std::ostringstream out;
    out << "OK: chown -> propietario actualizado: " << path
        << " uid=" << uid << " gid=" << outGid << "\n";
    return out.str();
}
