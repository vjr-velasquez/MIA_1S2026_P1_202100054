#include "commands/MkDisk.h"
#include "structs/MBR.h"
#include "disk/BinaryIO.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <ctime>
#include <sstream>
#include <cctype>

static int64_t now_unix() {
    return static_cast<int64_t>(std::time(nullptr));
}

static int32_t rand_int() {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int32_t> dist(1, 2000000000);
    return dist(rng);
}

std::string MkDisk::exec(int size, char unit, char fit, const std::string& path) {
    std::ostringstream out;

    if (size <= 0) return "ERROR: mkdisk -> -size debe ser > 0\n";

    unit = (char)std::toupper((unsigned char)unit);
    int64_t bytes = 0;
    if (unit == 'K') bytes = (int64_t)size * 1024;
    else if (unit == 'M') bytes = (int64_t)size * 1024 * 1024;
    else return "ERROR: mkdisk -> -unit debe ser K o M\n";

    fit = (char)std::toupper((unsigned char)fit);
    if (!(fit == 'B' || fit == 'F' || fit == 'W')) {
        return "ERROR: mkdisk -> -fit debe ser B, F o W\n";
    }

    if (path.empty()) return "ERROR: mkdisk -> -path vacío\n";

    // Crear directorios si no existen
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    // Crear archivo y llenarlo con ceros (tamaño fijo)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return "ERROR: mkdisk -> no se pudo crear el archivo\n";

        // “llenar con ceros” de forma eficiente: seek al último byte y escribir un 0
        file.seekp(bytes - 1);
        char zero = 0;
        file.write(&zero, 1);
        file.close();
    }

    // Construir MBR
    MBR mbr{};
    mbr.mbr_tamano = (int32_t)bytes;
    mbr.mbr_fecha_creacion = now_unix();
    mbr.mbr_disk_signature = rand_int();
    mbr.mbr_fit = fit;

    for (int i = 0; i < 4; i++) {
        auto &pt = mbr.mbr_partitions[i];
        pt.part_status = '0';
        pt.part_type = 'P';
        pt.part_fit = 'F';
        pt.part_start = -1;
        pt.part_size = 0;
        for (int j = 0; j < 16; j++) pt.part_name[j] = 0;
    }

    // Escribir MBR en offset 0
    if (!BinaryIO::writeAt0(path, &mbr, sizeof(MBR))) {
        return "ERROR: mkdisk -> no se pudo escribir el MBR\n";
    }

    out << "OK: mkdisk -> disco creado: " << path << " (" << bytes << " bytes)\n";
    return out.str();
}
