#pragma once

#include <string>

class SessionManager {
public:
    struct Session {
        bool active = false;
        std::string id;
        std::string username;
        std::string group;
        int uid = -1;
        int gid = -1;
    };

    static SessionManager& instance();

    void start(const Session& session);
    void clear();
    bool isActive() const;
    const Session& current() const;

private:
    Session current_;
};
