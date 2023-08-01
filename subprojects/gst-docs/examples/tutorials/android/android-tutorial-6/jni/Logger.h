//
// Created by dayong.li on 2023/7/27.
//

#ifndef ANDROID_LOGGER_H
#define ANDROID_LOGGER_H
#include <string>

namespace Log {

  enum class Level {
    Verbose, Info, Warning, Error
  };

  void SetLevel(Level minSeverity);
  void Write(Level severity, const std::string &msg);

}  // namespace Log

inline std::string Fmt(const char *fmt, ...) {
  va_list vl;
  va_start(vl, fmt);
  int size = std::vsnprintf(nullptr, 0, fmt, vl);
  va_end(vl);

  if (size != -1) {
    std::unique_ptr<char[]> buffer(new char[size + 1]);

    va_start(vl, fmt);
    size = std::vsnprintf(buffer.get(), size + 1, fmt, vl);
    va_end(vl);
    if (size != -1) {
      return std::string(buffer.get(), size);
    }
  }
  return "";
}
#endif //ANDROID_LOGGER_H
