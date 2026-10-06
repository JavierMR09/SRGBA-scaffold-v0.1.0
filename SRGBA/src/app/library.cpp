#include "app/library.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <set>
#include <string>
#include <system_error>
#include <utility>

namespace srgba::app {
namespace {

[[nodiscard]] std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](const char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return text;
}

void sort_by_title(std::vector<core::RomInfo>& entries) {
    std::sort(
        entries.begin(), entries.end(), [](const core::RomInfo& left, const core::RomInfo& right) {
            const auto left_title = lowercase(left.title);
            const auto right_title = lowercase(right.title);
            return left_title != right_title ? left_title < right_title : left.path < right.path;
        });
}

} // namespace

RomLibrary::~RomLibrary() {
    stop();
}

void RomLibrary::scan(std::vector<std::filesystem::path> folders, const bool recursive) {
    stop();
    scanning_ = true;
    examined_ = 0;
    found_ = 0;
    worker_ = std::jthread([this, folders = std::move(folders), recursive](
                               const std::stop_token& token) { run(token, folders, recursive); });
}

void RomLibrary::stop() noexcept {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    scanning_ = false;
}

bool RomLibrary::scanning() const noexcept {
    return scanning_;
}

std::size_t RomLibrary::files_examined() const noexcept {
    return examined_;
}

std::size_t RomLibrary::files_found() const noexcept {
    return found_;
}

std::uint64_t RomLibrary::revision() const noexcept {
    return revision_;
}

std::vector<core::RomInfo> RomLibrary::entries() const {
    const std::scoped_lock lock(mutex_);
    return entries_;
}

void RomLibrary::publish(std::vector<core::RomInfo> entries) {
    sort_by_title(entries);
    {
        const std::scoped_lock lock(mutex_);
        entries_ = std::move(entries);
    }
    ++revision_;
}

void RomLibrary::run(const std::stop_token& stop, const std::vector<std::filesystem::path>& folders,
                     const bool recursive) {
    std::set<std::filesystem::path> unique_files;
    for (const auto& folder : folders) {
        for (auto& file : core::find_rom_files(folder, recursive)) {
            unique_files.insert(std::move(file));
        }
        if (stop.stop_requested()) {
            scanning_ = false;
            return;
        }
    }
    found_ = unique_files.size();

    std::vector<core::RomInfo> entries;
    entries.reserve(unique_files.size());
    auto last_publish = std::chrono::steady_clock::now();
    for (const auto& file : unique_files) {
        if (stop.stop_requested()) {
            scanning_ = false;
            return;
        }
        std::error_code error;
        const auto modified = std::filesystem::last_write_time(file, error);
        const auto size = error ? std::uintmax_t{0} : std::filesystem::file_size(file, error);
        if (const auto cached = cache_.find(file); !error && cached != cache_.end() &&
                                                   cached->second.modified == modified &&
                                                   cached->second.size == size) {
            entries.push_back(cached->second.info);
        } else {
            std::string read_error;
            if (auto info = core::read_rom_info(file, read_error)) {
                if (!error) {
                    cache_[file] = {modified, size, *info};
                }
                entries.push_back(std::move(*info));
            }
        }
        ++examined_;

        // Show progress on big collections without re-sorting after every file.
        const auto now = std::chrono::steady_clock::now();
        if (now - last_publish > std::chrono::milliseconds(300)) {
            publish(entries);
            last_publish = now;
        }
    }
    publish(std::move(entries));
    scanning_ = false;
}

} // namespace srgba::app
