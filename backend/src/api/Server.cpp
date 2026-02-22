#include "api/Server.h"
#include "parser/CommandRunner.h"
#include "httplib.h"
#include "json.hpp"

#include <iostream>

using json = nlohmann::json;

static void add_cors(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
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
