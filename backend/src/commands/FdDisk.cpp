#include "commands/FdDisk.h"
#include "disk/BinaryIO.h"
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

static bool name_equals(const char part_name[16], const std::string& name) {
    // part_name puede no tener \0 al final; comparamos hasta 16
    char tmp[17];
    std::memset(tmp, 0, sizeof(tmp));
    std::memcpy(tmp, part_name, 16);
    return std::string(tmp) == name;
}

static int32_t calc_start_primary(const MBR& mbr, int32_t mbr_size_bytes) {
    // inicio “default”: inmediatamente después del MBR
    int32_t start = mbr_size_bytes;

    // si hay particiones creadas, ponemos start al final de la última (mayor end)
    int32_t max_end = start;
    for (int i = 0; i < 4; i++) {
        const auto& p = mbr.mbr_partitions[i];
        if (p.part_status == '1' && p.part_start >= 0 && p.part_size > 0) {
            int32_t end = p.part_start + p.part_size;
            if (end > max_end) max_end = end;
        }
    }
    return max_end;
}

std::string FdDisk::exec(const std::string& line) {
    std::ostringstream out;
    auto params = parse_params_local(line);

    // obligatorios
    if (!params.count("path")) return "ERROR: fdisk -> falta -path\n";
    if (!params.count("name")) return "ERROR: fdisk -> falta -name\n";
    if (!params.count("size")) return "ERROR: fdisk -> falta -size\n";

    std::string path = params["path"];
    std::string name = params["name"];
    if (name.size() > 16) return "ERROR: fdisk -> -name máximo 16 caracteres\n";

    int size_num = 0;
    try { size_num = std::stoi(params["size"]); }
    catch (...) { return "ERROR: fdisk -> -size inválido\n"; }
    if (size_num <= 0) return "ERROR: fdisk -> -size debe ser > 0\n";

    char unit = 'K'; // por defecto K
    if (params.count("unit") && !params["unit"].empty()) unit = params["unit"][0];
    unit = (char)std::toupper((unsigned char)unit);

    int32_t size_bytes = 0;
    if (unit == 'K') size_bytes = (int32_t)size_num * 1024;
    else if (unit == 'M') size_bytes = (int32_t)size_num * 1024 * 1024;
    else return "ERROR: fdisk -> -unit debe ser K o M\n";

    char type = 'P';
    if (params.count("type") && !params["type"].empty()) type = params["type"][0];
    type = (char)std::toupper((unsigned char)type);
    if (type != 'P') return "ERROR: fdisk -> por ahora solo -type=P (primaria)\n";

    char fit = 'F';
    if (params.count("fit") && !params["fit"].empty()) fit = params["fit"][0];
    fit = (char)std::toupper((unsigned char)fit);
    if (!(fit == 'B' || fit == 'F' || fit == 'W')) return "ERROR: fdisk -> -fit debe ser B, F o W\n";

    // Leer MBR
    MBR mbr{};
    if (!BinaryIO::readAt0(path, &mbr, sizeof(MBR))) {
        return "ERROR: fdisk -> no se pudo leer el MBR (¿path correcto?)\n";
    }

    // Validar nombre no repetido
    for (int i = 0; i < 4; i++) {
        const auto& p = mbr.mbr_partitions[i];
        if (p.part_status == '1' && name_equals(p.part_name, name)) {
            return "ERROR: fdisk -> ya existe una partición con ese nombre\n";
        }
    }

    // Buscar slot libre
    int free_idx = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status != '1') { free_idx = i; break; }
    }
    if (free_idx == -1) return "ERROR: fdisk -> no hay slots libres en el MBR (4/4 usadas)\n";

    // Calcular start (v1: al final de la última partición)
    int32_t start = calc_start_primary(mbr, (int32_t)sizeof(MBR));
    int32_t end = start + size_bytes;

    if (end > mbr.mbr_tamano) {
        return "ERROR: fdisk -> no hay espacio suficiente en el disco\n";
    }

    // Escribir partición
    Partition np{};
    np.part_status = '1';
    np.part_type = 'P';
    np.part_fit = fit;
    np.part_start = start;
    np.part_size = size_bytes;
    std::memset(np.part_name, 0, 16);
    std::memcpy(np.part_name, name.c_str(), name.size());

    mbr.mbr_partitions[free_idx] = np;

    // Guardar MBR
    if (!BinaryIO::writeAt0(path, &mbr, sizeof(MBR))) {
        return "ERROR: fdisk -> no se pudo escribir el MBR\n";
    }

    out << "OK: fdisk -> partición primaria creada: name=" << name
        << " start=" << start << " size=" << size_bytes << " bytes\n";
    return out.str();
}