#include "pch.h"
#include <winmd_reader.h>

using namespace winmd::reader;

std::filesystem::path get_local_winmd_path()
{
    std::array<char, 260> local{};

#ifdef _WIN64
    ExpandEnvironmentStringsA("%windir%\\System32\\WinMetadata", local.data(), static_cast<uint32_t>(local.size()));
#else
    ExpandEnvironmentStringsA("%windir%\\SysNative\\WinMetadata", local.data(), static_cast<uint32_t>(local.size()));
#endif

    return local.data();
}

TEST_CASE("cache_add_invalidate")
{
    std::filesystem::path winmd_dir = get_local_winmd_path();
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
    std::filesystem::path winmd_dir = get_local_winmd_path();
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

TEST_CASE("cache_add_filter")
{
    std::filesystem::path winmd_dir = get_local_winmd_path();
    auto file_path = winmd_dir;
    file_path.append("Windows.Foundation.winmd");

    cache c(file_path.string());

    TypeDef JsonValue = c.find("Windows.Data.Json", "JsonValue");
    REQUIRE(!JsonValue);

    file_path = winmd_dir;
    file_path.append("Windows.Data.winmd");
    c.add_database(file_path.string(), [](TypeDef const& type)
        {
            return !(type.TypeNamespace() == "Windows.Data.Json" && type.TypeName() == "JsonArray");
        });

    JsonValue = c.find("Windows.Data.Json", "JsonValue");
    REQUIRE(!!JsonValue);
    REQUIRE(JsonValue.TypeName() == "JsonValue");
    REQUIRE(JsonValue.TypeNamespace() == "Windows.Data.Json");

    REQUIRE(!c.find("Windows.Data.Json", "JsonArray"));
}

TEST_CASE("cache_add_duplicate")
{
    std::filesystem::path winmd_dir = get_local_winmd_path();
    auto file_path = winmd_dir;
    file_path.append("Windows.Foundation.winmd");

    cache c(file_path.string());

    TypeDef IStringable = c.find("Windows.Foundation", "IStringable");

    // Add a winmd with duplicate types, and verify the original types aren't invalidated.
    c.add_database(file_path.string());

    TypeDef IStringable2 = c.find("Windows.Foundation", "IStringable");
    REQUIRE(IStringable == IStringable2);
}

bool caches_equal(cache const& lhs, cache const& rhs)
{
    if (lhs.namespaces().size() != rhs.namespaces().size())
        return false;

    auto compare_typedef_names = [](TypeDef const& lhs, TypeDef const& rhs)
    {
        return lhs.TypeName() == rhs.TypeName() && lhs.TypeNamespace() == rhs.TypeNamespace();
    };

    auto compare_members = [compare_typedef_names](std::vector<TypeDef> const& lhs, std::vector<TypeDef> const& rhs)
    {
        return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), compare_typedef_names);
    };

    for (auto iter1 = lhs.namespaces().begin(), iter2 = rhs.namespaces().begin();
        iter1 != lhs.namespaces().end() && iter2 != rhs.namespaces().end();
        ++iter1, ++iter2)
    {
        if (iter1->first != iter2->first)
            return false;

        if (!(compare_members(iter1->second.attributes, iter2->second.attributes)) &&
            compare_members(iter1->second.classes, iter2->second.classes) &&
            compare_members(iter1->second.contracts, iter2->second.contracts) &&
            compare_members(iter1->second.delegates, iter2->second.delegates) &&
            compare_members(iter1->second.enums, iter2->second.enums) &&
            compare_members(iter1->second.interfaces, iter2->second.interfaces) &&
            compare_members(iter1->second.structs, iter2->second.structs))
            return false;


        if (!std::equal(iter1->second.types.begin(), iter1->second.types.end(),
            iter2->second.types.begin(), iter2->second.types.end(),
            [](auto const& lhs, auto const& rhs)
            {
                return lhs.first == rhs.first;
            }))
            return false;
    }
    return true;
}

TEST_CASE("cache_filter")
{
    std::filesystem::path winmd_dir = get_local_winmd_path();
    auto file_path = winmd_dir;
    file_path.append("Windows.Foundation.winmd");

    cache const unfiltered(file_path.string());

    {
        cache allow_all(file_path.string(), [](TypeDef const&) { return true; });
        REQUIRE(caches_equal(unfiltered, allow_all));
    }

    {
        cache allow_none(file_path.string(), [](TypeDef const&) { return false; });
        REQUIRE(allow_none.namespaces().empty());
    }

    {
        cache allow_winrt(file_path.string(), [](TypeDef const& type)
            {
                return type.Flags().WindowsRuntime();
            });
        REQUIRE(caches_equal(unfiltered, allow_winrt));
    }

    {
        auto const type_namespace = "Windows.Foundation";
        auto const type_name = "HResult";
        cache filter_hresult(file_path.string(), [type_namespace, type_name](TypeDef const& type)
            {
                return !(type.TypeName() == type_name && type.TypeNamespace() == type_namespace);
            });

        REQUIRE(unfiltered.find(type_namespace, type_name));
        REQUIRE(!filter_hresult.find(type_namespace, type_name));
    }
}
