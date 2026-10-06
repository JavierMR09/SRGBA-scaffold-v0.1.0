#include "srgba/core/cheats.hpp"

#include "srgba/core/gba_bus.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <utility>

namespace srgba::core {
namespace {

// Device keys: GameShark / Action Replay v1-v2 and Action Replay v3 (default seeds; the
// DEADFACE code that switches to other seeds is not supported).
constexpr std::array<std::uint32_t, 4> kGameSharkSeeds{0x09F4FBBDU, 0x9681884AU, 0x352027E9U,
                                                       0xF3DEE5A7U};
constexpr std::array<std::uint32_t, 4> kActionReplayV3Seeds{0x7AA9648FU, 0x7FAE6994U, 0xC0EFAAD5U,
                                                            0x42712C57U};
constexpr std::uint32_t kTeaDelta = 0x9E3779B9U;
constexpr std::uint32_t kTeaRounds = 32;
constexpr std::uint32_t kDeviceIdValue = 0x001DC0DEU;
constexpr std::uint32_t kSeedChangeAddress = 0xDEADFACEU;
// A single fill or slide never writes more than all of work RAM.
constexpr std::uint32_t kMaximumWriteCount = 256U * 1024U;
constexpr std::size_t kMaximumCheatsInFile = 4096;

using Operation = CheatOperation;

const std::array<std::uint32_t, 4>& seeds_for(const CheatFormat format) noexcept {
    return format == CheatFormat::ActionReplayV3 ? kActionReplayV3Seeds : kGameSharkSeeds;
}

[[nodiscard]] bool in_work_ram(const std::uint32_t address) noexcept {
    return (address >= 0x02000000U && address < 0x02040000U) ||
           (address >= 0x03000000U && address < 0x03008000U);
}

[[nodiscard]] std::uint32_t width_mask(const std::uint8_t width) noexcept {
    return width >= 4U ? 0xFFFFFFFFU : (1U << (width * 8U)) - 1U;
}

[[nodiscard]] std::int32_t sign_extend(const std::uint32_t value,
                                       const std::uint8_t width) noexcept {
    const auto shift = 32U - width * 8U;
    return static_cast<std::int32_t>(value << shift) >> shift;
}

[[nodiscard]] std::string hex32(const std::uint32_t value) {
    std::array<char, 9> text{};
    std::snprintf(text.data(), text.size(), "%08X", value);
    return text.data();
}

[[nodiscard]] std::string hex_digits(const std::uint32_t value, const int digits) {
    std::array<char, 9> text{};
    std::snprintf(text.data(), text.size(), "%0*X", digits, value);
    return text.data();
}

// ---------------------------------------------------------------------------------------------
// Tokenizing

struct CodeLine {
    enum class Shape : std::uint8_t {
        Raw,   // AAAAAAAA:VV..
        Short, // AAAAAAAA VVVV
        Long,  // AAAAAAAA VVVVVVVV
    };
    Shape shape{Shape::Long};
    std::uint32_t address{};
    std::uint32_t value{};
    std::uint8_t raw_width{}; // bytes, raw codes only

