#include "preset/migration.h"
#include "preset/preset_io.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("v0 document migrates to schema 1 with scenes added")
{
    const std::string v0 = R"({"name": "Legacy", "chain": []})";
    int schema = 0;
    std::string error;
    const std::string migrated = namfx::preset::migrate(v0, schema, error);
    REQUIRE(error.empty());
    REQUIRE(schema == 1);
    REQUIRE(migrated.find("\"schema\":1") != std::string::npos);
    REQUIRE(migrated.find("\"scenes\":[]") != std::string::npos);
}

TEST_CASE("migrating a current schema document is a no-op semantically")
{
    const std::string v1 = R"({"schema":1,"name":"Now","chain":[]})";
    int schema = 1;
    std::string error;
    const std::string migrated = namfx::preset::migrate(v1, schema, error);
    REQUIRE(error.empty());
    REQUIRE(schema == 1);
    REQUIRE(migrated.find("\"name\":\"Now\"") != std::string::npos);
}

TEST_CASE("schema with no migration path reports an error")
{
    const std::string doc = R"({"schema":5,"name":"Future","chain":[]})";
    int schema = 5;
    std::string error;
    namfx::preset::migrate(doc, schema, error);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("multi step migration chain walks to the target schema")
{
    namfx::preset::registerMigration(1, 2, [](std::string text) {
        return text.substr(0, text.size() - 1) + R"(,"addedByV2":true})";
    });
    namfx::preset::registerMigration(2, 3, [](std::string text) {
        return text.substr(0, text.size() - 1) + R"(,"addedByV3":true})";
    });

    const std::string v1 = R"({"schema":1,"name":"Walk","chain":[]})";
    int schema = 1;
    std::string error;
    const std::string migrated = namfx::preset::migrate(v1, schema, error, 3);
    REQUIRE(error.empty());
    REQUIRE(schema == 3);
    REQUIRE(migrated.find("\"addedByV2\":true") != std::string::npos);
    REQUIRE(migrated.find("\"addedByV3\":true") != std::string::npos);
}

TEST_CASE("duplicate migration registration is rejected")
{
    REQUIRE(namfx::preset::registerMigration(
        10, 11, [](std::string text) { return text; }));
    REQUIRE(namfx::preset::registerMigration(
        20, 21, [](std::string text) { return text; }));
    REQUIRE_FALSE(namfx::preset::registerMigration(
        20, 21, [](std::string text) { return text; }));
}

TEST_CASE("non forward migration registration is rejected")
{
    REQUIRE_FALSE(namfx::preset::registerMigration(
        5, 5, [](std::string text) { return text; }));
    REQUIRE_FALSE(namfx::preset::registerMigration(
        5, 4, [](std::string text) { return text; }));
    REQUIRE_FALSE(namfx::preset::registerMigration(
        4, 5, nullptr));
}
