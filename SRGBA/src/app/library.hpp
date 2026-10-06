#pragma once

#include "srgba/core/rom_library.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace srgba::app {

// Scans the user's ROM folders on a background thread so large collections never stall the UI.
// Files that have not changed since the previous scan are not read again.
class RomLibrary {
  public:
    RomLibrary() = default;
    ~RomLibrary();

    RomLibrary(const RomLibrary&) = delete;
    RomLibrary& operator=(const RomLibrary&) = delete;

    // Starts scanning `folders`, replacing any scan in progress.
    void scan(std::vector<std::filesystem::path> folders, bool recursive);
    void stop() noexcept;

    [[nodiscard]] bool scanning() const noexcept;
    [[nodiscard]] std::size_t files_examined() const noexcept;
    [[nodiscard]] std::size_t files_found() const noexcept;
    // Increments whenever entries() changes.
    [[nodiscard]] std::uint64_t revision() const noexcept;
    // ROMs found so far, sorted by title.
    [[nodiscard]] std::vector<core::RomInfo> entries() const;

  private:
    struct CachedInfo {
        std::filesystem::file_time_type modified;
        std::uintmax_t size{};
        core::RomInfo info;
    };

    void run(const std::stop_token& stop, const std::vector<std::filesystem::path>& folders,
             bool recursive);
    void publish(std::vector<core::RomInfo> entries);

    std::jthread worker_;
    mutable std::mutex mutex_;
    std::vector<core::RomInfo> entries_;
    std::map<std::filesystem::path, CachedInfo> cache_; // used only by the worker
    std::atomic<bool> scanning_{false};
    std::atomic<std::size_t> examined_{0};
    std::atomic<std::size_t> found_{0};
    std::atomic<std::uint64_t> revision_{0};
};

} // namespace srgba::app
