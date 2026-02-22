#pragma once
#include <string>

class MkDisk {
public:
    std::string exec(int size, char unit, char fit, const std::string& path);
};
