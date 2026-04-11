#include "commands/UserSessionCmds.h"

#include "disk/MountManager.h"
#include "fs/Ext2Paths.h"
#include "fs/FsFileOps.h"
#include "fs/JournalManager.h"
#include "session/SessionManager.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
    struct GroupRecord {
        int id = 0;
        std::string name;
    };

    struct UserRecord {
        int id = 0;
        std::string group;
        std::string user;
        std::string pass;
    };

    static inline std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    static inline std::string tolower_str(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static inline std::string strip_quotes(std::string v) {
        v = trim(v);
        if (v.size() >= 2) {
            if ((v.front() == '"' && v.back() == '"') ||
                (v.front() == '\'' && v.back() == '\'')) {
                return v.substr(1, v.size() - 2);
            }
        }
        return v;
    }

    static std::vector<std::string> tokenize_keep_quotes(const std::string& line) {
        std::vector<std::string> tokens;
        std::string cur;
        char quote = '\0';

        for (char ch : line) {
            if (quote != '\0') {
                cur.push_back(ch);
                if (ch == quote) quote = '\0';
                continue;
            }

            if (ch == '"' || ch == '\'') {
                quote = ch;
                cur.push_back(ch);
                continue;
            }

            if (std::isspace(static_cast<unsigned char>(ch))) {
                if (!cur.empty()) {
                    tokens.push_back(cur);
                    cur.clear();
                }
                continue;
            }

            cur.push_back(ch);
        }

        if (!cur.empty()) tokens.push_back(cur);
        return tokens;
    }

    static std::unordered_map<std::string, std::string> parse_params(const std::string& line) {
        std::unordered_map<std::string, std::string> p;
        auto tokens = tokenize_keep_quotes(line);
        if (tokens.empty()) return p;

        for (size_t i = 1; i < tokens.size(); ++i) {
            const std::string& token = tokens[i];
            if (token.rfind("-", 0) != 0) continue;

            auto eq = token.find('=');
            if (eq == std::string::npos) {
                p[tolower_str(token.substr(1))] = "true";
            } else {
                std::string key = tolower_str(token.substr(1, eq - 1));
                std::string val = token.substr(eq + 1);
                p[key] = strip_quotes(val);
            }
        }
        return p;
    }

    static std::vector<std::string> split_csv_line(const std::string& line) {
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string item;
        while (std::getline(ss, item, ',')) {
            parts.push_back(trim(item));
        }
        return parts;
    }

    static bool load_users_file(const std::string& mountId, Ext2Paths& fs, SuperBlock& sb, int32_t& inodeIndex, std::string& content, std::string& error) {
        MountEntry mounted;
        if (!MountManager::instance().getById(mountId, mounted)) {
            error = "no existe montaje con id=" + mountId;
            return false;
        }

        fs = Ext2Paths(mounted.path, mounted.start);
        sb = fs.loadSuper();
        if (sb.s_magic != 0xEF53) {
            error = "la partición no está formateada como EXT2 (ejecute mkfs)";
            return false;
        }

        if (!FsFileOps::readFile(fs, sb, "/users.txt", content, &inodeIndex)) {
            error = "no existe /users.txt";
            return false;
        }

        return true;
    }

    static void parse_users_content(const std::string& content, std::vector<GroupRecord>& groups, std::vector<UserRecord>& users) {
        std::istringstream in(content);
        std::string line;

        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty()) continue;

            auto cols = split_csv_line(line);
            if (cols.size() == 3 && cols[1] == "G") {
                GroupRecord g{};
                g.id = std::stoi(cols[0]);
                g.name = cols[2];
                groups.push_back(g);
            } else if (cols.size() == 5 && cols[1] == "U") {
                UserRecord u{};
                u.id = std::stoi(cols[0]);
                u.group = cols[2];
                u.user = cols[3];
                u.pass = cols[4];
                users.push_back(u);
            }
        }
    }

    static std::string serialize_users_content(const std::vector<GroupRecord>& groups, const std::vector<UserRecord>& users) {
        std::ostringstream out;
        for (const auto& g : groups) {
            out << g.id << ",G," << g.name << "\n";
        }
        for (const auto& u : users) {
            out << u.id << ",U," << u.group << "," << u.user << "," << u.pass << "\n";
        }
        return out.str();
    }

    static bool require_root_session(std::string& error) {
        if (!SessionManager::instance().isActive()) {
            error = "no hay una sesión activa";
            return false;
        }
        const auto& session = SessionManager::instance().current();
        if (session.username != "root") {
            error = "solo el usuario root puede ejecutar este comando";
            return false;
        }
        return true;
    }

    static int next_group_id(const std::vector<GroupRecord>& groups) {
        int maxId = 0;
        for (const auto& g : groups) {
            if (g.id > maxId) maxId = g.id;
        }
        return maxId + 1;
    }

    static int next_user_id(const std::vector<UserRecord>& users) {
        int maxId = 0;
        for (const auto& u : users) {
            if (u.id > maxId) maxId = u.id;
        }
        return maxId + 1;
    }
}

