#include "commands/FdDisk.h"
#include "disk/BinaryIO.h"
#include "structs/MBR.h"
#include "structs/EBR.h"

#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

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
    char tmp[17];
    std::memset(tmp, 0, sizeof(tmp));
    std::memcpy(tmp, part_name, 16);
    return std::string(tmp) == name;
}

static int32_t calc_start_primary(const MBR& mbr, int32_t mbr_size_bytes) {
    int32_t start = mbr_size_bytes;

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

// ===== Helpers Sprint2 =====
static bool is_used(const Partition& p) {
    return p.part_status == '1' && p.part_start >= 0 && p.part_size > 0;
}

static bool has_extended(const MBR& mbr) {
    for (int i = 0; i < 4; i++) {
        const auto& p = mbr.mbr_partitions[i];
        if (is_used(p) && (p.part_type == 'E' || p.part_type == 'e')) return true;
    }
    return false;
}

static bool get_extended(const MBR& mbr, Partition& out) {
    for (int i = 0; i < 4; i++) {
        const auto& p = mbr.mbr_partitions[i];
        if (is_used(p) && (p.part_type == 'E' || p.part_type == 'e')) { out = p; return true; }
    }
    return false;
}

static bool logical_name_exists(const std::string& path, const Partition& ext, const std::string& name) {
    int32_t ext_start = ext.part_start;
    int32_t ext_end   = ext.part_start + ext.part_size;

    int32_t off = ext_start;
    while (off >= ext_start && off < ext_end) {
        EBR e{};
        if (!BinaryIO::readAt(path, off, &e, sizeof(EBR))) return false;

        if (e.part_status == '1' && name_equals(e.part_name, name)) return true;
        if (e.part_next == -1) break;
        off = e.part_next;
    }
    return false;
}

static void zero_region(const std::string& path, int64_t start, int64_t size) {
    if (size <= 0) return;
    std::vector<char> zeros(static_cast<size_t>(size), '\0');
    BinaryIO::writeAt(path, start, zeros.data(), zeros.size());
}

std::string FdDisk::exec(const std::string& line) {
    std::ostringstream out;
    auto params = parse_params_local(line);

    if (!params.count("path")) return "ERROR: fdisk -> falta -path\n";
    if (!params.count("name")) return "ERROR: fdisk -> falta -name\n";

    std::string path = params["path"];
    std::string name = params["name"];
    if (name.size() > 16) return "ERROR: fdisk -> -name máximo 16 caracteres\n";

    // Leer MBR
    MBR mbr{};
    if (!BinaryIO::readAt0(path, &mbr, sizeof(MBR))) {
        return "ERROR: fdisk -> no se pudo leer el MBR (¿path correcto?)\n";
    }

    // ====== DELETE ======
    if (params.count("delete")) {
        const std::string mode = tolower_str(params["delete"]);
        if (!(mode == "fast" || mode == "full")) {
            return "ERROR: fdisk -> -delete debe ser fast o full\n";
        }

        for (int i = 0; i < 4; ++i) {
            auto& part = mbr.mbr_partitions[i];
            if (!is_used(part) || !name_equals(part.part_name, name)) continue;

            if (mode == "full") zero_region(path, part.part_start, part.part_size);
            part = Partition{};
            part.part_start = -1;
            if (!BinaryIO::writeAt0(path, &mbr, sizeof(MBR))) {
                return "ERROR: fdisk -> no se pudo escribir el MBR\n";
            }
            return "OK: fdisk -> partición eliminada: " + name + "\n";
        }

        Partition ext{};
        if (!get_extended(mbr, ext)) return "ERROR: fdisk -> no existe la partición\n";

        int32_t ext_start = ext.part_start;
        int32_t off = ext_start;
        int32_t prevOff = -1;
        EBR cur{};

        while (off >= ext_start && off < ext.part_start + ext.part_size) {
            if (!BinaryIO::readAt(path, off, &cur, sizeof(EBR))) break;
            if (cur.part_status == '1' && name_equals(cur.part_name, name)) {
                if (mode == "full") zero_region(path, off, sizeof(EBR) + cur.part_size);

                if (prevOff == -1) {
                    if (cur.part_next == -1) {
                        EBR empty{};
                        empty.part_status = '0';
                        empty.part_fit = ext.part_fit;
                        empty.part_start = ext_start + static_cast<int32_t>(sizeof(EBR));
                        empty.part_size = 0;
                        empty.part_next = -1;
                        std::memset(empty.part_name, 0, 16);
                        BinaryIO::writeAt(path, ext_start, &empty, sizeof(EBR));
                    } else {
                        EBR next{};
                        if (!BinaryIO::readAt(path, cur.part_next, &next, sizeof(EBR))) {
                            return "ERROR: fdisk -> no se pudo reencadenar la lógica\n";
                        }
                        BinaryIO::writeAt(path, ext_start, &next, sizeof(EBR));
                    }
                } else {
                    EBR prev{};
                    if (!BinaryIO::readAt(path, prevOff, &prev, sizeof(EBR))) {
                        return "ERROR: fdisk -> no se pudo leer EBR anterior\n";
                    }
                    prev.part_next = cur.part_next;
                    BinaryIO::writeAt(path, prevOff, &prev, sizeof(EBR));
                }
                return "OK: fdisk -> partición eliminada: " + name + "\n";
            }
            if (cur.part_next == -1) break;
            prevOff = off;
            off = cur.part_next;
        }

        return "ERROR: fdisk -> no existe la partición\n";
    }

    // ====== ADD ======
    if (params.count("add")) {
        int addNum = 0;
        try { addNum = std::stoi(params["add"]); }
        catch (...) { return "ERROR: fdisk -> -add inválido\n"; }
        if (addNum == 0) return "ERROR: fdisk -> -add no puede ser 0\n";

        char unit = 'K';
        if (params.count("unit") && !params["unit"].empty()) unit = params["unit"][0];
        unit = (char)std::toupper((unsigned char)unit);

        int32_t delta = 0;
        if (unit == 'K') delta = addNum * 1024;
        else if (unit == 'M') delta = addNum * 1024 * 1024;
        else return "ERROR: fdisk -> -unit debe ser K o M\n";

        for (int i = 0; i < 4; ++i) {
            auto& part = mbr.mbr_partitions[i];
            if (!is_used(part) || !name_equals(part.part_name, name)) continue;

            int32_t limit = mbr.mbr_tamano;
            for (int j = 0; j < 4; ++j) {
                const auto& other = mbr.mbr_partitions[j];
                if (!is_used(other) || other.part_start <= part.part_start) continue;
                limit = std::min(limit, other.part_start);
            }

            int32_t newSize = part.part_size + delta;
            if (newSize <= 0) return "ERROR: fdisk -> el tamaño resultante sería inválido\n";
            if (part.part_start + newSize > limit) return "ERROR: fdisk -> no hay espacio libre después de la partición\n";

            part.part_size = newSize;
            if (!BinaryIO::writeAt0(path, &mbr, sizeof(MBR))) {
                return "ERROR: fdisk -> no se pudo escribir el MBR\n";
            }
            return "OK: fdisk -> tamaño ajustado: " + name + "\n";
        }

        Partition ext{};
        if (!get_extended(mbr, ext)) return "ERROR: fdisk -> no existe la partición\n";
        int32_t ext_end = ext.part_start + ext.part_size;
        int32_t off = ext.part_start;
        while (off >= ext.part_start && off < ext_end) {
            EBR cur{};
            if (!BinaryIO::readAt(path, off, &cur, sizeof(EBR))) break;

            if (cur.part_status == '1' && name_equals(cur.part_name, name)) {
                int32_t limit = (cur.part_next == -1) ? ext_end : cur.part_next;
                int32_t newSize = cur.part_size + delta;
                if (newSize <= 0) return "ERROR: fdisk -> el tamaño resultante sería inválido\n";
                if (cur.part_start + newSize > limit) return "ERROR: fdisk -> no hay espacio libre después de la partición\n";
                cur.part_size = newSize;
                if (!BinaryIO::writeAt(path, off, &cur, sizeof(EBR))) {
                    return "ERROR: fdisk -> no se pudo actualizar la partición lógica\n";
                }
                return "OK: fdisk -> tamaño ajustado: " + name + "\n";
            }

            if (cur.part_next == -1) break;
            off = cur.part_next;
        }

        return "ERROR: fdisk -> no existe la partición\n";
    }

    // obligatorios al crear
    if (!params.count("size")) return "ERROR: fdisk -> falta -size\n";

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
    if (!(type == 'P' || type == 'E' || type == 'L')) {
        return "ERROR: fdisk -> -type debe ser P, E o L\n";
    }

    char fit = 'F';
    if (params.count("fit") && !params["fit"].empty()) fit = params["fit"][0];
    fit = (char)std::toupper((unsigned char)fit);
    if (!(fit == 'B' || fit == 'F' || fit == 'W')) return "ERROR: fdisk -> -fit debe ser B, F o W\n";

    // Validar nombre no repetido en MBR
    for (int i = 0; i < 4; i++) {
        const auto& p = mbr.mbr_partitions[i];
        if (p.part_status == '1' && name_equals(p.part_name, name)) {
            return "ERROR: fdisk -> ya existe una partición con ese nombre\n";
        }
    }

    // ====== Caso L: lógica (NO usa slots del MBR) ======
    if (type == 'L') {
        Partition ext{};
        if (!get_extended(mbr, ext)) return "ERROR: fdisk -> no existe partición extendida\n";

        if (logical_name_exists(path, ext, name)) {
            return "ERROR: fdisk -> ya existe una partición lógica con ese nombre\n";
        }

        int32_t ext_start = ext.part_start;
        int32_t ext_end   = ext.part_start + ext.part_size;

        // leer EBR inicial
        EBR first{};
        if (!BinaryIO::readAt(path, ext_start, &first, sizeof(EBR))) {
            return "ERROR: fdisk -> no se pudo leer EBR inicial\n";
        }

        // Caso: primer EBR vacío
        if (first.part_status == '0' && first.part_size == 0 && first.part_next == -1) {
            int32_t need = (int32_t)sizeof(EBR) + size_bytes;
            if (ext_start + need > ext_end) return "ERROR: fdisk -> no hay espacio en la extendida\n";

            first.part_status = '1';
            first.part_fit    = fit;
            first.part_start  = ext_start + (int32_t)sizeof(EBR);
            first.part_size   = size_bytes;
            first.part_next   = -1;
            std::memset(first.part_name, 0, 16);
            std::memcpy(first.part_name, name.c_str(), name.size());

            if (!BinaryIO::writeAt(path, ext_start, &first, sizeof(EBR))) {
                return "ERROR: fdisk -> no se pudo escribir EBR\n";
            }

            out << "OK: fdisk -> partición lógica creada: name=" << name
                << " ebr=" << ext_start
                << " start=" << first.part_start
                << " size=" << size_bytes << " bytes\n";
            return out.str();
        }

        // Insertar al final de la cadena
        int32_t off = ext_start;
        EBR cur{};
        while (true) {
            if (!BinaryIO::readAt(path, off, &cur, sizeof(EBR))) {
                return "ERROR: fdisk -> no se pudo leer cadena EBR\n";
            }
            if (cur.part_next == -1) break;
            off = cur.part_next;
        }
        int32_t last_ebr_off = off;
        EBR last = cur;

        int32_t last_data_start = last_ebr_off + (int32_t)sizeof(EBR);
        int32_t last_data_end   = last_data_start + last.part_size;

        int32_t new_ebr_off = last_data_end;
        int32_t need = (int32_t)sizeof(EBR) + size_bytes;

        if (new_ebr_off + need > ext_end) return "ERROR: fdisk -> no hay espacio suficiente en la extendida\n";

        EBR neu{};
        neu.part_status = '1';
        neu.part_fit    = fit;
        neu.part_start  = new_ebr_off + (int32_t)sizeof(EBR);
        neu.part_size   = size_bytes;
        neu.part_next   = -1;
        std::memset(neu.part_name, 0, 16);
        std::memcpy(neu.part_name, name.c_str(), name.size());

        if (!BinaryIO::writeAt(path, new_ebr_off, &neu, sizeof(EBR))) {
            return "ERROR: fdisk -> no se pudo escribir nuevo EBR\n";
        }

        last.part_next = new_ebr_off;
        if (!BinaryIO::writeAt(path, last_ebr_off, &last, sizeof(EBR))) {
            return "ERROR: fdisk -> no se pudo actualizar EBR anterior\n";
        }

        out << "OK: fdisk -> partición lógica creada: name=" << name
            << " ebr=" << new_ebr_off
            << " start=" << neu.part_start
            << " size=" << size_bytes << " bytes\n";
        return out.str();
    }

    // ====== Caso E: extendida (usa slots del MBR, solo 1) ======
    if (type == 'E') {
        if (has_extended(mbr)) return "ERROR: fdisk -> ya existe una partición extendida\n";
    }

    // Buscar slot libre (P o E usan slot)
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

    // Escribir partición (P o E)
    Partition np{};
    np.part_status = '1';
    np.part_type = type; // 'P' o 'E'
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

    // Si es extendida: inicializar EBR vacío en el inicio de la extendida
    if (type == 'E') {
        EBR e{};
        e.part_status = '0';
        e.part_fit    = fit;
        e.part_start  = start + (int32_t)sizeof(EBR);
        e.part_size   = 0;
        e.part_next   = -1;
        std::memset(e.part_name, 0, 16);

        if (!BinaryIO::writeAt(path, start, &e, sizeof(EBR))) {
            return "ERROR: fdisk -> no se pudo inicializar EBR en la extendida\n";
        }

        out << "OK: fdisk -> partición extendida creada: name=" << name
            << " start=" << start << " size=" << size_bytes << " bytes\n";
        return out.str();
    }

    // Primaria (igual que antes)
    out << "OK: fdisk -> partición primaria creada: name=" << name
        << " start=" << start << " size=" << size_bytes << " bytes\n";
    return out.str();
}
