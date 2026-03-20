#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace test_support
{
inline std::uint64_t nextTempCounter()
{
  static std::uint64_t counter = 0;
  return ++counter;
}

class ScopedTempFile
{
public:
  ScopedTempFile(std::string_view prefix, std::string_view extension)
  {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
      (std::string(prefix) + "_" + std::to_string(now) + "_" + std::to_string(nextTempCounter()) + std::string(extension));
  }

  ScopedTempFile(std::string_view prefix, std::string_view extension, std::string_view contents)
    : ScopedTempFile(prefix, extension)
  {
    write(contents);
  }

  ScopedTempFile(const ScopedTempFile &) = delete;
  ScopedTempFile &operator=(const ScopedTempFile &) = delete;

  ScopedTempFile(ScopedTempFile &&) = delete;
  ScopedTempFile &operator=(ScopedTempFile &&) = delete;

  ~ScopedTempFile()
  {
    std::error_code errorCode;
    std::filesystem::remove(path_, errorCode);
  }

  [[nodiscard]] const std::filesystem::path &path() const
  {
    return path_;
  }

  [[nodiscard]] std::string string() const
  {
    return path_.string();
  }

  void write(std::string_view contents) const
  {
    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    output << contents;
  }

private:
  std::filesystem::path path_;
};
} // namespace test_support
