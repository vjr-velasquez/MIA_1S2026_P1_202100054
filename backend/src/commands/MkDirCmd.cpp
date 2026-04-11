#include "commands/MkDirCmd.h"
#include "disk/MountManager.h"
#include "fs/Ext2Paths.h"
#include "fs/JournalManager.h"
#include "session/SessionManager.h"

#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <string>
#include <cstring>
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

        iss >> token; // saltar comando

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

    static void setName(char dst[12], const std::string& name) {
        std::memset(dst, 0, 12);
        std::strncpy(dst, name.c_str(), 11);
    }
}

std::string MkDirCmd::exec(const std::string& line) {
    auto params = parse_params(line);

    if (!params.count("path")) return "ERROR: mkdir -> falta -path\n";

    std::string id;
    if (params.count("id")) id = params["id"];
    else if (SessionManager::instance().isActive()) id = SessionManager::instance().current().id;
    else return "ERROR: mkdir -> falta -id\n";

    const std::string path = params["path"];
    const bool pflag = params.count("p") && params["p"] == "true";

    if (path.empty() || path[0] != '/') {
        return "ERROR: mkdir -> -path debe ser absoluta e iniciar con '/'\n";
    }
    if (path == "/") {
        return "ERROR: mkdir -> no se puede crear '/'\n";
    }

    MountEntry mounted;
    if (!MountManager::instance().getById(id, mounted)) {
        return "ERROR: mkdir -> no existe montaje con id=" + id + "\n";
    }

    Ext2Paths fs(mounted.path, mounted.start);
    SuperBlock sb = fs.loadSuper();

    if (sb.s_magic != 0xEF53) {
        return "ERROR: mkdir -> la partición no está formateada como EXT2 (ejecute mkfs)\n";
    }

    auto parts = Ext2Paths::splitPath(path);
    if (parts.empty()) return "ERROR: mkdir -> ruta inválida\n";

    int32_t current = 0; // raíz

    for (size_t i = 0; i < parts.size(); ++i) {
        const std::string& name = parts[i];

        // Si existe, avanzamos (nota: no validamos tipo aquí por simplicidad)
        int32_t existing = fs.findInDirectory(sb, current, name);
        if (existing >= 0) {
            current = existing;
            continue;
        }

        // Si no existe y no hay -p, solo permitimos crear el último segmento
        if (!pflag && i != parts.size() - 1) {
            return "ERROR: mkdir -> no existe la carpeta padre (use -p)\n";
        }

        // Reservar inodo y bloque
        int32_t newInodeIdx = fs.allocateInode(sb);
        if (newInodeIdx < 0) return "ERROR: mkdir -> no hay inodos libres\n";

        int32_t newBlockIdx = fs.allocateBlock(sb);
        if (newBlockIdx < 0) return "ERROR: mkdir -> no hay bloques libres\n";

        // Crear inodo de carpeta
        Inode dir{};
        dir.i_uid = 1;
        dir.i_gid = 1;
        dir.i_size = 0;

        int64_t now = static_cast<int64_t>(std::time(nullptr));
        dir.i_atime = now;
        dir.i_ctime = now;
        dir.i_mtime = now;

        for (int k = 0; k < 15; ++k) dir.i_block[k] = -1;
        dir.i_block[0] = newBlockIdx;
        dir.i_type = '0';
        dir.i_perm = 664;

        fs.writeInode(sb, newInodeIdx, dir);

        // Crear bloque carpeta con . y ..
        FolderBlock fb{};
        std::memset(&fb, 0, sizeof(FolderBlock));

        setName(fb.b_content[0].b_name, ".");
        fb.b_content[0].b_inodo = newInodeIdx;

        setName(fb.b_content[1].b_name, "..");
        fb.b_content[1].b_inodo = current;

        fb.b_content[2].b_name[0] = '\0';
        fb.b_content[2].b_inodo = -1;
        fb.b_content[3].b_name[0] = '\0';
        fb.b_content[3].b_inodo = -1;

        fs.writeFolderBlock(sb, newBlockIdx, fb);

        // ✅ Insertar entrada en el padre (ahora soporta múltiples bloques)
        if (!fs.addDirEntry(sb, current, name, newInodeIdx)) {
            return "ERROR: mkdir -> carpeta padre sin espacio\n";
        }

        // Guardar superbloque actualizado (contadores/bitmaps)
        fs.writeSuper(sb);

        current = newInodeIdx;
    }

    fs.writeSuper(sb);
    JournalManager::append(mounted.path, mounted.start, sb, "mkdir", path, pflag ? "-p" : "");
    return "OK: mkdir -> carpeta creada: " + path + "\n";
}
