#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace srgba::core {

// Reads a whole file, or returns nullopt when it does not exist or cannot be read.
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
read_binary_file(const std::filesystem::path& path, std::size_t size_limit);

// Writes `bytes` to a temporary file beside `path`, then renames it over `path`, so an
// interrupted write never leaves a truncated save behind.
[[nodiscard]] bool write_file_atomically(const std::filesystem::path& path,
                                         std::span<const std::uint8_t> bytes,
                                         std::string& error_message) noexcept;

} // namespace srgba::core
