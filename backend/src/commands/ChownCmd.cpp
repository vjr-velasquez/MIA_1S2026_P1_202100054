#include "commands/ChownCmd.h"
#include "disk/MountManager.h"
#include "fs/Ext2Paths.h"
#include "fs/FsFileOps.h"
#include "fs/JournalManager.h"
#include "session/SessionManager.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
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

    struct UserInfo {
        int uid = -1;
        int gid = -1;
        std::string user;
    };

    static std::vector<std::string> split_csv(const std::string& line) {
        std::vector<std::string> cols;
        std::stringstream ss(line);
        std::string item;
        while (std::getline(ss, item, ',')) cols.push_back(trim(item));
        return cols;
    }

    static bool load_user_info(Ext2Paths& fs, const SuperBlock& sb, const std::string& username, UserInfo& out) {
        std::string content;
        if (!FsFileOps::readFile(fs, sb, "/users.txt", content, nullptr)) return false;

        std::unordered_map<std::string, int> groups;
        std::istringstream in(content);
        std::string line;
        while (std::getline(in, line)) {
            auto cols = split_csv(line);
            if (cols.size() == 3 && cols[1] == "G" && cols[0] != "0") {
                groups[cols[2]] = std::stoi(cols[0]);
            }
        }

        in.clear();
        in.seekg(0);
        while (std::getline(in, line)) {
            auto cols = split_csv(line);
            if (cols.size() == 5 && cols[1] == "U" && cols[0] != "0" && cols[3] == username) {
                out.uid = std::stoi(cols[0]);
                out.user = cols[3];
                auto it = groups.find(cols[2]);
                out.gid = (it != groups.end()) ? it->second : -1;
                return true;
            }
        }
        return false;
    }
}

std::string ChownCmd::exec(const std::string& line) {
    auto params = parse_params(line);

    if (!params.count("path")) return "ERROR: chown -> falta -path\n";
    if (!params.count("uid") && !params.count("usuario"))  return "ERROR: chown -> falta -usuario\n";

    std::string id;
    if (params.count("id")) id = params["id"];
    else if (SessionManager::instance().isActive()) id = SessionManager::instance().current().id;
    else return "ERROR: chown -> falta -id\n";

    const std::string path = params["path"];
    const bool recursive = params.count("r") && params["r"] == "true";

    if (path.empty() || path[0] != '/') {
        return "ERROR: chown -> -path debe ser absoluta e iniciar con '/'\n";
    }

    MountEntry mounted;
    if (!MountManager::instance().getById(id, mounted)) {
        return "ERROR: chown -> no existe montaje con id=" + id + "\n";
    }
    if (!SessionManager::instance().isActive() || SessionManager::instance().current().id != id) {
        return "ERROR: chown -> requiere una sesión activa sobre la partición\n";
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

    UserInfo targetUser{};
    if (params.count("usuario")) {
        if (!load_user_info(fs, sb, params["usuario"], targetUser)) {
            return "ERROR: chown -> no existe el usuario\n";
        }
    } else {
        targetUser.uid = std::stoi(params["uid"]);
        targetUser.gid = params.count("gid") ? std::stoi(params["gid"]) : -1;
        targetUser.user = std::to_string(targetUser.uid);
    }

    const auto& session = SessionManager::instance().current();
    std::function<bool(int32_t)> applyOwner = [&](int32_t idx) {
        Inode inode = fs.readInode(sb, idx);
        if (session.username != "root" && inode.i_uid != session.uid) {
            return false;
        }

        inode.i_uid = targetUser.uid;
        if (targetUser.gid >= 0) inode.i_gid = targetUser.gid;
        inode.i_mtime = static_cast<int64_t>(std::time(nullptr));
        fs.writeInode(sb, idx, inode);

        if (!recursive || inode.i_type != '0') return true;
        auto entries = fs.listDirectory(sb, idx);
        for (const auto& entry : entries) {
            if (entry.name == "." || entry.name == "..") continue;
            applyOwner(entry.inodeIndex);
        }
        return true;
    };

    if (!applyOwner(inodeIdx)) {
        return "ERROR: chown -> permisos insuficientes\n";
    }

    std::ostringstream out;
    JournalManager::append(mounted.path, mounted.start, sb, "chown", path, targetUser.user);
    out << "OK: chown -> propietario actualizado: " << path
        << " uid=" << targetUser.uid << " gid=" << targetUser.gid << "\n";
    return out.str();
}
