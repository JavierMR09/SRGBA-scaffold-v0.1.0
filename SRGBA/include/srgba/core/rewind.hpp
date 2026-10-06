#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace srgba::core {

// A bounded history of machine snapshots for rewinding. Only the newest snapshot is stored in
// full; each older one is kept as the XOR difference from its successor, run-length compressed.
// Consecutive snapshots differ in a small part of memory, so a difference typically takes a few
// kilobytes instead of the ~400 KiB of a full snapshot.
class RewindBuffer {
  public:
    explicit RewindBuffer(std::size_t memory_limit = 64U * 1024U * 1024U) noexcept;

    void set_memory_limit(std::size_t bytes) noexcept;
    void clear() noexcept;

    void push(std::vector<std::uint8_t> snapshot);
    // Removes and returns the newest snapshot; the one before it becomes the newest.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> pop();

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t memory_used() const noexcept;

    // Exposed for tests: compresses the XOR of two equally sized buffers and applies it back.
    [[nodiscard]] static std::vector<std::uint8_t>
    encode_difference(std::span<const std::uint8_t> from, std::span<const std::uint8_t> to);
    [[nodiscard]] static bool apply_difference(std::vector<std::uint8_t>& target,
                                               std::span<const std::uint8_t> difference);

  private:
    void evict() noexcept;

    std::size_t memory_limit_;
    std::optional<std::vector<std::uint8_t>> newest_;
    std::deque<std::vector<std::uint8_t>> differences_; // back() leads from newest_ to the previous
    std::size_t difference_bytes_{};
};

} // namespace srgba::core
