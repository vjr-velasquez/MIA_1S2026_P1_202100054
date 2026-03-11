#ifndef COMMANDS_CATCMD_H
#define COMMANDS_CATCMD_H

#include <string>

class CatCmd {
public:
    std::string exec(const std::string& line);
};

#endif
