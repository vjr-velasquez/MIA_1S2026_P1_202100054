#include "commands/RmDisk.h"
#include <filesystem>
#include <sstream>

std::string RmDisk::exec(const std::string& path) {
    std::ostringstream out;

    if (path.empty()) return "ERROR: rmdisk -> falta -path\n";

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return "ERROR: rmdisk -> el archivo no existe o no se pudo verificar\n";
    }

    if (!std::filesystem::remove(path, ec) || ec) {
        return "ERROR: rmdisk -> no se pudo eliminar el archivo\n";
    }

    out << "OK: rmdisk -> disco eliminado: " << path << "\n";
    return out.str();
}