    [[nodiscard]] std::string text() const {
        switch (shape) {
        case Shape::Raw:
            return hex32(address) + ":" + hex_digits(value, raw_width * 2);
        case Shape::Short:
            return hex32(address) + " " + hex_digits(value, 4);
        case Shape::Long:
            break;
        }
        return hex32(address) + " " + hex32(value);
    }
};

[[nodiscard]] bool is_hex(const char character) noexcept {
    return (character >= '0' && character <= '9') || (character >= 'A' && character <= 'F') ||
           (character >= 'a' && character <= 'f');
}

[[nodiscard]] bool all_hex(const std::string_view text) noexcept {
    return !text.empty() && std::all_of(text.begin(), text.end(), is_hex);
}

[[nodiscard]] std::uint32_t parse_hex(const std::string_view text) noexcept {
    std::uint32_t value = 0;
    for (const char character : text) {
        std::uint32_t digit = 0;
        if (character >= '0' && character <= '9') {
            digit = static_cast<std::uint32_t>(character - '0');
        } else if (character >= 'A' && character <= 'F') {
            digit = static_cast<std::uint32_t>(character - 'A' + 10);
        } else {
            digit = static_cast<std::uint32_t>(character - 'a' + 10);
        }
        value = (value << 4U) | digit;
    }
    return value;
}

[[nodiscard]] bool is_separator(const char character) noexcept {
    return character == ' ' || character == '\t' || character == '\r' || character == '\n' ||
           character == '+' || character == '-' || character == ',';
}

std::vector<std::string_view> split_tokens(const std::string_view code) {
    std::vector<std::string_view> tokens;
    bool in_comment = false;
    std::size_t start = std::string_view::npos;
    for (std::size_t index = 0; index <= code.size(); ++index) {
        const char character = index < code.size() ? code[index] : '\n';
        if (in_comment) {
            in_comment = character != '\n';
            continue;
        }
        const bool comment = character == '#';
        if (is_separator(character) || comment) {
            if (start != std::string_view::npos) {
                tokens.push_back(code.substr(start, index - start));
                start = std::string_view::npos;
            }
            in_comment = comment;
            continue;
        }
        if (start == std::string_view::npos) {
            start = index;
        }
    }
    return tokens;
}

std::optional<std::vector<CodeLine>> tokenize(const std::string_view code, std::string& error) {
    const auto tokens = split_tokens(code);
    std::vector<CodeLine> lines;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const auto token = tokens[index];
        const auto describe = [&](const std::string_view problem) {
            error = "\"" + std::string(token) + "\" " + std::string(problem);
        };

        if (const auto colon = token.find(':'); colon != std::string_view::npos) {
            const auto address = token.substr(0, colon);
            const auto value = token.substr(colon + 1U);
            if (!all_hex(address) || address.size() > 8U || !all_hex(value) ||
                (value.size() != 2U && value.size() != 4U && value.size() != 8U)) {
                describe("is not a valid address:value code (use 2, 4 or 8 value digits).");
                return std::nullopt;
            }
            lines.push_back({CodeLine::Shape::Raw, parse_hex(address), parse_hex(value),
                             static_cast<std::uint8_t>(value.size() / 2U)});
            continue;
        }
        if (!all_hex(token)) {
            describe("is not a hexadecimal cheat code.");
            return std::nullopt;
        }
        if (token.size() == 16U) {
            lines.push_back({CodeLine::Shape::Long, parse_hex(token.substr(0, 8)),
                             parse_hex(token.substr(8)), 0});
            continue;
        }
        if (token.size() == 12U) {
            lines.push_back({CodeLine::Shape::Short, parse_hex(token.substr(0, 8)),
                             parse_hex(token.substr(8)), 0});
            continue;
        }
        if (token.size() == 8U && index + 1U < tokens.size() && all_hex(tokens[index + 1U]) &&
            (tokens[index + 1U].size() == 8U || tokens[index + 1U].size() == 4U)) {
            const auto value = tokens[index + 1U];
            lines.push_back({value.size() == 8U ? CodeLine::Shape::Long : CodeLine::Shape::Short,
                             parse_hex(token), parse_hex(value), 0});
            ++index;
            continue;
        }
        describe(token.size() == 8U ? "is missing its value part."
                                    : "is not a complete cheat code.");
        return std::nullopt;
    }
    if (lines.empty()) {
        error = "Enter at least one code.";
        return std::nullopt;
    }
    return lines;
}

// ---------------------------------------------------------------------------------------------
// Decoding

struct DecodeFailure {
    std::size_t line{};
    std::string message;
};

[[nodiscard]] std::string line_error(const std::vector<CodeLine>& lines, const std::size_t line,
                                     const std::string_view message) {
    return "Line " + std::to_string(line + 1U) + " (" + lines[line].text() +
           "): " + std::string(message);
}

Operation make_write(const std::uint8_t width, const std::uint32_t address,
                     const std::uint32_t value) noexcept {
    Operation operation;
    operation.kind = Operation::Kind::Write;
    operation.width = width;
    operation.address = address;
    operation.value = value & width_mask(width);
    return operation;
}

Operation make_modify(const Operation::Kind kind, const std::uint8_t width,
                      const std::uint32_t address, const std::uint32_t value) noexcept {
    auto operation = make_write(width, address, value);
    operation.kind = kind;
    return operation;
}

Operation make_condition(const Operation::Compare compare, const std::uint8_t width,
                         const std::uint32_t address, const std::uint32_t value,
                         const std::uint32_t skip) noexcept {
    Operation operation;
    operation.kind = Operation::Kind::Condition;
    operation.compare = compare;
    operation.width = width;
    operation.address = address;
    operation.value = value & width_mask(width);
    operation.skip = skip;
    return operation;
}

Operation make_rom_patch(const std::uint32_t address, const std::uint16_t value) noexcept {
    Operation operation;
    operation.kind = Operation::Kind::RomPatch;
    operation.width = 2;
    operation.address = address;
    operation.value = value;
    return operation;
}

std::string game_code_from(const std::uint32_t word) {
    std::string code;
    for (unsigned index = 0; index < 4U; ++index) {
        const auto character = static_cast<char>((word >> (index * 8U)) & 0xFFU);
        code.push_back(character >= ' ' && character <= '~' ? character : '?');
    }
    return code;
}

std::optional<CheatProgram> decode_raw(const std::vector<CodeLine>& lines) {
    CheatProgram program;
    program.format = CheatFormat::Raw;
    for (const auto& line : lines) {
        program.operations.push_back(make_write(line.raw_width, line.address, line.value));
    }
    return program;
}

std::optional<CheatProgram> decode_codebreaker(const std::vector<CodeLine>& lines,
                                               DecodeFailure& failure) {
    CheatProgram program;
    program.format = CheatFormat::CodeBreaker;
    using Compare = Operation::Compare;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto& line = lines[index];
        const auto type = line.address >> 28U;
        const auto address = line.address & 0x0FFFFFFFU;
        const auto value = line.value & 0xFFFFU;
        const auto fail = [&](const std::string_view message) {
            failure = {index, std::string(message)};
            return std::nullopt;
        };

        switch (type) {
        case 0x0: // game ID / CRC check
        case 0x1: // master code hook
            program.operations.emplace_back();
            break;
        case 0x2:
            program.operations.push_back(make_modify(Operation::Kind::Or, 2, address, value));
            break;
        case 0x3:
            program.operations.push_back(make_write(1, address, value));
            break;
        case 0x4: { // slide: write, then step the address and value
            if (index + 1U >= lines.size()) {
                return fail("This slide code is missing its second line.");
            }
            const auto& step = lines[++index];
            auto operation = make_write(2, address, value);
            operation.count = step.address & 0xFFFFU;
            operation.value_step = step.address >> 16U;
            operation.address_step = step.value & 0xFFFFU;
            program.operations.push_back(operation);
            break;
        }
        case 0x5: { // super code: 2 * value bytes follow, six per line
            const auto byte_count = value * 2U;
            Operation operation;
            operation.kind = Operation::Kind::WriteBytes;
            operation.address = address;
            operation.count = byte_count;
            operation.data_offset = static_cast<std::uint32_t>(program.data.size());
            for (std::uint32_t written = 0; written < byte_count; written += 6U) {
                if (index + 1U >= lines.size()) {
                    return fail("This code is missing some of its data lines.");
                }
                const auto& data = lines[++index];
                const std::array<std::uint8_t, 6> bytes{
                    static_cast<std::uint8_t>(data.address >> 24U),
                    static_cast<std::uint8_t>(data.address >> 16U),
                    static_cast<std::uint8_t>(data.address >> 8U),
                    static_cast<std::uint8_t>(data.address),
                    static_cast<std::uint8_t>(data.value >> 8U),
                    static_cast<std::uint8_t>(data.value),
                };
                const auto take = std::min<std::uint32_t>(6U, byte_count - written);
                program.data.insert(program.data.end(), bytes.begin(), bytes.begin() + take);
            }
            program.operations.push_back(operation);
            break;
        }
        case 0x6:
            program.operations.push_back(make_modify(Operation::Kind::And, 2, address, value));
            break;
        case 0x7:
            program.operations.push_back(make_condition(Compare::Equal, 2, address, value, 1));
            break;
        case 0x8:
            program.operations.push_back(make_write(2, address, value));
            break;
        case 0x9:
            return fail("Encrypted CodeBreaker codes are not supported yet.");
        case 0xA:
            program.operations.push_back(make_condition(Compare::NotEqual, 2, address, value, 1));
            break;
        case 0xB:
            program.operations.push_back(make_condition(Compare::Greater, 2, address, value, 1));
            break;
        case 0xC:
            program.operations.push_back(make_condition(Compare::Less, 2, address, value, 1));
            break;
        case 0xD: {
            if (line.address != 0xD0000020U) {
                return fail("Only the D0000020 button condition is supported.");
            }
            Operation operation;
            operation.kind = Operation::Kind::KeyCondition;
            operation.value = value & 0x03FFU;
            operation.skip = 1;
            program.operations.push_back(operation);
            break;
        }
        case 0xE: { // add a signed 16-bit amount; odd addresses select a 32-bit target
            const auto amount = static_cast<std::uint32_t>(static_cast<std::int32_t>(
                static_cast<std::int16_t>(static_cast<std::uint16_t>(value))));
            if ((address & 1U) != 0U) {
                program.operations.push_back(
                    make_modify(Operation::Kind::Add, 4, address & ~1U, amount));
            } else {
                program.operations.push_back(make_modify(Operation::Kind::Add, 2, address, value));
            }
            break;
        }
        default: // 0xF
            program.operations.push_back(make_condition(Compare::AndNonZero, 2, address, value, 1));
            break;
        }
    }
    return program;
}

