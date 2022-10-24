#ifndef SJEF_LIB_UTIL_LOGGER_H_
#define SJEF_LIB_UTIL_LOGGER_H_
#include <iostream>
#include <vector>
#include <string>

namespace sjef::util {

class Logger {
  std::ostream* m_stream;
  int m_level;

  class NullBuffer : public std::streambuf {
  public:
    int overflow(int c) override { return c; }
  };
  NullBuffer m_null_buffer;
  mutable std::ostream m_null{&m_null_buffer};
  std::vector<std::string> m_preambles;

public:
  enum class Levels : int { quiet = -1, error = 0, warning = 1, notification = 2, detail = 3 };
  explicit Logger(std::ostream& stream, int level) : m_stream(&stream), m_level(level) {}
  explicit Logger(std::ostream& stream = std::cout, const Levels level = Levels::error,
                  std::vector<std::string> preambles = {})
      : m_stream(&stream), m_level(static_cast<int>(level)), m_preambles(std::move(preambles)) {}
  explicit Logger(const Logger& source)
      : m_stream(source.m_stream), m_level(source.m_level), m_preambles(source.m_preambles) {}
  explicit Logger(Logger&& source) noexcept
      : m_stream(std::move(source.m_stream)), m_level(std::move(source.m_level)),
        m_preambles(std::move(source.m_preambles)) {}
  Logger& operator=(const Logger& source) {
    m_stream = source.m_stream;
    m_level = source.m_level;
    m_preambles = source.m_preambles;
    return *this;
  }
  Logger& operator=(Logger&& source) noexcept {
    m_stream = source.m_stream;
    m_level = source.m_level;
    m_preambles = std::move(source.m_preambles);
    return *this;
  }
  ~Logger() = default;
  std::ostream& stream() const { return *m_stream; }
  std::ostream& operator()(int level, const std::string& message = "") const {
    auto& stream = level > m_level ? m_null : *m_stream;
    if (level >= 0 && level < decltype(level)(m_preambles.size()))
      stream << m_preambles[level];
    if (!message.empty())
      stream << message << std::endl;
    return stream;
  }
  std::ostream& operator()(const Levels level, const std::string& message = "") const {
    return ((*this)(static_cast<int>(level), message));
  }
  std::ostream& detail(const std::string& message = "") const { return ((*this)(Levels::detail, message)); }
  std::ostream& notify(const std::string& message = "") const { return ((*this)(Levels::notification, message)); }
  std::ostream& warn(const std::string& message = "") const { return ((*this)(Levels::warning, message)); }
  std::ostream& error(const std::string& message = "") const { return ((*this)(Levels::error, message)); }
  int level() const { return m_level; }
  void set_level(int level) { Logger::m_level = level; }
  void set_level(Levels level) { Logger::m_level = static_cast<int>(level); }
  void set_stream(std::ostream& stream) { m_stream = &stream; }
};

template <typename Arg>
std::ostream& operator<<(const Logger& l, Arg arg) {
  l.stream() << arg;
  return l.stream();
}

} // namespace sjef::util

#endif // SJEF_LIB_UTIL_LOGGER_H_
