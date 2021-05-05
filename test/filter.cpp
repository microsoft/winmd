#include "pch.h"
#include <winmd_reader.h>

using namespace winmd::reader;

TEST_CASE("filter_simple")
{
    std::vector<std::string> include = { "N1", "N3", "N3.N4.N5" };
    std::vector<std::string> exclude = { "N2", "N3.N4" };

    filter f{ include, exclude };

    REQUIRE(!f.empty());

    REQUIRE(!f.includes("NN.T"));

    REQUIRE(f.includes("N1.T"));
    REQUIRE(f.includes("N3.T"));

    REQUIRE(!f.includes("N2.T"));
    REQUIRE(!f.includes("N3.N4.T"));

    REQUIRE(f.includes("N3.N4.N5.T"));
}

TEST_CASE("filter_excludes_same_length")
{
    std::vector<std::string> include = { "N.N1", "N.N2" };
    std::vector<std::string> exclude = { "N.N3", "N.N4" };

    filter f{ include, exclude };

    REQUIRE(!f.empty());

    REQUIRE(f.includes("N.N1.T"));
    REQUIRE(f.includes("N.N2.T"));

    REQUIRE(!f.includes("N.N3.T"));
    REQUIRE(!f.includes("N.N4.T"));
}