std::optional<CheatProgram> decode_gameshark(const std::vector<CodeLine>& lines,
                                             DecodeFailure& failure, int& score) {
    CheatProgram program;
    program.format = CheatFormat::GameShark;
    using Compare = Operation::Compare;
    constexpr std::array<Compare, 4> kCompares{Compare::Equal, Compare::NotEqual,
                                               Compare::LessOrEqual, Compare::GreaterOrEqual};
    for (std::size_t index = 0; index < lines.size(); ++index) {
        auto address = lines[index].address;
        auto value = lines[index].value;
        decrypt_device_code(address, value, CheatFormat::GameShark);
        const auto fail = [&](const std::string_view message) {
            failure = {index, std::string(message)};
            return std::nullopt;
        };

        if (value == kDeviceIdValue) {
            program.game_code = game_code_from(address);
            program.operations.emplace_back();
            score += 4;
            continue;
        }
        if (address == kSeedChangeAddress) {
            return fail("Codes that change the encryption key (DEADFACE) are not supported yet.");
        }
        const auto target = address & 0x0FFFFFFFU;
        switch (address >> 28U) {
        case 0x0:
        case 0x1:
        case 0x2: {
            const auto width = static_cast<std::uint8_t>(1U << (address >> 28U));
            if (in_work_ram(target) && (value & ~width_mask(width)) == 0U) {
                ++score;
            }
            program.operations.push_back(make_write(width, target, value));
            break;
        }
        case 0x6: {
            if ((value >> 24U) != 0U) {
                return fail("Only the first ROM patch slot is supported.");
            }
            const auto rom_address = (target << 1U) & 0x0FFFFFFFU;
            if (rom_address < GbaBus::kGamePakStart || rom_address >= 0x0A000000U) {
                return fail("This ROM patch points outside the cartridge.");
            }
            ++score;
            program.operations.push_back(
                make_rom_patch(rom_address, static_cast<std::uint16_t>(value)));
            break;
        }
        case 0x8:
            return fail("Codes activated by the GameShark's button are not supported.");
        case 0xD: {
            const auto mode = (value >> 20U) & 0xFU;
            if (mode >= kCompares.size()) {
                return fail("This condition type is not supported.");
            }
            if (in_work_ram(target)) {
                ++score;
            }
            program.operations.push_back(make_condition(kCompares[mode], 2, target, value, 1));
            break;
        }
        case 0xE: {
            const auto mode = value >> 28U;
            const auto condition_address = value & 0x0FFFFFFFU;
            if (mode >= kCompares.size()) {
                return fail("This condition type is not supported.");
            }
            if (in_work_ram(condition_address)) {
                ++score;
            }
            program.operations.push_back(make_condition(kCompares[mode], 2, condition_address,
                                                        address & 0xFFFFU,
                                                        (address >> 16U) & 0xFFU));
            break;
        }
        case 0xF: // master code hook
            score += 2;
            program.operations.emplace_back();
            break;
        default:
            return fail("GameShark code type " + hex_digits(address >> 28U, 1) +
                        " is not supported.");
        }
    }
    return program;
}

