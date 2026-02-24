#include "disk/MountManager.h"
#include <sstream>
#include <algorithm>

MountManager& MountManager::instance() {
    static MountManager inst;
    return inst;
}

std::string MountManager::next_id_for_disk(const std::string& path) {
    // letra por disco: a,b,c... según orden de aparición
    // número por partición: 1,2,3... por cada disco
    std::vector<std::string> disks;
    for (const auto& m : mounts) {
        if (std::find(disks.begin(), disks.end(), m.path) == disks.end()) {
            disks.push_back(m.path);
        }
    }
    auto it = std::find(disks.begin(), disks.end(), path);
    size_t disk_index = (it == disks.end()) ? disks.size() : (size_t)std::distance(disks.begin(), it);

    char letter = (char)('a' + (int)disk_index);

    int count_for_disk = 0;
    for (const auto& m : mounts) if (m.path == path) count_for_disk++;

    int num = count_for_disk + 1;

    std::ostringstream id;
    id << "vd" << letter << num;
    return id.str();
}

std::string MountManager::mount(const std::string& path, const std::string& name, int32_t start, int32_t size) {
    for (const auto& m : mounts) {
        if (m.path == path && m.name == name) {
            return "ERROR: mount -> ya está montada esa partición\n";
        }
    }

    MountEntry e;
    e.id = next_id_for_disk(path);
    e.path = path;
    e.name = name;
    e.start = start;
    e.size = size;

    mounts.push_back(e);

    std::ostringstream out;
    out << "OK: mount -> " << name << " montada como " << e.id << "\n";
    return out.str();
}

std::string MountManager::list() const {
    std::ostringstream out;
    if (mounts.empty()) {
        out << "No hay particiones montadas.\n";
        return out.str();
    }

    out << "PARTICIONES MONTADAS:\n";
    for (const auto& m : mounts) {
        out << "- " << m.id << " | " << m.name << " | " << m.path << "\n";
    }
    return out.str();
}