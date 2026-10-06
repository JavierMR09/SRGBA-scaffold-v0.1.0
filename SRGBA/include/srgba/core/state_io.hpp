#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace srgba::core {

// Little-endian, field-by-field serialization used by save states. Components write their logical
// state explicitly; raw object memory is never written, so the format does not depend on the
// compiler's struct layout.
class StateWriter {
  public:
    StateWriter() = default;
    explicit StateWriter(const std::size_t expected_size) {
        bytes_.reserve(expected_size);
    }

    void u8(const std::uint8_t value) {
        bytes_.push_back(value);
    }
    void boolean(const bool value) {
        u8(value ? 1U : 0U);
    }
    void u16(const std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8U));
    }
    void u32(const std::uint32_t value) {
        u16(static_cast<std::uint16_t>(value));
        u16(static_cast<std::uint16_t>(value >> 16U));
    }
    void u64(const std::uint64_t value) {
        u32(static_cast<std::uint32_t>(value));
        u32(static_cast<std::uint32_t>(value >> 32U));
    }
    void i32(const std::int32_t value) {
        u32(static_cast<std::uint32_t>(value));
    }
    void i64(const std::int64_t value) {
        u64(static_cast<std::uint64_t>(value));
    }
    void bytes(const std::span<const std::uint8_t> data) {
        bytes_.insert(bytes_.end(), data.begin(), data.end());
    }
    // A four-character marker that lets the reader detect a misaligned or foreign stream.
    void section(const char (&tag)[5]) {
        bytes(std::span(reinterpret_cast<const std::uint8_t*>(tag), 4));
    }

    [[nodiscard]] const std::vector<std::uint8_t>& data() const noexcept {
        return bytes_;
    }
    [[nodiscard]] std::vector<std::uint8_t> take() noexcept {
        return std::move(bytes_);
    }

  private:
    std::vector<std::uint8_t> bytes_;
};

class StateReader {
  public:
    explicit StateReader(const std::span<const std::uint8_t> data) noexcept : data_(data) {}

    [[nodiscard]] std::uint8_t u8() noexcept {
        if (position_ >= data_.size()) {
            ok_ = false;
            return 0;
        }
        return data_[position_++];
    }
    [[nodiscard]] bool boolean() noexcept {
        return u8() != 0U;
    }
    [[nodiscard]] std::uint16_t u16() noexcept {
        const auto low = u8();
        return static_cast<std::uint16_t>(low | (u8() << 8U));
    }
    [[nodiscard]] std::uint32_t u32() noexcept {
        const auto low = u16();
        return static_cast<std::uint32_t>(low) | (static_cast<std::uint32_t>(u16()) << 16U);
    }
    [[nodiscard]] std::uint64_t u64() noexcept {
        const auto low = u32();
        return static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(u32()) << 32U);
    }
    [[nodiscard]] std::int32_t i32() noexcept {
        return static_cast<std::int32_t>(u32());
    }
    [[nodiscard]] std::int64_t i64() noexcept {
        return static_cast<std::int64_t>(u64());
    }
    void bytes(const std::span<std::uint8_t> destination) noexcept {
        if (destination.empty()) {
            return;
        }
        if (data_.size() - position_ < destination.size() || position_ > data_.size()) {
            ok_ = false;
            return;
        }
        std::memcpy(destination.data(), data_.data() + position_, destination.size());
        position_ += destination.size();
    }
    // Returns false (and marks the stream bad) when the next four bytes are not `tag`.
    bool section(const char (&tag)[5]) noexcept {
        std::array<std::uint8_t, 4> found{};
        bytes(found);
        if (!ok_ || std::memcmp(found.data(), tag, 4) != 0) {
            ok_ = false;
        }
        return ok_;
    }
    void fail() noexcept {
        ok_ = false;
    }

    [[nodiscard]] bool ok() const noexcept {
        return ok_;
    }
    [[nodiscard]] bool at_end() const noexcept {
        return position_ == data_.size();
    }
    [[nodiscard]] std::size_t remaining() const noexcept {
        return data_.size() - position_;
    }

  private:
    std::span<const std::uint8_t> data_;
    std::size_t position_{};
    bool ok_{true};
};

} // namespace srgba::core
