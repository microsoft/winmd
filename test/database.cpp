#include "pch.h"
#include <winmd_reader.h>

using namespace winmd::reader;

TEST_CASE("database")
{
    std::array<char, 260> local{};

#ifdef _WIN64
    ExpandEnvironmentStringsA("%windir%\\System32\\WinMetadata", local.data(), static_cast<uint32_t>(local.size()));
#else
    ExpandEnvironmentStringsA("%windir%\\SysNative\\WinMetadata", local.data(), static_cast<uint32_t>(local.size()));
#endif

    std::filesystem::path path = local.data();
    path.append("Windows.Foundation.winmd");
    database db(path.string());

    TypeDef stringable;

    for (auto&& type : db.TypeDef)
    {
        if (type.TypeName() == "IStringable")
        {
            stringable = type;
            break;
        }
    }

    REQUIRE(stringable.TypeName() == "IStringable");
    REQUIRE(stringable.TypeNamespace() == "Windows.Foundation");
    REQUIRE(stringable.Flags().WindowsRuntime());
    REQUIRE(stringable.Flags().Semantics() == TypeSemantics::Interface);

    auto methods = stringable.MethodList();
    REQUIRE(methods.first + 1 == methods.second);
    MethodDef method = methods.first;
    REQUIRE(method.Name() == "ToString");
}
