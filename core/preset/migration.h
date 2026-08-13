#pragma once

#include <functional>
#include <string>

namespace namfx {
namespace preset {

constexpr int kCurrentSchema = 1;

bool registerMigration(int fromSchema, int toSchema, std::function<std::string(std::string)> fn);

std::string migrate(std::string jsonText, int& schema, std::string& error,
                    int targetSchema = kCurrentSchema);

void ensureBuiltinMigrations();

} // namespace preset
} // namespace namfx
