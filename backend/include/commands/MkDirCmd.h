#ifndef COMMANDS_MKDIRCMD_H
#define COMMANDS_MKDIRCMD_H
#include "commands/MkDirCmd.h"
#include <string>

class MkDirCmd {
public:
    std::string exec(const std::string& line);
};

#endif