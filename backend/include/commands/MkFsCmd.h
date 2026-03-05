#ifndef COMMANDS_MKFSCMD_H
#define COMMANDS_MKFSCMD_H

#include <string>

class MkFsCmd {
public:
    std::string exec(const std::string& line);
};

#endif // COMMANDS_MKFSCMD_H