#include "commands/Ext3Cmds.h"

#include "disk/MountManager.h"
#include "fs/Ext2IO.h"
#include "fs/Ext2Paths.h"
#include "fs/JournalManager.h"

#include <algorithm>
#include <cctype>
#include <sstream>
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

    static std::unordered_map<std::string, std::string> parse_params(const std::string& line) {
        std::unordered_map<std::string, std::string> p;
        std::istringstream iss(line);
        std::string token;
        iss >> token;

        while (iss >> token) {
            if (token.rfind("-", 0) != 0) continue;
            auto eq = token.find('=');
            if (eq == std::string::npos) p[tolower_str(token.substr(1))] = "true";
            else p[tolower_str(token.substr(1, eq - 1))] = strip_quotes(token.substr(eq + 1));
        }
        return p;
    }
}

std::string LossCmd::exec(const std::string& line) {
    auto params = parse_params(line);
    if (!params.count("id")) return "ERROR: loss -> falta -id\n";

    MountEntry mounted{};
    if (!MountManager::instance().getById(params["id"], mounted)) {
        return "ERROR: loss -> no existe montaje con id=" + params["id"] + "\n";
    }

    Ext2Paths fs(mounted.path, mounted.start);
    SuperBlock sb = fs.loadSuper();
    if (!JournalManager::isExt3(sb)) return "ERROR: loss -> la partición no es EXT3\n";

    Ext2IO io(mounted.path);
    std::vector<char> zeros;

    zeros.assign(static_cast<size_t>(sb.s_inodes_count), '\0');
    io.writeBytes(sb.s_bm_inode_start, zeros.data(), zeros.size());

    zeros.assign(static_cast<size_t>(sb.s_blocks_count), '\0');
    io.writeBytes(sb.s_bm_block_start, zeros.data(), zeros.size());

    zeros.assign(static_cast<size_t>(sb.s_inodes_count) * sizeof(Inode), '\0');
    io.writeBytes(sb.s_inode_start, zeros.data(), zeros.size());

    zeros.assign(static_cast<size_t>(sb.s_blocks_count) * sizeof(FileBlock), '\0');
    io.writeBytes(sb.s_block_start, zeros.data(), zeros.size());

    return "OK: loss -> sistema simulado como perdido: " + params["id"] + "\n";
}

std::string JournalingCmd::exec(const std::string& line) {
    auto params = parse_params(line);
    if (!params.count("id")) return "ERROR: journaling -> falta -id\n";

    MountEntry mounted{};
    if (!MountManager::instance().getById(params["id"], mounted)) {
        return "ERROR: journaling -> no existe montaje con id=" + params["id"] + "\n";
    }

    Ext2Paths fs(mounted.path, mounted.start);
    SuperBlock sb = fs.loadSuper();
    if (!JournalManager::isExt3(sb)) return "ERROR: journaling -> la partición no es EXT3\n";

    auto entries = JournalManager::readAll(mounted.path, mounted.start, sb);
    std::ostringstream out;
    out << "OK: journaling\n";
    if (entries.empty()) {
        out << "(sin entradas)\n";
        return out.str();
    }

    for (const auto& e : entries) {
        out << "- op=" << e.operation
            << " | path=" << e.path
            << " | content=" << e.content
            << " | ts=" << e.timestamp
            << "\n";
    }
    return out.str();
}
