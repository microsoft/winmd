#include "pch.h"
#include <winmd_reader.h>
#include <cstddef>
#include <cstring>
#include <fstream>

using namespace winmd::reader;
namespace wi = winmd::impl;

static std::filesystem::path get_local_winmd_path()
{
    std::array<char, 260> local{};

#ifdef _WIN64
    ExpandEnvironmentStringsA("%windir%\\System32\\WinMetadata", local.data(), static_cast<uint32_t>(local.size()));
#else
    ExpandEnvironmentStringsA("%windir%\\SysNative\\WinMetadata", local.data(), static_cast<uint32_t>(local.size()));
#endif

    return local.data();
}

static std::vector<uint8_t> load_foundation_bytes()
{
    auto path = get_local_winmd_path();
    path.append("Windows.Foundation.winmd");

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    REQUIRE(file.good());

    auto const file_size = file.tellg();
    REQUIRE(file_size > 0);

    std::vector<uint8_t> bytes(static_cast<size_t>(file_size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(file.gcount() == static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

template <typename T>
static T read_struct(std::vector<uint8_t> const& bytes, size_t const offset)
{
    REQUIRE(offset + sizeof(T) <= bytes.size());
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

template <typename T>
static void write_struct(std::vector<uint8_t>& bytes, size_t const offset, T const value)
{
    REQUIRE(offset + sizeof(T) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

static wi::image_section_header find_section(
    std::vector<uint8_t> const& bytes,
    uint32_t const sections_offset,
    uint16_t const section_count,
    uint32_t const rva)
{
    for (uint16_t i = 0; i < section_count; ++i)
    {
        auto const section = read_struct<wi::image_section_header>(
            bytes,
            static_cast<size_t>(sections_offset) + static_cast<size_t>(i) * sizeof(wi::image_section_header));

        if (rva >= section.VirtualAddress && (rva - section.VirtualAddress) < section.Misc.VirtualSize)
        {
            return section;
        }
    }

    FAIL("Required section was not found in baseline winmd");
    return {};
}

static uint32_t offset_from_rva(wi::image_section_header const& section, uint32_t const rva)
{
    REQUIRE(rva >= section.VirtualAddress);
    return section.PointerToRawData + (rva - section.VirtualAddress);
}

struct layout_info
{
    uint32_t pe_offset{};
    uint32_t sections_offset{};
    uint16_t section_count{};
    uint32_t metadata_offset{};
    uint32_t stream_headers_offset{};
    uint16_t stream_count{};
};

static layout_info inspect_layout(std::vector<uint8_t> const& bytes)
{
    auto const dos = read_struct<wi::image_dos_header>(bytes, 0);
    REQUIRE(dos.e_signature == 0x5A4D);
    REQUIRE(dos.e_lfanew >= 0);

    auto const pe_offset = static_cast<uint32_t>(dos.e_lfanew);
    auto const pe = read_struct<wi::image_nt_headers32>(bytes, pe_offset);
    REQUIRE(pe.FileHeader.NumberOfSections > 0);

    uint32_t sections_offset{};
    uint32_t com_virtual_address{};

    if (pe.OptionalHeader.Magic == 0x10B)
    {
        sections_offset = pe_offset + sizeof(wi::image_nt_headers32);
        com_virtual_address = pe.OptionalHeader.DataDirectory[14].VirtualAddress;
    }
    else
    {
        auto const pe_plus = read_struct<wi::image_nt_headers32plus>(bytes, pe_offset);
        REQUIRE(pe_plus.OptionalHeader.Magic == 0x20B);
        sections_offset = pe_offset + sizeof(wi::image_nt_headers32plus);
        com_virtual_address = pe_plus.OptionalHeader.DataDirectory[14].VirtualAddress;
    }

    auto const cli_section = find_section(bytes, sections_offset, pe.FileHeader.NumberOfSections, com_virtual_address);
    auto const cli_offset = offset_from_rva(cli_section, com_virtual_address);
    auto const cli = read_struct<wi::image_cor20_header>(bytes, cli_offset);

    auto const metadata_section = find_section(bytes, sections_offset, pe.FileHeader.NumberOfSections, cli.MetaData.VirtualAddress);
    auto const metadata_offset = offset_from_rva(metadata_section, cli.MetaData.VirtualAddress);

    auto const version_length = read_struct<uint32_t>(bytes, metadata_offset + 12);
    auto const stream_count = read_struct<uint16_t>(bytes, metadata_offset + version_length + 18);
    auto const stream_headers_offset = metadata_offset + version_length + 20;

    return { pe_offset, sections_offset, pe.FileHeader.NumberOfSections, metadata_offset, stream_headers_offset, stream_count };
}

TEST_CASE("database rejects negative e_lfanew")
{
    auto bytes = load_foundation_bytes();
    auto dos = read_struct<wi::image_dos_header>(bytes, 0);
    dos.e_lfanew = -1;
    write_struct(bytes, 0, dos);

    REQUIRE_THROWS_AS(database{ std::move(bytes) }, std::invalid_argument);
}

TEST_CASE("database rejects oversized section table")
{
    auto bytes = load_foundation_bytes();
    auto const layout = inspect_layout(bytes);

    auto file_header = read_struct<wi::image_file_header>(bytes, layout.pe_offset + offsetof(wi::image_nt_headers32, FileHeader));
    file_header.NumberOfSections = 100;
    write_struct(bytes, layout.pe_offset + offsetof(wi::image_nt_headers32, FileHeader), file_header);

    bytes.resize(layout.sections_offset + sizeof(wi::image_section_header));
    REQUIRE_THROWS_AS(database{ std::move(bytes) }, std::invalid_argument);
}

TEST_CASE("database rejects unterminated stream names")
{
    auto bytes = load_foundation_bytes();
    auto const layout = inspect_layout(bytes);
    REQUIRE(layout.stream_count > 0);

    auto const first_stream_name_offset = static_cast<size_t>(layout.stream_headers_offset) + 8;
    REQUIRE(first_stream_name_offset + 12 <= bytes.size());

    for (size_t i = 0; i < 12; ++i)
    {
        bytes[first_stream_name_offset + i] = 'A';
    }

    REQUIRE_THROWS_AS(database{ std::move(bytes) }, std::invalid_argument);
}

TEST_CASE("database rejects overflowing stream offset")
{
    auto bytes = load_foundation_bytes();
    auto const layout = inspect_layout(bytes);
    REQUIRE(layout.stream_count > 0);

    write_struct<uint32_t>(bytes, layout.stream_headers_offset, UINT32_MAX);
    REQUIRE_THROWS_AS(database{ std::move(bytes) }, std::invalid_argument);
}

TEST_CASE("database row access rejects end iterator index")
{
    auto path = get_local_winmd_path();
    path.append("Windows.Foundation.winmd");
    database db(path.string());

    auto invalid = db.TypeDef[db.TypeDef.size()];
    REQUIRE_THROWS_AS((void)invalid.TypeName(), std::invalid_argument);
}