std::optional<CheatProgram> decode_action_replay_v3(const std::vector<CodeLine>& lines,
                                                    DecodeFailure& failure, int& score) {
    CheatProgram program;
    program.format = CheatFormat::ActionReplayV3;
    using Compare = Operation::Compare;
    constexpr std::array<Compare, 8> kCompares{
        Compare::Equal,         Compare::Equal, Compare::NotEqual, Compare::LessSigned,
        Compare::GreaterSigned, Compare::Less,  Compare::Greater,  Compare::AndNonZero,
    };
    for (std::size_t index = 0; index < lines.size(); ++index) {
        auto address = lines[index].address;
        auto value = lines[index].value;
        decrypt_device_code(address, value, CheatFormat::ActionReplayV3);
        const auto fail = [&](const std::string_view message) {
            failure = {index, std::string(message)};
            return std::nullopt;
        };

        if (value == kDeviceIdValue) {
            program.game_code = game_code_from(address);
            program.operations.emplace_back();
            score += 4;
            continue;
        }
        if (address == kSeedChangeAddress) {
            return fail("Codes that change the encryption key (DEADFACE) are not supported yet.");
        }
        if (((address >> 24U) & 0xFEU) == 0xC4U) { // master code hook
            score += 2;
            program.operations.emplace_back();
            continue;
        }
        if (address == 0U) {
            if (value == 0U) {
                program.operations.emplace_back();
                continue;
            }
            const auto special = (value >> 25U) & 0x7FU;
            if (special < 0x0CU || special > 0x0FU) {
                return fail("This Action Replay special code is not supported.");
            }
            if (index + 1U >= lines.size()) {
                return fail("This ROM patch is missing its second line.");
            }
            auto patch_address = lines[index + 1U].address;
            auto patch_value = lines[index + 1U].value;
            decrypt_device_code(patch_address, patch_value, CheatFormat::ActionReplayV3);
            ++index;
            ++score;
            program.operations.push_back(
                make_rom_patch(((value & 0x00FFFFFFU) << 1U) + GbaBus::kGamePakStart,
                               static_cast<std::uint16_t>(patch_address)));
            continue;
        }

        const auto type = ((address >> 25U) & 0x7FU) | ((address >> 17U) & 0x80U);
        if ((type & 0x80U) != 0U) {
            return fail("Action Replay IO register codes are not supported.");
        }
        const auto target = ((address & 0x00F00000U) << 4U) | (address & 0x0003FFFFU);
        const auto width_code = type & 3U;
        const auto kind = (type >> 2U) & 7U;
        const auto mode = (type >> 5U) & 3U;
        const auto width = static_cast<std::uint8_t>(1U << width_code);
        if (mode == 3U || (width_code == 3U && kind != 1U)) {
            return fail("Action Replay code type " + hex_digits(type, 2) + " is not supported.");
        }
        if (in_work_ram(target)) {
            ++score;
        }

        if (kind == 0U) {
            if (width_code == 3U) {
                return fail("Action Replay code type " + hex_digits(type, 2) +
                            " is not supported.");
            }
            if (mode == 0U) { // fills (8/16-bit) and 32-bit writes
                auto operation = make_write(width, target, value);
                if (width == 1U) {
                    operation.count = (value >> 8U) + 1U;
                } else if (width == 2U) {
                    operation.count = (value >> 16U) + 1U;
                }
                operation.address_step = width;
                if (operation.count > kMaximumWriteCount) {
                    return fail("This fill code covers more memory than the GBA has.");
                }
                program.operations.push_back(operation);
            } else if (mode == 1U) { // write through a pointer
                auto operation = make_write(width, target, value);
                operation.kind = Operation::Kind::PointerWrite;
                operation.address_step =
                    width == 1U ? (value >> 8U) : (width == 2U ? (value >> 16U) << 1U : 0U);
                program.operations.push_back(operation);
            } else {
                program.operations.push_back(
                    make_modify(Operation::Kind::Add, width, target, value));
            }
            continue;
        }

        const auto skip = mode == 0U ? 1U : (mode == 1U ? 2U : Operation::kSkipRest);
        if (width_code == 3U) { // kind 1: unconditional skip
            Operation operation;
            operation.kind = Operation::Kind::Skip;
            operation.skip = skip;
            program.operations.push_back(operation);
            continue;
        }
        program.operations.push_back(make_condition(kCompares[kind], width, target, value, skip));
    }
    return program;
}

