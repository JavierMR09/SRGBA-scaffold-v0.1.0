#include "srgba/core/cartridge.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace srgba::core {
namespace {

constexpr std::size_t kTitleOffset = 0xA0;
constexpr std::size_t kTitleLength = 12;
constexpr std::size_t kGameCodeOffset = 0xAC;
constexpr std::size_t kGameCodeLength = 4;
constexpr std::size_t kMakerCodeOffset = 0xB0;
constexpr std::size_t kMakerCodeLength = 2;
constexpr std::size_t kFixedValueOffset = 0xB2;
constexpr std::size_t kUnitCodeOffset = 0xB3;
constexpr std::size_t kSoftwareVersionOffset = 0xBC;
constexpr std::size_t kChecksumOffset = 0xBD;

[[nodiscard]] std::string read_ascii_field(const std::span<const std::uint8_t> bytes,
                                           const std::size_t offset, const std::size_t length) {
    std::string value;
    value.reserve(length);

    for (std::size_t index = 0; index < length; ++index) {
        const auto character = bytes[offset + index];
        if (character == 0) {
            break;
        }

        const bool printable = character >= 0x20 && character <= 0x7E;
        value.push_back(printable ? static_cast<char>(character) : '?');
    }

    while (!value.empty() && value.back() == ' ') {
        value.pop_back();
    }
    return value;
}

[[nodiscard]] std::uint8_t
calculate_header_checksum(const std::span<const std::uint8_t> bytes) noexcept {
    std::uint8_t checksum = 0;
    for (std::size_t offset = kTitleOffset; offset <= kSoftwareVersionOffset; ++offset) {
        checksum = static_cast<std::uint8_t>(checksum - bytes[offset]);
    }
    return static_cast<std::uint8_t>(checksum - 0x19U);
}

} // namespace

RomHeader parse_rom_header(const std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kGbaHeaderSize) {
        throw std::invalid_argument("The file is too small to contain a GBA cartridge header.");
    }

    RomHeader header;
    header.title = read_ascii_field(bytes, kTitleOffset, kTitleLength);
    header.game_code = read_ascii_field(bytes, kGameCodeOffset, kGameCodeLength);
    header.maker_code = read_ascii_field(bytes, kMakerCodeOffset, kMakerCodeLength);
    header.unit_code = bytes[kUnitCodeOffset];
    header.software_version = bytes[kSoftwareVersionOffset];
    header.stored_checksum = bytes[kChecksumOffset];
    header.calculated_checksum = calculate_header_checksum(bytes);
    header.fixed_value_valid = bytes[kFixedValueOffset] == 0x96U;
    header.checksum_valid = header.stored_checksum == header.calculated_checksum;
    return header;
}

Cartridge Cartridge::load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("SRGBA could not open the selected file.");
    }

    const auto end_position = input.tellg();
    if (end_position < 0) {
        throw std::runtime_error("SRGBA could not determine the ROM size.");
    }

    const auto file_size = static_cast<std::uintmax_t>(end_position);
    if (file_size < kGbaHeaderSize) {
        throw std::runtime_error("The selected file is too small to be a GBA ROM.");
    }
    if (file_size > kMaximumRomSize) {
        throw std::runtime_error(
            "The selected file is larger than the 32 MiB GBA ROM address space.");
    }
    if (file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("The selected file is too large for this build to read safely.");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) {
        throw std::runtime_error("SRGBA could not read the complete ROM file.");
    }

    auto header = parse_rom_header(bytes);
    return Cartridge(path, std::move(bytes), std::move(header));
}

Cartridge::Cartridge(std::filesystem::path path, std::vector<std::uint8_t> bytes, RomHeader header)
    : path_(std::move(path)), bytes_(std::move(bytes)), header_(std::move(header)) {}

const RomHeader& Cartridge::header() const noexcept {
    return header_;
}

const std::filesystem::path& Cartridge::path() const noexcept {
    return path_;
}

std::span<const std::uint8_t> Cartridge::bytes() const noexcept {
    return bytes_;
}

std::size_t Cartridge::size() const noexcept {
    return bytes_.size();
}

} // namespace srgba::core
