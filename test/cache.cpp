#include "pch.h"
#include <winmd_reader.h>

using namespace winmd::reader;

TEST_CASE("cache_add_invalidate")
{
    std::array<char, 260> local{};

#ifdef _WIN64
    ExpandEnvironmentStringsA("%windir%\\System32\\WinMetadata", local.data(), static_cast<uint32_t>(local.size()));
#else
    ExpandEnvironmentStringsA("%windir%\\SysNative\\WinMetadata", local.data(), static_cast<uint32_t>(local.size()));
#endif

    std::filesystem::path winmd_dir = local.data();
    auto file_path = winmd_dir;
    file_path.append("Windows.Foundation.winmd");

    cache c(file_path.string());

    // Get a type and a database and verify that neither are invalidated by adding a new db
    TypeDef IStringable = c.find("Windows.Foundation", "IStringable");
    auto const db = &(c.databases().front());

    file_path = winmd_dir;
    file_path.append("Windows.Data.winmd");
    c.add_database(file_path.string());

    TypeDef IStringable2 = c.find("Windows.Foundation", "IStringable");
    REQUIRE(IStringable == IStringable2);

    auto const db2 = &(c.databases().front());
    REQUIRE(db == db2);
}

TEST_CASE("cache_add")
{
    std::array<char, 260> local{};

#ifdef _WIN64
    ExpandEnvironmentStringsA("%windir%\\System32\\WinMetadata", local.data(), static_cast<uint32_t>(local.size()));
#else
    ExpandEnvironmentStringsA("%windir%\\SysNative\\WinMetadata", local.data(), static_cast<uint32_t>(local.size()));
#endif

    std::filesystem::path winmd_dir = local.data();
    auto file_path = winmd_dir;
    file_path.append("Windows.Foundation.winmd");

    cache c(file_path.string());

    TypeDef JsonValue = c.find("Windows.Data.Json", "JsonValue");
    REQUIRE(!JsonValue);

    file_path = winmd_dir;
    file_path.append("Windows.Data.winmd");
    c.add_database(file_path.string());

    JsonValue = c.find("Windows.Data.Json", "JsonValue");
    REQUIRE(!!JsonValue);
    REQUIRE(JsonValue.TypeName() == "JsonValue");
    REQUIRE(JsonValue.TypeNamespace() == "Windows.Data.Json");
}

TEST_CASE("cache_add_duplicate")
{
    std::array<char, 260> local{};

#ifdef _WIN64
    ExpandEnvironmentStringsA("%windir%\\System32\\WinMetadata", local.data(), static_cast<uint32_t>(local.size()));
#else
    ExpandEnvironmentStringsA("%windir%\\SysNative\\WinMetadata", local.data(), static_cast<uint32_t>(local.size()));
#endif

    std::filesystem::path winmd_dir = local.data();
    auto file_path = winmd_dir;
    file_path.append("Windows.Foundation.winmd");

    cache c(file_path.string());

    TypeDef IStringable = c.find("Windows.Foundation", "IStringable");

    // Add a winmd with duplicate types, and verify the original types aren't invalidated.
    c.add_database(file_path.string());

    TypeDef IStringable2 = c.find("Windows.Foundation", "IStringable");
    REQUIRE(IStringable == IStringable2);
}