std::string normalized_code(const std::vector<CodeLine>& lines) {
    std::string text;
    for (const auto& line : lines) {
        if (!text.empty()) {
            text.push_back('\n');
        }
        text += line.text();
    }
    return text;
}

// ---------------------------------------------------------------------------------------------
// Execution

[[nodiscard]] bool condition_holds(const Operation& operation,
                                   const std::uint32_t memory) noexcept {
    const auto value = operation.value;
    switch (operation.compare) {
    case Operation::Compare::Equal:
        return memory == value;
    case Operation::Compare::NotEqual:
        return memory != value;
    case Operation::Compare::Less:
        return memory < value;
    case Operation::Compare::Greater:
        return memory > value;
    case Operation::Compare::LessOrEqual:
        return memory <= value;
    case Operation::Compare::GreaterOrEqual:
        return memory >= value;
    case Operation::Compare::LessSigned:
        return sign_extend(memory, operation.width) < sign_extend(value, operation.width);
    case Operation::Compare::GreaterSigned:
        return sign_extend(memory, operation.width) > sign_extend(value, operation.width);
    case Operation::Compare::AndNonZero:
        return (memory & value) != 0U;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Cheat files

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                             text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] std::string quote_value(const std::string_view text) {
    std::string result = "\"";
    for (const char character : text) {
        if (character == '"') {
            result.push_back('\'');
        } else if (character == '\n' || character == '\r') {
            result.push_back(' ');
        } else {
            result.push_back(character);
        }
    }
    result.push_back('"');
    return result;
}

struct FileCheat {
    std::string description;
    std::string code;
    std::string format;
    bool enabled{};
    bool has_code{};
};

} // namespace

std::string_view cheat_format_name(const CheatFormat format) noexcept {
    switch (format) {
    case CheatFormat::Auto:
        return "Automatic";
    case CheatFormat::Raw:
        return "Raw (address:value)";
    case CheatFormat::CodeBreaker:
        return "CodeBreaker";
    case CheatFormat::GameShark:
        return "GameShark / AR v1-v2";
    case CheatFormat::ActionReplayV3:
        return "Action Replay v3";
    }
    return "Unknown";
}

