#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace srgba::core {

class GbaBus;

enum class CheatFormat : std::uint8_t {
    Auto,           // chosen from the code's shape and, for 16-digit codes, its decrypted content
    Raw,            // "AAAAAAAA:VV", "AAAAAAAA:VVVV" or "AAAAAAAA:VVVVVVVV" direct writes
    CodeBreaker,    // unencrypted "AAAAAAAA VVVV"
    GameShark,      // GameShark / Action Replay v1 and v2, encrypted "AAAAAAAA VVVVVVVV"
    ActionReplayV3, // Action Replay v3 / GameShark SP, encrypted "AAAAAAAA VVVVVVVV"
};

inline constexpr CheatFormat kCheatFormats[] = {
    CheatFormat::Auto,           CheatFormat::Raw, CheatFormat::CodeBreaker, CheatFormat::GameShark,
    CheatFormat::ActionReplayV3,
};

// Human-readable name ("GameShark v1/v2") and the stable identifier used in cheat files.
[[nodiscard]] std::string_view cheat_format_name(CheatFormat format) noexcept;
[[nodiscard]] std::string_view cheat_format_id(CheatFormat format) noexcept;
[[nodiscard]] std::optional<CheatFormat> cheat_format_from_id(std::string_view id) noexcept;

// The GameShark and Action Replay devices encrypt codes with a 32-round TEA variant keyed by
// fixed device seeds (v1/v2 and v3 use different seeds). `format` must be GameShark or
// ActionReplayV3.
void decrypt_device_code(std::uint32_t& address, std::uint32_t& value, CheatFormat format) noexcept;
void encrypt_device_code(std::uint32_t& address, std::uint32_t& value, CheatFormat format) noexcept;

struct Cheat {
    std::string description;
    // The code as entered: one code per line (spaces, '+' and '-' between parts are accepted).
    std::string code;
    CheatFormat format{CheatFormat::Auto};
    bool enabled{true};
};

// A 16-bit ROM override requested by a GameShark or Action Replay "ROM patch" code.
struct RomPatch {
    std::uint32_t address{};
    std::uint16_t value{};
};

// One decoded instruction of a cheat program. Each code (which may span several lines) becomes
// one operation, so "execute the next code" conditions skip exactly one operation.
struct CheatOperation {
    enum class Kind : std::uint8_t {
        Nop,          // game ID, master-code hook, and other device bookkeeping
        Write,        // store `value` `count` times, stepping by `address_step`/`value_step`
        WriteBytes,   // store `count` bytes from the program's data, starting at `data_offset`
        Add,          // read-modify-write
        Or,           //
        And,          //
        PointerWrite, // if the word at `address` points into work RAM, store `value` at
                      // pointer + `address_step`
        RomPatch,     // replace the ROM halfword at `address` while the cheat is enabled
        Condition,    // when the comparison fails, skip the next `skip` operations
        KeyCondition, // when the keys in `value` are not all held, skip the next `skip`
        Skip,         // unconditionally skip the next `skip` operations
    };
    enum class Compare : std::uint8_t {
        Equal,
        NotEqual,
        Less,
        Greater,
        LessOrEqual,
        GreaterOrEqual,
        LessSigned,
        GreaterSigned,
        AndNonZero,
    };
    static constexpr std::uint32_t kSkipRest = 0xFFFFFFFFU;

    Kind kind{Kind::Nop};
    Compare compare{Compare::Equal};
    std::uint8_t width{1}; // 1, 2 or 4 bytes
    std::uint32_t address{};
    std::uint32_t value{};
    std::uint32_t count{1};
    std::uint32_t address_step{};
    std::uint32_t value_step{};
    std::uint32_t skip{};
    std::uint32_t data_offset{};
};

struct CheatProgram {
    CheatFormat format{CheatFormat::Auto}; // the format the code was decoded as
    std::vector<CheatOperation> operations;
    std::vector<std::uint8_t> data;
    std::string game_code; // from a device ID line ("AXVE"), when present
};

// Decodes `code`. Returns nullopt and sets `error` (naming the offending line) when the code is
// malformed or uses a feature SRGBA does not support.
[[nodiscard]] std::optional<CheatProgram> compile_cheat(std::string_view code, CheatFormat format,
                                                        std::string& error);

// Runs a program once against memory. `pressed_keys` is the active-high srgba::core::Key mask.
void run_cheat_program(const CheatProgram& program, GbaBus& bus,
                       std::uint16_t pressed_keys) noexcept;

// The cheat list for one game. Programs run once per frame, in list order.
class CheatEngine {
  public:
    // Adds a cheat after decoding its code. On failure the list is unchanged.
    bool add(Cheat cheat, std::string& error);
    bool replace(std::size_t index, Cheat cheat, std::string& error);
    void remove(std::size_t index);
    void set_enabled(std::size_t index, bool enabled);
    void clear();

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const Cheat& cheat(std::size_t index) const;
    // Why a cheat loaded from a file cannot run (empty when it decodes).
    [[nodiscard]] const std::string& problem(std::size_t index) const;
    // The format the cheat was decoded as (resolves Auto).
    [[nodiscard]] CheatFormat decoded_format(std::size_t index) const;
    [[nodiscard]] bool any_enabled() const noexcept;
    // Increments whenever the list or an enabled flag changes.
    [[nodiscard]] std::uint64_t revision() const noexcept;

    void apply(GbaBus& bus, std::uint16_t pressed_keys) const noexcept;
    [[nodiscard]] std::vector<RomPatch> rom_patches() const;

    // Cheat files use the libretro .cht layout (`cheats = N`, `cheatK_desc`, `cheatK_code`,
    // `cheatK_enable`) plus a `cheatK_format` key; code lines are joined with '+'.
    [[nodiscard]] std::string to_text() const;
    // Replaces the list. Cheats whose codes do not decode are kept (disabled, with a problem
    // description) so saving the file does not lose them. Returns false when the text is not a
    // cheat file at all.
    bool load_text(std::string_view text, std::string& error);

  private:
    struct Entry {
        Cheat cheat;
        std::optional<CheatProgram> program;
        std::string problem;
    };

    std::vector<Entry> entries_;
    std::uint64_t revision_{};
};

} // namespace srgba::core
