#include "commands/MkFsCmd.h"
#include "disk/MountManager.h"
#include "fs/Ext2Formatter.h"

#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>

namespace {
    static inline std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    static inline std::string tolower_str(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
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

    static std::unordered_map<std::string, std::string> parse_params(const std::string& line) {
        std::unordered_map<std::string, std::string> p;
        std::istringstream iss(line);
        std::string token;

        // saltar comando
        iss >> token;

        while (iss >> token) {
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
}

std::string MkFsCmd::exec(const std::string& line) {
    auto params = parse_params(line);

    if (!params.count("id")) {
        return "ERROR: mkfs -> falta -id\n";
    }

    std::string id = params["id"];
    int fsType = 2;
    if (params.count("fs")) {
        std::string fs = tolower_str(params["fs"]);
        if (fs == "2fs") fsType = 2;
        else if (fs == "3fs") fsType = 3;
        else return "ERROR: mkfs -> -fs debe ser 2fs o 3fs\n";
    }

    MountEntry mounted;
    if (!MountManager::instance().getById(id, mounted)) {
        return "ERROR: mkfs -> no existe montaje con id=" + id + "\n";
    }

    Ext2Formatter formatter;
    return formatter.format(mounted.path, mounted.start, mounted.size, fsType);
}