std::string_view cheat_format_id(const CheatFormat format) noexcept {
    switch (format) {
    case CheatFormat::Auto:
        return "auto";
    case CheatFormat::Raw:
        return "raw";
    case CheatFormat::CodeBreaker:
        return "codebreaker";
    case CheatFormat::GameShark:
        return "gameshark";
    case CheatFormat::ActionReplayV3:
        return "actionreplay3";
    }
    return "auto";
}

std::optional<CheatFormat> cheat_format_from_id(const std::string_view id) noexcept {
    for (const auto format : kCheatFormats) {
        if (cheat_format_id(format) == id) {
            return format;
        }
    }
    return std::nullopt;
}

void decrypt_device_code(std::uint32_t& address, std::uint32_t& value,
                         const CheatFormat format) noexcept {
    const auto& seeds = seeds_for(format);
    std::uint32_t sum = kTeaDelta * kTeaRounds;
    for (std::uint32_t round = 0; round < kTeaRounds; ++round) {
        value -= ((address << 4U) + seeds[2]) ^ (address + sum) ^ ((address >> 5U) + seeds[3]);
        address -= ((value << 4U) + seeds[0]) ^ (value + sum) ^ ((value >> 5U) + seeds[1]);
        sum -= kTeaDelta;
    }
}

void encrypt_device_code(std::uint32_t& address, std::uint32_t& value,
                         const CheatFormat format) noexcept {
    const auto& seeds = seeds_for(format);
    std::uint32_t sum = 0;
    for (std::uint32_t round = 0; round < kTeaRounds; ++round) {
        sum += kTeaDelta;
        address += ((value << 4U) + seeds[0]) ^ (value + sum) ^ ((value >> 5U) + seeds[1]);
        value += ((address << 4U) + seeds[2]) ^ (address + sum) ^ ((address >> 5U) + seeds[3]);
    }
}

std::optional<CheatProgram> compile_cheat(const std::string_view code, const CheatFormat format,
                                          std::string& error) {
    const auto lines = tokenize(code, error);
    if (!lines) {
        return std::nullopt;
    }
    const auto shape = lines->front().shape;
    for (std::size_t index = 1; index < lines->size(); ++index) {
        if ((*lines)[index].shape != shape) {
            error = line_error(*lines, index,
                               "The lines mix different code formats; add each format as its own "
                               "cheat.");
            return std::nullopt;
        }
    }

    std::optional<CheatProgram> program;
    DecodeFailure failure;
    switch (shape) {
    case CodeLine::Shape::Raw:
        if (format != CheatFormat::Auto && format != CheatFormat::Raw) {
            error = "This is an address:value code; choose the Raw or Automatic format.";
            return std::nullopt;
        }
        program = decode_raw(*lines);
        break;
    case CodeLine::Shape::Short:
        if (format != CheatFormat::Auto && format != CheatFormat::CodeBreaker) {
            error = "Codes with 8 + 4 digits are CodeBreaker codes; choose the CodeBreaker or "
                    "Automatic format.";
            return std::nullopt;
        }
        program = decode_codebreaker(*lines, failure);
        break;
    case CodeLine::Shape::Long: {
        if (format == CheatFormat::Raw || format == CheatFormat::CodeBreaker) {
            error = "Codes with 8 + 8 digits are GameShark or Action Replay codes; choose one of "
                    "those formats or Automatic.";
            return std::nullopt;
        }
        if (format != CheatFormat::Auto) {
            int score = 0;
            program = format == CheatFormat::GameShark
                          ? decode_gameshark(*lines, failure, score)
                          : decode_action_replay_v3(*lines, failure, score);
            break;
        }
        // Automatic: the two device formats share a shape, so decode both and keep the one whose
        // decrypted content looks like real code (device IDs, hooks, work-RAM addresses).
        int gameshark_score = 0;
        int action_replay_score = 0;
        DecodeFailure gameshark_failure;
        DecodeFailure action_replay_failure;
        auto gameshark = decode_gameshark(*lines, gameshark_failure, gameshark_score);
        auto action_replay =
            decode_action_replay_v3(*lines, action_replay_failure, action_replay_score);
        if (gameshark && (!action_replay || gameshark_score >= action_replay_score)) {
            program = std::move(gameshark);
        } else if (action_replay) {
            program = std::move(action_replay);
        } else {
            failure = gameshark_failure.line >= action_replay_failure.line
                          ? std::move(gameshark_failure)
                          : std::move(action_replay_failure);
            failure.message += " (tried GameShark v1/v2 and Action Replay v3)";
        }
        break;
    }
    }

    if (!program) {
        error = line_error(*lines, failure.line, failure.message);
        return std::nullopt;
    }
    error.clear();
    return program;
}

