#pragma once

#include <string>

class LoginCmd {
public:
    std::string exec(const std::string& line);
};

class LogoutCmd {
public:
    std::string exec();
};

class MkGrpCmd {
public:
    std::string exec(const std::string& line);
};

class RmGrpCmd {
public:
    std::string exec(const std::string& line);
};

class MkUsrCmd {
public:
    std::string exec(const std::string& line);
};

class RmUsrCmd {
public:
    std::string exec(const std::string& line);
};

class ChGrpCmd {
public:
    std::string exec(const std::string& line);
};
