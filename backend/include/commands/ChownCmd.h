#ifndef COMMANDS_CHOWNCMD_H
#define COMMANDS_CHOWNCMD_H

#include <string>

class ChownCmd {
public:
    std::string exec(const std::string& line);
};

#endif