void run_cheat_program(const CheatProgram& program, GbaBus& bus,
                       const std::uint16_t pressed_keys) noexcept {
    const auto& operations = program.operations;
    std::size_t index = 0;
    while (index < operations.size()) {
        const auto& operation = operations[index++];
        const auto skip = [&](const std::uint32_t count) {
            index += std::min<std::size_t>(count, operations.size() - index);
        };
        switch (operation.kind) {
        case Operation::Kind::Nop:
        case Operation::Kind::RomPatch:
            break;
        case Operation::Kind::Write: {
            auto address = operation.address;
            auto value = operation.value;
            for (std::uint32_t written = 0; written < operation.count; ++written) {
                bus.poke(address, value, operation.width);
                address += operation.address_step;
                value += operation.value_step;
            }
            break;
        }
        case Operation::Kind::WriteBytes:
            for (std::uint32_t offset = 0; offset < operation.count; ++offset) {
                bus.poke(operation.address + offset, program.data[operation.data_offset + offset],
                         1);
            }
            break;
        case Operation::Kind::Add:
            bus.poke(operation.address,
                     bus.peek(operation.address, operation.width) + operation.value,
                     operation.width);
            break;
        case Operation::Kind::Or:
            bus.poke(operation.address,
                     bus.peek(operation.address, operation.width) | operation.value,
                     operation.width);
            break;
        case Operation::Kind::And:
            bus.poke(operation.address,
                     bus.peek(operation.address, operation.width) & operation.value,
                     operation.width);
            break;
        case Operation::Kind::PointerWrite: {
            const auto pointer = bus.peek(operation.address, 4);
            if (in_work_ram(pointer)) {
                bus.poke(pointer + operation.address_step, operation.value, operation.width);
            }
            break;
        }
        case Operation::Kind::Condition:
            if (!condition_holds(operation, bus.peek(operation.address, operation.width))) {
                skip(operation.skip);
            }
            break;
        case Operation::Kind::KeyCondition:
            if ((pressed_keys & operation.value) != operation.value) {
                skip(operation.skip);
            }
            break;
        case Operation::Kind::Skip:
            skip(operation.skip);
            break;
        }
    }
}

bool CheatEngine::add(Cheat cheat, std::string& error) {
    auto program = compile_cheat(cheat.code, cheat.format, error);
    if (!program) {
        return false;
    }
    if (const auto lines = tokenize(cheat.code, error)) {
        cheat.code = normalized_code(*lines);
    }
    error.clear();
    entries_.push_back({std::move(cheat), std::move(program), {}});
    ++revision_;
    return true;
}

bool CheatEngine::replace(const std::size_t index, Cheat cheat, std::string& error) {
    if (index >= entries_.size()) {
        error = "That cheat no longer exists.";
        return false;
    }
    auto program = compile_cheat(cheat.code, cheat.format, error);
    if (!program) {
        return false;
    }
    if (const auto lines = tokenize(cheat.code, error)) {
        cheat.code = normalized_code(*lines);
    }
    error.clear();
    entries_[index] = {std::move(cheat), std::move(program), {}};
    ++revision_;
    return true;
}

void CheatEngine::remove(const std::size_t index) {
    if (index < entries_.size()) {
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
        ++revision_;
    }
}

void CheatEngine::set_enabled(const std::size_t index, const bool enabled) {
    if (index < entries_.size() && entries_[index].cheat.enabled != enabled) {
        entries_[index].cheat.enabled = enabled;
        ++revision_;
    }
}

void CheatEngine::clear() {
    if (!entries_.empty()) {
        entries_.clear();
        ++revision_;
    }
}

std::size_t CheatEngine::size() const noexcept {
    return entries_.size();
}

bool CheatEngine::empty() const noexcept {
    return entries_.empty();
}

const Cheat& CheatEngine::cheat(const std::size_t index) const {
    return entries_.at(index).cheat;
}

const std::string& CheatEngine::problem(const std::size_t index) const {
    return entries_.at(index).problem;
}

CheatFormat CheatEngine::decoded_format(const std::size_t index) const {
    const auto& entry = entries_.at(index);
    return entry.program ? entry.program->format : entry.cheat.format;
}

bool CheatEngine::any_enabled() const noexcept {
    return std::any_of(entries_.begin(), entries_.end(), [](const Entry& entry) {
        return entry.cheat.enabled && entry.program.has_value();
    });
}

std::uint64_t CheatEngine::revision() const noexcept {
    return revision_;
}

