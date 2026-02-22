#include "parser/CommandRunner.h"
#include "commands/MkDisk.h"

#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <string>

static inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static inline std::string tolower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

static inline std::string strip_quotes(std::string v) {
    v = trim(v);
    if (v.size() >= 2) {
        if ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')) {
            return v.substr(1, v.size() - 2);
        }
    }
    return v;
}

// Parseador simple de tokens -param=valor (soporta comillas sin espacios adentro)
static std::unordered_map<std::string, std::string> parse_params(const std::string& line) {
    std::unordered_map<std::string, std::string> p;
    std::istringstream iss(line);
    std::string token;

    // saltar comando
    iss >> token;

    while (iss >> token) {
        if (token.rfind("-", 0) != 0) continue;

        auto eq = token.find('=');
        if (eq == std::string::npos) {
            // flags tipo -p
            p[tolower_str(token.substr(1))] = "true";
        } else {
            std::string key = tolower_str(token.substr(1, eq - 1));
            std::string val = token.substr(eq + 1);
            p[key] = strip_quotes(val);
        }
    }
    return p;
}

std::string CommandRunner::run(const std::string& input) {
    std::ostringstream out;
    std::istringstream in(input);
    std::string line;

    while (std::getline(in, line)) {
        if (trim(line).empty()) { out << "\n"; continue; }

        std::string t = trim(line);

        // Comentarios: imprimir tal cual
        if (!t.empty() && t[0] == '#') {
            out << t << "\n";
            continue;
        }

        // Comando
        std::istringstream ls(t);
        std::string cmd;
        ls >> cmd;
        std::string c = tolower_str(cmd);

        if (c == "mkdisk") {
            auto params = parse_params(t);

            if (!params.count("size")) { out << "ERROR: mkdisk -> falta -size\n"; continue; }
            if (!params.count("path")) { out << "ERROR: mkdisk -> falta -path\n"; continue; }

            int size = 0;
            try { size = std::stoi(params["size"]); }
            catch (...) { out << "ERROR: mkdisk -> -size inválido\n"; continue; }

            char unit = 'M';
            if (params.count("unit") && !params["unit"].empty()) unit = params["unit"][0];

            char fit = 'F';
            if (params.count("fit") && !params["fit"].empty()) fit = params["fit"][0];

            std::string path = params["path"];

            MkDisk mk;
            out << mk.exec(size, unit, fit, path);
        }
        else {
            out << "ERROR: comando no reconocido -> " << cmd << "\n";
        }
    }

    return out.str();
}
