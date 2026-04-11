#pragma once

#include <string>

class UnmountCmd {
public:
    std::string exec(const std::string& line);
};

class RemoveCmd {
public:
    std::string exec(const std::string& line);
};

class RenameCmd {
public:
    std::string exec(const std::string& line);
};

class CopyCmd {
public:
    std::string exec(const std::string& line);
};

class MoveCmd {
public:
    std::string exec(const std::string& line);
};

class FindCmd {
public:
    std::string exec(const std::string& line);
};
