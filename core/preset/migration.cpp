#include "preset/migration.h"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace namfx {
namespace preset {

namespace {

struct Step {
    int from = 0;
    int to = 0;
    std::function<std::string(std::string)> fn;
};

std::vector<Step>& steps()
{
    static std::vector<Step> registry;
    return registry;
}

std::once_flag g_builtinFlag;

void registerBuiltinsOnce()
{
    registerMigration(0, 1, [](std::string text) {
        nlohmann::json doc = nlohmann::json::parse(text);
        doc["schema"] = 1;
        if (!doc.contains("scenes")) {
            doc["scenes"] = nlohmann::json::array();
        }
        return doc.dump();
    });
}

} // namespace

void ensureBuiltinMigrations()
{
    std::call_once(g_builtinFlag, registerBuiltinsOnce);
}

bool registerMigration(int fromSchema, int toSchema, std::function<std::string(std::string)> fn)
{
    if (!fn || fromSchema < 0 || toSchema <= fromSchema) {
        return false;
    }
    for (const Step& step : steps()) {
        if (step.from == fromSchema && step.to == toSchema) {
            return false;
        }
    }
    steps().push_back(Step{fromSchema, toSchema, std::move(fn)});
    return true;
}

std::string migrate(std::string jsonText, int& schema, std::string& error, int targetSchema)
{
    ensureBuiltinMigrations();
    if (schema < 0 || targetSchema < 0) {
        error = "invalid schema";
        return {};
    }
    while (schema < targetSchema) {
        bool found = false;
        for (const Step& step : steps()) {
            if (step.from == schema) {
                try {
                    jsonText = step.fn(std::move(jsonText));
                } catch (...) {
                    error = "migration failed at schema " + std::to_string(schema);
                    return {};
                }
                schema = step.to;
                found = true;
                break;
            }
        }
        if (!found) {
            error = "no migration from schema " + std::to_string(schema);
            return {};
        }
    }
    if (schema > targetSchema) {
        error = "preset schema newer than supported";
        return {};
    }
    return jsonText;
}

} // namespace preset
} // namespace namfx
