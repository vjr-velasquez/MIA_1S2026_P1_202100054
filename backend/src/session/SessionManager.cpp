#include "session/SessionManager.h"

SessionManager& SessionManager::instance() {
    static SessionManager manager;
    return manager;
}

void SessionManager::start(const Session& session) {
    current_ = session;
    current_.active = true;
}

void SessionManager::clear() {
    current_ = Session{};
}

bool SessionManager::isActive() const {
    return current_.active;
}

const SessionManager::Session& SessionManager::current() const {
    return current_;
}
