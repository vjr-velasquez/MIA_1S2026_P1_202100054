#include "api/Server.h"
#include "parser/CommandRunner.h"
#include "httplib.h"
#include "json.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using json = nlohmann::json;


static void add_cors(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    res.set_header("Access-Control-Max-Age", "86400"); // opcional
}

static std::string guess_content_type(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    if (ext == ".svg") return "image/svg+xml; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".pdf") return "application/pdf";
    if (ext == ".dot" || ext == ".txt") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

void Server::run(int port) {
    httplib::Server app;

    app.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        add_cors(res);
        res.status = 204;
    });

    app.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        add_cors(res);
        res.set_content("OK", "text/plain; charset=utf-8");
    });

    app.Get("/report", [](const httplib::Request& req, httplib::Response& res) {
        add_cors(res);

        if (!req.has_param("path")) {
            res.status = 400;
            res.set_content("Falta el parámetro path", "text/plain; charset=utf-8");
            return;
        }

        const std::filesystem::path requested = req.get_param_value("path");
        if (requested.empty()) {
            res.status = 400;
            res.set_content("Ruta de reporte vacía", "text/plain; charset=utf-8");
            return;
        }

        std::error_code ec;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(requested, ec);
        if (ec || normalized.empty()) {
            res.status = 400;
            res.set_content("Ruta de reporte inválida", "text/plain; charset=utf-8");
            return;
        }

        if (!std::filesystem::exists(normalized) || !std::filesystem::is_regular_file(normalized)) {
            res.status = 404;
            res.set_content("No se encontró el reporte solicitado", "text/plain; charset=utf-8");
            return;
        }

        std::ifstream file(normalized, std::ios::binary);
        if (!file.is_open()) {
            res.status = 500;
            res.set_content("No se pudo abrir el reporte solicitado", "text/plain; charset=utf-8");
            return;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        res.set_content(buffer.str(), guess_content_type(normalized));
    });

    app.Post("/execute", [](const httplib::Request& req, httplib::Response& res) {
        add_cors(res);
        json response;

        try {
            auto body = json::parse(req.body);
            std::string commands = body.value("commands", "");

            CommandRunner runner;
            response["output"] = runner.run(commands);

            res.set_content(response.dump(2), "application/json; charset=utf-8");
        } catch (const std::exception& ex) {
            res.status = 400;
            response["output"] = std::string("ERROR: ") + ex.what() + "\n";
            res.set_content(response.dump(2), "application/json; charset=utf-8");
        }
    });

    std::cout << "[mia_backend] Listening on http://0.0.0.0:" << port << "\n";
    app.listen("0.0.0.0", port);
}
