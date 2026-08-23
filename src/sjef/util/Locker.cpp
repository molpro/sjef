#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

#include "Locker.h"
#include <boost/interprocess/sync/file_lock.hpp>

namespace sjef::util {

// Opening or locking the lock file are both a single syscall against whatever filesystem the project
// lives on. On a networked filesystem (NFS and similar), that syscall can fail with a transient I/O
// error under ordinary load -- observed in practice on an HPC cluster's shared home directory -- with
// nothing wrong with the file or the lock itself. Retry a few times with a short backoff before
// giving up and letting the error propagate, rather than failing a job launch outright over a hiccup
// that would have gone unnoticed a moment later.
template <class F> inline auto retry_transient_io_error(F&& f) -> decltype(f()) {
  constexpr int max_attempts = 5;
  for (int attempt = 1;; ++attempt) {
    try {
      return f();
    } catch (const std::exception&) {
      if (attempt >= max_attempts)
        throw;
      std::this_thread::sleep_for(std::chrono::milliseconds(20 * (1 << (attempt - 1))));
    }
  }
}

inline std::string hash_path(const fs::path& path) {
  if (auto parent = path.parent_path(); !parent.string().empty())
    fs::create_directories(parent);
  auto x = std::hash<std::string>{}(fs::absolute(path).string());
  return std::to_string(x);
}
inline fs::path lock_file(fs::path path) {
  auto result = fs::is_directory(path) ? path / (std::move(path).stem().string() + ".lock")
                                       : std::move(path);
  if (!fs::exists(result))
    try {
      if (const auto& parent_path = result.parent_path(); !parent_path.empty()) {
        fs::create_directories(parent_path);
      }
      std::ofstream(result.string()) << "";
    } catch (...) {
      throw std::domain_error("Cannot create lock file " + result.string());
    }
  return result;
}

Locker::Locker(fs::path path)
    : m_path(lock_file(std::move(path))), m_file_lock(retry_transient_io_error([this] {
        return std::make_unique<boost::interprocess::file_lock>(fs::absolute(m_path).string().c_str());
      })) {}
Locker::~Locker() = default;

void Locker::add_bolt() {
  auto this_thread = std::this_thread::get_id();
  if (m_owning_thread == this_thread && m_bolts > 0) {
    m_bolts++;
    return;
  }
  m_lock.reset(new std::scoped_lock<std::mutex>(m_mutex));
  m_owning_thread = this_thread;
  m_bolts = 1;
  try {
    retry_transient_io_error([this] { m_file_lock->lock(); return 0; });
  } catch (...) {
    // Roll back: without this, a lock() failure (even after retries) leaves m_mutex held with no
    // Bolt object ever fully constructed to release it via remove_bolt() -- the RAII Bolt whose
    // constructor called add_bolt() never finishes constructing, so its destructor never runs
    // either. Every subsequent bolt() call on this Locker, from any thread, would then block on
    // m_mutex forever.
    m_bolts = 0;
    m_owning_thread = {};
    m_lock.reset(nullptr);
    throw;
  }
}
void Locker::remove_bolt() {
  --m_bolts;
  if (m_bolts < 0)
    throw std::out_of_range("Locker::remove_bolt called too many times");
  if (m_bolts == 0) {
    m_file_lock->unlock();
    m_lock.reset(nullptr);
  }
}

// RAII
Locker::Bolt Locker::bolt() { return Bolt(*this); }
Locker::Bolt::Bolt(Locker& locker) : m_locker(locker) { m_locker.add_bolt(); }
Locker::Bolt::~Bolt() { m_locker.remove_bolt(); }
} // namespace sjef