void CheatEngine::apply(GbaBus& bus, const std::uint16_t pressed_keys) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.cheat.enabled && entry.program) {
            run_cheat_program(*entry.program, bus, pressed_keys);
        }
    }
}

std::vector<RomPatch> CheatEngine::rom_patches() const {
    std::vector<RomPatch> patches;
    for (const auto& entry : entries_) {
        if (!entry.cheat.enabled || !entry.program) {
            continue;
        }
        for (const auto& operation : entry.program->operations) {
            if (operation.kind == Operation::Kind::RomPatch) {
                patches.push_back({operation.address, static_cast<std::uint16_t>(operation.value)});
            }
        }
    }
    return patches;
}

std::string CheatEngine::to_text() const {
    std::string text = "cheats = " + std::to_string(entries_.size()) + "\n";
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        const auto& cheat = entries_[index].cheat;
        const auto prefix = "cheat" + std::to_string(index) + "_";
        std::string code;
        for (const char character : cheat.code) {
            code.push_back(character == '\n' ? '+' : character);
        }
        text += "\n";
        text += prefix + "desc = " + quote_value(cheat.description) + "\n";
        text += prefix + "code = " + quote_value(code) + "\n";
        text += prefix + "enable = " + (cheat.enabled ? "true" : "false") + "\n";
        text += prefix + "format = " + quote_value(cheat_format_id(cheat.format)) + "\n";
    }
    return text;
}

bool CheatEngine::load_text(const std::string_view text, std::string& error) {
    std::map<std::size_t, FileCheat> found;
    bool recognized = false;
    std::size_t position = 0;
    if (text.substr(0, 3) == "\xEF\xBB\xBF") { // UTF-8 byte order mark
        position = 3;
    }
    while (position < text.size()) {
        auto end = text.find('\n', position);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const auto line = trim(text.substr(position, end - position));
        position = end + 1U;

        const auto equals = line.find('=');
        if (line.empty() || line.front() == '#' || equals == std::string_view::npos) {
            continue;
        }
        const auto key = trim(line.substr(0, equals));
        auto value = trim(line.substr(equals + 1U));
        if (value.size() >= 2U && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2U);
        }
        if (key == "cheats") {
            recognized = true;
            continue;
        }
        if (key.rfind("cheat", 0) != 0) {
            continue;
        }
        const auto underscore = key.find('_');
        const auto number = key.substr(
            5, underscore == std::string_view::npos ? std::string_view::npos : underscore - 5U);
        if (underscore == std::string_view::npos || number.empty() ||
            !std::all_of(
                number.begin(), number.end(),
                [](const char character) { return character >= '0' && character <= '9'; }) ||
            number.size() > 6U) {
            continue;
        }
        const auto cheat_index = static_cast<std::size_t>(std::stoul(std::string(number)));
        if (cheat_index >= kMaximumCheatsInFile) {
            continue;
        }
        recognized = true;
        auto& cheat = found[cheat_index];
        const auto field = key.substr(underscore + 1U);
        if (field == "desc") {
            cheat.description = std::string(value);
        } else if (field == "code") {
            cheat.code = std::string(value);
            cheat.has_code = true;
        } else if (field == "enable") {
            cheat.enabled = value == "true" || value == "1";
        } else if (field == "format") {
            cheat.format = std::string(value);
        }
    }

    if (!recognized && !trim(text).empty()) {
        error = "This is not a cheat file.";
        return false;
    }

    std::vector<Entry> entries;
    std::size_t problems = 0;
    for (auto& [index, file_cheat] : found) {
        static_cast<void>(index);
        if (!file_cheat.has_code) {
            continue;
        }
        Entry entry;
        entry.cheat.description = std::move(file_cheat.description);
        entry.cheat.enabled = file_cheat.enabled;
        entry.cheat.format = cheat_format_from_id(file_cheat.format).value_or(CheatFormat::Auto);
        std::string code;
        for (const char character : file_cheat.code) {
            code.push_back(character == '+' ? '\n' : character);
        }
        std::string problem;
        entry.program = compile_cheat(code, entry.cheat.format, problem);
        if (entry.program) {
            std::string ignored;
            if (const auto lines = tokenize(code, ignored)) {
                code = normalized_code(*lines);
            }
        } else {
            entry.problem = std::move(problem);
            entry.cheat.enabled = false;
            ++problems;
        }
        entry.cheat.code = std::move(code);
        entries.push_back(std::move(entry));
    }

    entries_ = std::move(entries);
    ++revision_;
    error = problems == 0U
                ? std::string{}
                : std::to_string(problems) + (problems == 1U ? " cheat could not be decoded."
                                                             : " cheats could not be decoded.");
    return true;
}

} // namespace srgba::core
