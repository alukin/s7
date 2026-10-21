#include "s7/platform.hpp"

#include <cstdlib>
#include <system_error>
#include <utility>
#include <vector>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace s7::platform {

auto executable_directory() -> std::optional<std::filesystem::path>
{
#if defined(_WIN32)
    std::vector<wchar_t> buffer(256);
    for (;;) {
        const DWORD size = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!size) return std::nullopt;
        if (size < buffer.size()) return std::filesystem::path(std::wstring(buffer.data(), size)).parent_path();
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__linux__)
    std::error_code ec;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) return path.parent_path();
    return std::nullopt;
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (::_NSGetExecutablePath(buffer.data(), &size) != 0) return std::nullopt;
    std::error_code ec;
    const auto path = std::filesystem::canonical(buffer.data(), ec);
    if (!ec) return path.parent_path();
    return std::nullopt;
#else
    return std::nullopt;
#endif
}

#if defined(_WIN32)

namespace {

[[noreturn]] void throw_last_error(const std::string& what)
{
    throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), what);
}

} // namespace

void write_file_atomically(const std::filesystem::path& path, const std::string_view contents)
{
    const auto temporary = std::filesystem::path(path).concat(".tmp");
    const HANDLE file = ::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw_last_error("create " + temporary.string());
    }
    DWORD written = 0;
    const bool ok = ::WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) &&
                    written == contents.size() && ::FlushFileBuffers(file);
    const DWORD error = ::GetLastError();
    ::CloseHandle(file);
    if (!ok) {
        throw std::system_error(static_cast<int>(error), std::system_category(), "write " + temporary.string());
    }
    if (!::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw_last_error("rename " + temporary.string());
    }
}

auto FileLock::try_lock(const std::filesystem::path& path) -> std::optional<FileLock>
{
    // Opened without sharing: a second opener fails until this handle closes.
    const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (::GetLastError() == ERROR_SHARING_VIOLATION) {
            return std::nullopt;
        }
        throw_last_error("open " + path.string());
    }
    return FileLock(reinterpret_cast<std::intptr_t>(file));
}

FileLock::~FileLock()
{
    if (handle_ != -1) {
        ::CloseHandle(reinterpret_cast<HANDLE>(handle_));
    }
}

auto home_directory() -> std::optional<std::filesystem::path>
{
    for (const wchar_t* name : {L"USERPROFILE", L"HOME"}) {
        if (const wchar_t* value = ::_wgetenv(name); value && *value) {
            return std::filesystem::path(value);
        }
    }
    return std::nullopt;
}

void* open_library(const std::filesystem::path& path, std::string& error)
{
    // The plugin's own directory is searched for the DLLs it depends on.
    const HMODULE library = ::LoadLibraryExW(std::filesystem::absolute(path).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!library) {
        error = std::system_category().message(static_cast<int>(::GetLastError()));
    }
    return reinterpret_cast<void*>(library);
}

void* library_symbol(void* library, const char* name)
{
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(library), name));
}

void close_library(void* library)
{
    ::FreeLibrary(static_cast<HMODULE>(library));
}

#else

void write_file_atomically(const std::filesystem::path& path, const std::string_view contents)
{
    const auto temporary = std::filesystem::path(path).concat(".tmp");
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "open " + temporary.string());
    }
    const bool ok = ::write(fd, contents.data(), contents.size()) == static_cast<ssize_t>(contents.size()) && ::fsync(fd) == 0;
    const int error = errno;
    ::close(fd);
    if (!ok) {
        throw std::system_error(error, std::generic_category(), "write " + temporary.string());
    }
    std::filesystem::rename(temporary, path);
    auto directory = path.parent_path();
    if (directory.empty()) {
        directory = ".";
    }
    if (const int dir = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); dir >= 0) {
        ::fsync(dir);
        ::close(dir);
    }
}

auto FileLock::try_lock(const std::filesystem::path& path) -> std::optional<FileLock>
{
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "open " + path.string());
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return std::nullopt;
    }
    return FileLock(fd);
}

FileLock::~FileLock()
{
    if (handle_ != -1) {
        ::close(static_cast<int>(handle_));
    }
}

auto home_directory() -> std::optional<std::filesystem::path>
{
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home);
    }
    return std::nullopt;
}

void* open_library(const std::filesystem::path& path, std::string& error)
{
    void* library = ::dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!library) {
        const char* message = ::dlerror();
        error = message ? message : "unknown error";
    }
    return library;
}

void* library_symbol(void* library, const char* name)
{
    return ::dlsym(library, name);
}

void close_library(void* library)
{
    ::dlclose(library);
}

#endif

FileLock::FileLock(FileLock&& other) noexcept
    : handle_(std::exchange(other.handle_, -1))
{
}

FileLock& FileLock::operator=(FileLock&& other) noexcept
{
    if (this != &other) {
        FileLock released(std::move(*this));
        handle_ = std::exchange(other.handle_, -1);
    }
    return *this;
}

} // namespace s7::platform
