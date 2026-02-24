#include "commands/MountCmd.h"
#include "disk/BinaryIO.h"
#include "disk/MountManager.h"
#include "structs/MBR.h"

#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cstring>

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
        if ((v.front()=='"' && v.back()=='"') || (v.front()=='\'' && v.back()=='\'')) {
            return v.substr(1, v.size()-2);
        }
    }
    return v;
}
static std::unordered_map<std::string,std::string> parse_params_local(const std::string& line) {
    std::unordered_map<std::string,std::string> p;
    std::istringstream iss(line);
    std::string token;
    iss >> token;
    while (iss >> token) {
        if (token.rfind("-", 0) != 0) continue;
        auto eq = token.find('=');
        if (eq == std::string::npos) p[tolower_str(token.substr(1))] = "true";
        else p[tolower_str(token.substr(1, eq-1))] = strip_quotes(token.substr(eq+1));
    }
    return p;
}
static bool name_equals(const char part_name[16], const std::string& name) {
    char tmp[17]; std::memset(tmp, 0, sizeof(tmp));
    std::memcpy(tmp, part_name, 16);
    return std::string(tmp) == name;
}

std::string MountCmd::exec(const std::string& line) {
    auto params = parse_params_local(line);
    if (!params.count("path")) return "ERROR: mount -> falta -path\n";
    if (!params.count("name")) return "ERROR: mount -> falta -name\n";

    std::string path = params["path"];
    std::string name = params["name"];

    MBR mbr{};
    if (!BinaryIO::readAt0(path, &mbr, sizeof(MBR))) {
        return "ERROR: mount -> no se pudo leer el MBR\n";
    }

    for (int i = 0; i < 4; i++) {
        const auto& p = mbr.mbr_partitions[i];
        if (p.part_status == '1' && name_equals(p.part_name, name)) {
            return MountManager::instance().mount(path, name, p.part_start, p.part_size);
        }
    }

    return "ERROR: mount -> no existe una partición con ese nombre\n";
}

std::string MountedCmd::exec() {
    return MountManager::instance().list();
}