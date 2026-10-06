#include "srgba/core/rewind.hpp"

#include <cstring>

namespace srgba::core {
namespace {

void write_varint(std::vector<std::uint8_t>& output, std::size_t value) {
    while (value >= 0x80U) {
        output.push_back(static_cast<std::uint8_t>(value | 0x80U));
        value >>= 7U;
    }
    output.push_back(static_cast<std::uint8_t>(value));
}

[[nodiscard]] bool read_varint(const std::span<const std::uint8_t> input, std::size_t& position,
                               std::size_t& value) {
    value = 0;
    for (unsigned shift = 0; shift < 64U; shift += 7U) {
        if (position >= input.size()) {
            return false;
        }
        const auto byte = input[position++];
        value |= static_cast<std::size_t>(byte & 0x7FU) << shift;
        if ((byte & 0x80U) == 0U) {
            return true;
        }
    }
    return false;
}

} // namespace

RewindBuffer::RewindBuffer(const std::size_t memory_limit) noexcept : memory_limit_(memory_limit) {}

void RewindBuffer::set_memory_limit(const std::size_t bytes) noexcept {
    memory_limit_ = bytes;
    evict();
}

void RewindBuffer::clear() noexcept {
    newest_.reset();
    differences_.clear();
    difference_bytes_ = 0;
}

// Format: repeated [run of unchanged bytes][count of changed bytes][changed bytes XOR old].
std::vector<std::uint8_t> RewindBuffer::encode_difference(const std::span<const std::uint8_t> from,
                                                          const std::span<const std::uint8_t> to) {
    std::vector<std::uint8_t> output;
    std::size_t position = 0;
    while (position < to.size()) {
        const auto unchanged_start = position;
        // Skip identical memory eight bytes at a time; most of a snapshot is unchanged.
        while (position + 8U <= to.size() &&
               std::memcmp(from.data() + position, to.data() + position, 8U) == 0) {
            position += 8U;
        }
        while (position < to.size() && from[position] == to[position]) {
            ++position;
        }
        const auto changed_start = position;
        // End a changed run only at a gap of several equal bytes, so short gaps stay literal.
        std::size_t equal_run = 0;
        while (position < to.size() && equal_run < 8U) {
            equal_run = from[position] == to[position] ? equal_run + 1U : 0U;
            ++position;
        }
        const auto changed_end = position - equal_run;
        position = changed_end;
        write_varint(output, changed_start - unchanged_start);
        write_varint(output, changed_end - changed_start);
        for (auto index = changed_start; index < changed_end; ++index) {
            output.push_back(static_cast<std::uint8_t>(from[index] ^ to[index]));
        }
    }
    return output;
}

bool RewindBuffer::apply_difference(std::vector<std::uint8_t>& target,
                                    const std::span<const std::uint8_t> difference) {
    std::size_t input = 0;
    std::size_t output = 0;
    while (input < difference.size()) {
        std::size_t unchanged = 0;
        std::size_t changed = 0;
        if (!read_varint(difference, input, unchanged) ||
            !read_varint(difference, input, changed)) {
            return false;
        }
        output += unchanged;
        if (output + changed > target.size() || input + changed > difference.size()) {
            return false;
        }
        for (std::size_t index = 0; index < changed; ++index) {
            target[output++] ^= difference[input++];
        }
    }
    return true;
}

void RewindBuffer::push(std::vector<std::uint8_t> snapshot) {
    if (newest_ && newest_->size() == snapshot.size()) {
        auto difference = encode_difference(snapshot, *newest_);
        difference_bytes_ += difference.size();
        differences_.push_back(std::move(difference));
    } else {
        // A size change (for example an EEPROM resizing itself) breaks the chain.
        differences_.clear();
        difference_bytes_ = 0;
    }
    newest_ = std::move(snapshot);
    evict();
}

std::optional<std::vector<std::uint8_t>> RewindBuffer::pop() {
    if (!newest_) {
        return std::nullopt;
    }
    auto result = *newest_;
    if (differences_.empty()) {
        newest_.reset();
        return result;
    }
    const auto& difference = differences_.back();
    if (!apply_difference(*newest_, difference)) {
        clear();
        return result;
    }
    difference_bytes_ -= difference.size();
    differences_.pop_back();
    return result;
}

std::size_t RewindBuffer::size() const noexcept {
    return newest_ ? differences_.size() + 1U : 0U;
}

bool RewindBuffer::empty() const noexcept {
    return !newest_;
}

std::size_t RewindBuffer::memory_used() const noexcept {
    return difference_bytes_ + (newest_ ? newest_->size() : 0U);
}

void RewindBuffer::evict() noexcept {
    while (!differences_.empty() && memory_used() > memory_limit_) {
        difference_bytes_ -= differences_.front().size();
        differences_.pop_front();
    }
}

} // namespace srgba::core
