#ifndef SJEF_LIB_UTIL_LOGGER_H_
#define SJEF_LIB_UTIL_LOGGER_H_
#include <iostream>
#include <mutex>
#include <set>
#include <vector>
#include <string>

// For LockedStream::operator<<(Arg&&) below: template argument deduction never considers
// implicit conversions, so a template operator<<(basic_ostream<CharT,Traits>&, ...) overload --
// which covers const char*, and sjef::util's own operator<<(std::ostream&, const std::vector<T>&)/
// operator<<(std::ostream&, const std::set<T>&) below -- is only found for a LockedStream operand
// via ordinary lookup at LockedStream::operator<<'s own definition point (two-phase lookup), not
// via argument-dependent lookup on std::set<T>/std::vector<T>'s std:: namespace at the call site.
// Pulled in here rather than the other way around: util.h has no dependency back on this header.
#include "util.h"

namespace sjef::util {

class Logger {
  std::ostream* m_stream;
  int m_level;
  // Every Logger instance across every Project/Job typically points at the same process-wide
  // std::cout/std::cerr (e.g. a Job's background poll_job() thread and the main thread both log
  // through their own Logger member, both ultimately writing the same stream object).
  // std::ostream's formatted output isn't safe for concurrent, unsynchronized use from multiple
  // threads -- confirmed directly by ThreadSanitizer, which caught interleaved writes corrupting
  // the stream's internal padding/formatting state. One mutex, shared by every Logger regardless
  // of which stream it wraps, is enough: logging is not a contended hot path, so there's no need
  // for finer-grained per-stream locking.
  //
  // Recursive, not plain mutex: a LockedStream temporary holds this lock for its entire full
  // expression (see the comment on LockedStream below), and one of the values being streamed in
  // that expression can itself be a function call that logs -- e.g.
  // `m_trace(level) << "..." << run_needed(verbosity) << ...`, where run_needed()'s own body logs
  // through this same m_trace before the outer LockedStream temporary has been destroyed. A plain
  // mutex self-deadlocks the one thread that's already holding it in that case (confirmed
  // directly: this exact statement in Project::run(), calling Project::run_needed(), hung solid).
  static inline std::recursive_mutex s_stream_mutex;

  class NullBuffer : public std::streambuf {
  public:
    int overflow(int c) override { return c; }
  };
  NullBuffer m_null_buffer;
  mutable std::ostream m_null{&m_null_buffer};
  // m_null is per-instance, but a single instance (e.g. a Job's one m_trace member) is routinely
  // shared across threads -- its own background poll_job() task and the thread that constructed
  // it both log through the same Logger object. Two concurrent filtered-out calls (level >
  // m_level, the common case at low verbosity) still both touch m_null's own formatting state
  // even though the bytes go nowhere, which is exactly what ThreadSanitizer caught. A mutex
  // scoped to this instance rather than s_stream_mutex above keeps that cost from being paid
  // globally: unrelated Project/Job instances' own filtered calls never contend with each other.
  // Recursive for the same reason as s_stream_mutex above.
  mutable std::recursive_mutex m_null_mutex;
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

  // Returned by operator() in place of a bare std::ostream&, so that the lock taken for a
  // shared, contended stream (see s_stream_mutex above) stays held for the caller's whole
  // chained expression -- e.g. `m_trace(level) << a << b << std::endl;` -- rather than being
  // released the instant operator() itself returns, which would leave every `<<` after the first
  // just as unsynchronized as if no lock existed at all. A temporary's lifetime extends to the
  // end of the full expression it appears in, so the lock naturally releases right after the
  // final `<<` in a chain, with no explicit scoping needed at call sites.
  class LockedStream {
    std::unique_lock<std::recursive_mutex> m_lock;
    std::ostream* m_stream;

  public:
    LockedStream(std::recursive_mutex& mutex, std::ostream& stream) : m_lock(mutex), m_stream(&stream) {}
    LockedStream(LockedStream&&) = default;
    LockedStream(const LockedStream&) = delete;
    // Forwards to *m_stream's own operator<<, whichever overload that resolves to (built-in
    // types, manipulators, or sjef::util's own operator<<(std::ostream&, const std::vector<T>&)/
    // operator<<(std::ostream&, const std::set<T>&) -- see the include comment on this header for
    // why those are visible here at all). A plain implicit-conversion-to-ostream& approach doesn't
    // work here: template argument deduction (which every one of those overloads needs, being
    // templates themselves) never considers user-defined conversions, so a LockedStream operand
    // would never even become a candidate for them without this forwarding operator<< to bridge it.
    template <typename Arg> LockedStream& operator<<(Arg&& arg) {
      *m_stream << std::forward<Arg>(arg);
      return *this;
    }
    // std::endl/std::flush/std::ends and similar are function templates instantiated for
    // std::ostream specifically; without this overload, template argument deduction against the
    // generic Arg&& above can't resolve which instantiation was meant.
    LockedStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
      manip(*m_stream);
      return *this;
    }
  };

  LockedStream operator()(int level, const std::string& message = "") const {
    if (level > m_level)
      return LockedStream(m_null_mutex, m_null);
    LockedStream ls(s_stream_mutex, *m_stream);
    if (level >= 0 && level < decltype(level)(m_preambles.size()))
      ls << m_preambles[level];
    if (!message.empty())
      ls << message << std::endl;
    return ls;
  }
  LockedStream operator()(const Levels level, const std::string& message = "") const {
    return ((*this)(static_cast<int>(level), message));
  }
  LockedStream detail(const std::string& message = "") const { return ((*this)(Levels::detail, message)); }
  LockedStream notify(const std::string& message = "") const { return ((*this)(Levels::notification, message)); }
  LockedStream warn(const std::string& message = "") const { return ((*this)(Levels::warning, message)); }
  LockedStream error(const std::string& message = "") const { return ((*this)(Levels::error, message)); }
  int level() const { return m_level; }
  void set_level(int level) { Logger::m_level = level; }
  void set_level(Levels level) { Logger::m_level = static_cast<int>(level); }
  void set_stream(std::ostream& stream) { m_stream = &stream; }
};

} // namespace sjef::util

#endif // SJEF_LIB_UTIL_LOGGER_H_
