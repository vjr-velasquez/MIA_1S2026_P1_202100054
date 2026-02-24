#pragma once
#include <string>

class MountCmd {
public:
    std::string exec(const std::string& line);
};

class MountedCmd {
public:
    std::string exec();
};