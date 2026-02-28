#include "commands/RepCmd.h"
#include "disk/BinaryIO.h"
#include "disk/MountManager.h"
#include "structs/MBR.h"
#include "structs/EBR.h"

#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
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
    iss >> token; // comando

    while (iss >> token) {
        if (token.rfind("-", 0) != 0) continue;

        auto eq = token.find('=');
        if (eq == std::string::npos) {
            p[tolower_str(token.substr(1))] = "true";
        } else {
            std::string key = tolower_str(token.substr(1, eq-1));
            std::string val = strip_quotes(token.substr(eq+1));
            p[key] = val;
        }
    }
    return p;
}

static std::string part_name_to_string(const char part_name[16]) {
    char tmp[17];
    std::memset(tmp, 0, sizeof(tmp));
    std::memcpy(tmp, part_name, 16);
    return std::string(tmp);
}

static bool is_used_part(const Partition& p) {
    return p.part_status == '1' && p.part_start >= 0 && p.part_size > 0;
}

static bool find_extended_part(const MBR& mbr, Partition& out) {
    for (int i = 0; i < 4; i++) {
        const auto& p = mbr.mbr_partitions[i];
        if (is_used_part(p) && (p.part_type == 'E' || p.part_type == 'e')) {
            out = p;
            return true;
        }
    }
    return false;
}

std::string RepCmd::exec(const std::string& line) {
    auto params = parse_params_local(line);

    if (!params.count("name")) return "ERROR: rep -> falta -name\n";
    if (!params.count("path")) return "ERROR: rep -> falta -path (salida)\n";

    std::string name = tolower_str(params["name"]);
    std::string out_path = params["path"];

    // ===== Fuente: preferir -id (calificación), si no usar -disk (debug) =====
    std::string disk_path;
    std::string used_id;

    if (params.count("id")) {
        used_id = params["id"];
        MountEntry me{};
        if (!MountManager::instance().getById(used_id, me)) {
            return "ERROR: rep -> id no encontrado (use mounted para ver ids)\n";
        }
        disk_path = me.path;
    } else if (params.count("disk")) {
        disk_path = params["disk"];
    } else {
        return "ERROR: rep -> falta -id (partición montada) o -disk (disco fuente)\n";
    }

    if (!(name == "mbr" || name == "disk")) {
        return "ERROR: rep -> por ahora solo -name=mbr o -name=disk\n";
    }

    // Leer MBR del disco
    MBR mbr{};
    if (!BinaryIO::readAt0(disk_path, &mbr, sizeof(MBR))) {
        return "ERROR: rep -> no se pudo leer el MBR del disco\n";
    }

    // Crear directorios de salida si no existen
    std::filesystem::path p(out_path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ofstream out(out_path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return "ERROR: rep -> no se pudo crear el archivo de reporte\n";

    if (name == "mbr") {
        out << "REPORTE MBR\n";
        out << "DISK: " << disk_path << "\n";
        if (!used_id.empty()) out << "ID: " << used_id << "\n";
        out << "\n";

        out << "mbr_tamano: " << mbr.mbr_tamano << " bytes\n";
        out << "mbr_fecha_creacion: " << mbr.mbr_fecha_creacion << "\n";
        out << "mbr_disk_signature: " << mbr.mbr_disk_signature << "\n";
        out << "mbr_fit: " << mbr.mbr_fit << "\n\n";

        out << "PARTICIONES (4)\n";
        out << "idx | status | type | fit | start | size | name\n";
        out << "----+--------+------+-----+-------+------+----------------\n";

        for (int i = 0; i < 4; i++) {
            const auto& pt = mbr.mbr_partitions[i];
            out << i
                << "   | " << pt.part_status
                << "      | " << pt.part_type
                << "    | " << pt.part_fit
                << "   | " << pt.part_start
                << "   | " << pt.part_size
                << " | " << part_name_to_string(pt.part_name)
                << "\n";
        }

        out.close();

        std::ostringstream msg;
        msg << "OK: rep -> mbr generado en: " << out_path << "\n";
        return msg.str();
    }

    // ====== name == "disk" ======
    out << "REPORTE DISK (MINIMO)\n";
    out << "DISK: " << disk_path << "\n";
    if (!used_id.empty()) out << "ID: " << used_id << "\n";
    out << "\n";

    out << "mbr_tamano: " << mbr.mbr_tamano << " bytes\n";
    out << "mbr_fit: " << mbr.mbr_fit << "\n\n";

    out << "PARTICIONES MBR (4)\n";
    out << "idx | status | type | fit | start | size | name\n";
    out << "----+--------+------+-----+-------+------+----------------\n";
    for (int i = 0; i < 4; i++) {
        const auto& pt = mbr.mbr_partitions[i];
        out << i
            << "   | " << pt.part_status
            << "      | " << pt.part_type
            << "    | " << pt.part_fit
            << "   | " << pt.part_start
            << "   | " << pt.part_size
            << " | " << part_name_to_string(pt.part_name)
            << "\n";
    }

    Partition ext{};
    if (!find_extended_part(mbr, ext)) {
        out << "\n(No hay partición extendida)\n";
        out.close();

        std::ostringstream msg;
        msg << "OK: rep -> disk generado en: " << out_path << "\n";
        return msg.str();
    }

    int32_t ext_start = ext.part_start;
    int32_t ext_end   = ext.part_start + ext.part_size;

    out << "\nEXTENDIDA\n";
    out << "name: " << part_name_to_string(ext.part_name) << "\n";
    out << "start: " << ext_start << "\n";
    out << "size: " << ext.part_size << "\n";
    out << "end: " << ext_end << "\n\n";

    out << "LOGICAS (EBR CHAIN)\n";
    out << "ebr_off | status | fit | data_start | data_size | next | name\n";
    out << "--------+--------+-----+-----------+----------+------+----------------\n";

    int32_t off = ext_start;
    int guard = 0; // evita loops infinitos si se corrompe la cadena

    while (off >= ext_start && off < ext_end && guard < 1000) {
        guard++;

        EBR e{};
        if (!BinaryIO::readAt(disk_path, off, &e, sizeof(EBR))) {
            out << "ERROR leyendo EBR en offset " << off << "\n";
            break;
        }

        out << off
            << " | " << e.part_status
            << "      | " << e.part_fit
            << "   | " << e.part_start
            << "      | " << e.part_size
            << " | " << e.part_next
            << " | " << part_name_to_string(e.part_name)
            << "\n";

        if (e.part_next == -1) break;
        off = e.part_next;
    }

    if (guard >= 1000) {
        out << "ERROR: cadena EBR demasiado larga (posible loop)\n";
    }

    out.close();

    std::ostringstream msg;
    msg << "OK: rep -> disk generado en: " << out_path << "\n";
    return msg.str();
}