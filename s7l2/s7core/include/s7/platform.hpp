// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_PLATFORM_HPP
#define S7CORE_INCLUDE_S7_PLATFORM_HPP

// Operating-system services, one interface over POSIX and Windows.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace s7::platform {

// Replaces the file at `path` with `contents` so that a crash leaves either
// the old or the new content: writes a temporary file next to it, flushes it
// to disk and renames it over `path` (then syncs the directory where the
// system supports that). New files are readable by the owner only on POSIX.
// Throws std::system_error.
void write_file_atomically(const std::filesystem::path& path, std::string_view contents);

// An exclusive lock on a file, held by this process until destroyed.
class FileLock {
public:
    // Creates the file if needed. Empty when another process holds the lock;
    // throws std::system_error when the file cannot be opened.
    [[nodiscard]] static std::optional<FileLock> try_lock(const std::filesystem::path& path);

    FileLock(FileLock&& other) noexcept;
    FileLock& operator=(FileLock&& other) noexcept;
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    ~FileLock();

private:
    explicit FileLock(std::intptr_t handle) noexcept : handle_(handle) {}
    std::intptr_t handle_; // file descriptor, or Windows HANDLE
};

// The user's home directory: $HOME, or %USERPROFILE% on Windows.
[[nodiscard]] std::optional<std::filesystem::path> home_directory();

// Directory of the running executable, independent of cwd and argv[0].
[[nodiscard]] std::optional<std::filesystem::path> executable_directory();

// Dynamic libraries (plugins). A null handle means failure; `error` says why.
[[nodiscard]] void* open_library(const std::filesystem::path& path, std::string& error);
[[nodiscard]] void* library_symbol(void* library, const char* name);
void close_library(void* library);

} // namespace s7::platform

#endif // S7CORE_INCLUDE_S7_PLATFORM_HPP
