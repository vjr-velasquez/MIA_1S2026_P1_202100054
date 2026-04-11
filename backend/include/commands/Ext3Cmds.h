#pragma once

#include <string>

class LossCmd {
public:
    std::string exec(const std::string& line);
};

class JournalingCmd {
public:
    std::string exec(const std::string& line);
};
