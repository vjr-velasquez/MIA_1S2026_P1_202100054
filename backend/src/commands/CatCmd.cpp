#include "commands/CatCmd.h"
#include "disk/MountManager.h"
#include "fs/Ext2Paths.h"
#include "fs/FsFileOps.h"
#include "session/SessionManager.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
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

    static std::unordered_map<std::string, std::vector<std::string>> parse_params_multi(const std::string& line) {
        std::unordered_map<std::string, std::vector<std::string>> p;
        auto tokens = tokenize_keep_quotes(line);
        if (tokens.empty()) return p;

        for (size_t i = 1; i < tokens.size(); ++i) {
            const std::string& token = tokens[i];
            if (token.rfind("-", 0) != 0) continue;

            auto eq = token.find('=');
            if (eq == std::string::npos) {
                p[tolower_str(token.substr(1))].push_back("true");
            } else {
                std::string key = tolower_str(token.substr(1, eq - 1));
                std::string val = strip_quotes(token.substr(eq + 1));
                p[key].push_back(val);
            }
        }
        return p;
    }
}

std::string CatCmd::exec(const std::string& line) {
    auto params = parse_params_multi(line);

    std::vector<std::string> paths;
    if (params.count("path")) {
        paths = params["path"];
    } else {
        for (const auto& entry : params) {
            if (entry.first.rfind("file", 0) == 0) {
                paths.insert(paths.end(), entry.second.begin(), entry.second.end());
            }
        }
    }
    if (paths.empty()) {
        return "ERROR: cat -> falta -path\n";
    }

    std::string id;
    if (params.count("id") && !params["id"].empty()) {
        id = params["id"].back();
    } else if (SessionManager::instance().isActive()) {
        id = SessionManager::instance().current().id;
    } else {
        return "ERROR: cat -> falta -id\n";
    }

    MountEntry mounted;
    if (!MountManager::instance().getById(id, mounted)) {
        return "ERROR: cat -> no existe montaje con id=" + id + "\n";
    }

    Ext2Paths fs(mounted.path, mounted.start);
    SuperBlock sb = fs.loadSuper();
    if (sb.s_magic != 0xEF53) {
        return "ERROR: cat -> la partición no está formateada como EXT2 (ejecute mkfs)\n";
    }

    std::ostringstream out;
    for (size_t pi = 0; pi < paths.size(); ++pi) {
        const std::string& path = paths[pi];
        if (path.empty() || path[0] != '/') {
            return "ERROR: cat -> -path debe ser absoluta e iniciar con '/'\n";
        }

        std::string content;
        int32_t inodeIdx = -1;
        if (!FsFileOps::readFile(fs, sb, path, content, &inodeIdx)) {
            return "ERROR: cat -> no existe: " + path + "\n";
        }

        Inode fileInode = fs.readInode(sb, inodeIdx);
        fileInode.i_atime = static_cast<int64_t>(std::time(nullptr));
        fs.writeInode(sb, inodeIdx, fileInode);

        if (paths.size() > 1) {
            out << "OK: cat -> " << path << "\n";
        } else {
            out << "OK: cat\n";
        }
        out << content << "\n";

        if (pi + 1 < paths.size()) out << "\n";
    }

    return out.str();
}