std::string LoginCmd::exec(const std::string& line) {
    auto params = parse_params(line);

    if (!params.count("user")) return "ERROR: login -> falta -user\n";
    if (!params.count("pass")) return "ERROR: login -> falta -pass\n";
    if (!params.count("id")) return "ERROR: login -> falta -id\n";

    Ext2Paths fs("", 0);
    SuperBlock sb{};
    int32_t inodeIndex = -1;
    std::string content;
    std::string error;
    if (!load_users_file(params["id"], fs, sb, inodeIndex, content, error)) {
        return "ERROR: login -> " + error + "\n";
    }

    std::vector<GroupRecord> groups;
    std::vector<UserRecord> users;
    parse_users_content(content, groups, users);

    for (const auto& user : users) {
        if (user.id <= 0) continue;
        if (user.user == params["user"] && user.pass == params["pass"]) {
            int gid = -1;
            for (const auto& group : groups) {
                if (group.id > 0 && group.name == user.group) {
                    gid = group.id;
                    break;
                }
            }

            SessionManager::Session session;
            session.id = params["id"];
            session.username = user.user;
            session.group = user.group;
            session.uid = user.id;
            session.gid = gid;
            SessionManager::instance().start(session);
            return "OK: login -> sesión iniciada: " + user.user + "\n";
        }
    }

    return "ERROR: login -> credenciales inválidas\n";
}

std::string LogoutCmd::exec() {
    if (!SessionManager::instance().isActive()) {
        return "ERROR: logout -> no hay una sesión activa\n";
    }

    const std::string user = SessionManager::instance().current().username;
    SessionManager::instance().clear();
    return "OK: logout -> sesión finalizada: " + user + "\n";
}

std::string MkGrpCmd::exec(const std::string& line) {
    auto params = parse_params(line);
    if (!params.count("name")) return "ERROR: mkgrp -> falta -name\n";

    std::string error;
    if (!require_root_session(error)) return "ERROR: mkgrp -> " + error + "\n";

    Ext2Paths fs("", 0);
    SuperBlock sb{};
    int32_t inodeIndex = -1;
    std::string content;
    if (!load_users_file(SessionManager::instance().current().id, fs, sb, inodeIndex, content, error)) {
        return "ERROR: mkgrp -> " + error + "\n";
    }

    std::vector<GroupRecord> groups;
    std::vector<UserRecord> users;
    parse_users_content(content, groups, users);

    for (const auto& group : groups) {
        if (group.id > 0 && group.name == params["name"]) {
            return "ERROR: mkgrp -> ya existe el grupo\n";
        }
    }

    GroupRecord group;
    group.id = next_group_id(groups);
    group.name = params["name"];
    groups.push_back(group);

    content = serialize_users_content(groups, users);
    if (!FsFileOps::writeFile(fs, sb, inodeIndex, content, error)) {
        return "ERROR: mkgrp -> " + error + "\n";
    }
    MountEntry mounted{};
    if (MountManager::instance().getById(SessionManager::instance().current().id, mounted)) {
        JournalManager::append(mounted.path, mounted.start, sb, "mkgrp", "/users.txt", group.name);
    }
    return "OK: mkgrp -> grupo creado: " + group.name + "\n";
}

std::string RmGrpCmd::exec(const std::string& line) {
    auto params = parse_params(line);
    if (!params.count("name")) return "ERROR: rmgrp -> falta -name\n";

    std::string error;
    if (!require_root_session(error)) return "ERROR: rmgrp -> " + error + "\n";

    Ext2Paths fs("", 0);
    SuperBlock sb{};
    int32_t inodeIndex = -1;
    std::string content;
    if (!load_users_file(SessionManager::instance().current().id, fs, sb, inodeIndex, content, error)) {
        return "ERROR: rmgrp -> " + error + "\n";
    }

    std::vector<GroupRecord> groups;
    std::vector<UserRecord> users;
    parse_users_content(content, groups, users);

    bool found = false;
    for (auto& group : groups) {
        if (group.id > 0 && group.name == params["name"]) {
            group.id = 0;
            found = true;
            break;
        }
    }
    if (!found) return "ERROR: rmgrp -> no existe el grupo\n";

    content = serialize_users_content(groups, users);
    if (!FsFileOps::writeFile(fs, sb, inodeIndex, content, error)) {
        return "ERROR: rmgrp -> " + error + "\n";
    }
    MountEntry mounted{};
    if (MountManager::instance().getById(SessionManager::instance().current().id, mounted)) {
        JournalManager::append(mounted.path, mounted.start, sb, "rmgrp", "/users.txt", params["name"]);
    }
    return "OK: rmgrp -> grupo eliminado: " + params["name"] + "\n";
}

