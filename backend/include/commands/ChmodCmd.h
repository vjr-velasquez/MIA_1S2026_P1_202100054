#ifndef COMMANDS_CHMODCMD_H
#define COMMANDS_CHMODCMD_H

#include <string>

class ChmodCmd {
public:
    std::string exec(const std::string& line);
};

#endif
