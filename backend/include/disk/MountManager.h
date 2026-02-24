#pragma once
#include <string>
#include <vector>

struct MountEntry {
    std::string id;
    std::string path;
    std::string name;
    int32_t start = -1;
    int32_t size = 0;
};

class MountManager {
public:
    static MountManager& instance();

    // retorna id o error (string empieza con "ERROR:")
    std::string mount(const std::string& path, const std::string& name, int32_t start, int32_t size);

    std::string list() const;

private:
    std::vector<MountEntry> mounts;

    std::string next_id_for_disk(const std::string& path);
};