std::string MkUsrCmd::exec(const std::string& line) {
    auto params = parse_params(line);
    if (!params.count("user")) return "ERROR: mkusr -> falta -user\n";
    if (!params.count("pass")) return "ERROR: mkusr -> falta -pass\n";
    if (!params.count("grp")) return "ERROR: mkusr -> falta -grp\n";

    std::string error;
    if (!require_root_session(error)) return "ERROR: mkusr -> " + error + "\n";

    Ext2Paths fs("", 0);
    SuperBlock sb{};
    int32_t inodeIndex = -1;
    std::string content;
    if (!load_users_file(SessionManager::instance().current().id, fs, sb, inodeIndex, content, error)) {
        return "ERROR: mkusr -> " + error + "\n";
    }

    std::vector<GroupRecord> groups;
    std::vector<UserRecord> users;
    parse_users_content(content, groups, users);

    bool groupExists = false;
    for (const auto& group : groups) {
        if (group.id > 0 && group.name == params["grp"]) {
            groupExists = true;
            break;
        }
    }
    if (!groupExists) return "ERROR: mkusr -> no existe el grupo\n";

    for (const auto& user : users) {
        if (user.id > 0 && user.user == params["user"]) {
            return "ERROR: mkusr -> ya existe el usuario\n";
        }
    }

    UserRecord user;
    user.id = next_user_id(users);
    user.group = params["grp"];
    user.user = params["user"];
    user.pass = params["pass"];
    users.push_back(user);

    content = serialize_users_content(groups, users);
    if (!FsFileOps::writeFile(fs, sb, inodeIndex, content, error)) {
        return "ERROR: mkusr -> " + error + "\n";
    }
    MountEntry mounted{};
    if (MountManager::instance().getById(SessionManager::instance().current().id, mounted)) {
        JournalManager::append(mounted.path, mounted.start, sb, "mkusr", "/users.txt", user.user);
    }
    return "OK: mkusr -> usuario creado: " + user.user + "\n";
}

std::string RmUsrCmd::exec(const std::string& line) {
    auto params = parse_params(line);
    if (!params.count("user")) return "ERROR: rmusr -> falta -user\n";

    std::string error;
    if (!require_root_session(error)) return "ERROR: rmusr -> " + error + "\n";

    Ext2Paths fs("", 0);
    SuperBlock sb{};
    int32_t inodeIndex = -1;
    std::string content;
    if (!load_users_file(SessionManager::instance().current().id, fs, sb, inodeIndex, content, error)) {
        return "ERROR: rmusr -> " + error + "\n";
    }

    std::vector<GroupRecord> groups;
    std::vector<UserRecord> users;
    parse_users_content(content, groups, users);

    bool found = false;
    for (auto& user : users) {
        if (user.id > 0 && user.user == params["user"]) {
            user.id = 0;
            found = true;
            break;
        }
    }
    if (!found) return "ERROR: rmusr -> no existe el usuario\n";

    content = serialize_users_content(groups, users);
    if (!FsFileOps::writeFile(fs, sb, inodeIndex, content, error)) {
        return "ERROR: rmusr -> " + error + "\n";
    }
    MountEntry mounted{};
    if (MountManager::instance().getById(SessionManager::instance().current().id, mounted)) {
        JournalManager::append(mounted.path, mounted.start, sb, "rmusr", "/users.txt", params["user"]);
    }
    return "OK: rmusr -> usuario eliminado: " + params["user"] + "\n";
}

std::string ChGrpCmd::exec(const std::string& line) {
    auto params = parse_params(line);
    if (!params.count("user")) return "ERROR: chgrp -> falta -user\n";
    if (!params.count("grp")) return "ERROR: chgrp -> falta -grp\n";

    std::string error;
    if (!require_root_session(error)) return "ERROR: chgrp -> " + error + "\n";

    Ext2Paths fs("", 0);
    SuperBlock sb{};
    int32_t inodeIndex = -1;
    std::string content;
    if (!load_users_file(SessionManager::instance().current().id, fs, sb, inodeIndex, content, error)) {
        return "ERROR: chgrp -> " + error + "\n";
    }

    std::vector<GroupRecord> groups;
    std::vector<UserRecord> users;
    parse_users_content(content, groups, users);

    bool groupExists = false;
    for (const auto& group : groups) {
        if (group.id > 0 && group.name == params["grp"]) {
            groupExists = true;
            break;
        }
    }
    if (!groupExists) return "ERROR: chgrp -> no existe el grupo\n";

    bool found = false;
    for (auto& user : users) {
        if (user.id > 0 && user.user == params["user"]) {
            user.group = params["grp"];
            found = true;
            break;
        }
    }
    if (!found) return "ERROR: chgrp -> no existe el usuario\n";

    content = serialize_users_content(groups, users);
    if (!FsFileOps::writeFile(fs, sb, inodeIndex, content, error)) {
        return "ERROR: chgrp -> " + error + "\n";
    }
    MountEntry mounted{};
    if (MountManager::instance().getById(SessionManager::instance().current().id, mounted)) {
        JournalManager::append(mounted.path, mounted.start, sb, "chgrp", "/users.txt", params["user"] + "->" + params["grp"]);
    }
    return "OK: chgrp -> grupo actualizado para: " + params["user"] + "\n";
}
