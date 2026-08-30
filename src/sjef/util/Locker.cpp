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
// error under ordinary load -- observed in practice on an HPC cluster's shared home directory not
// just as an occasional microsecond-scale blip, but as bursts of failures recurring across several
// seconds -- with nothing wrong with the file or the lock itself. Retry with a backoff generous
// enough to ride that out (~8s total across 8 attempts) before giving up and letting the error
// propagate, rather than failing a job launch outright over a hiccup that would have gone unnoticed
// a moment later.
template <class F> inline auto retry_transient_io_error(F&& f) -> decltype(f()) {
  constexpr int max_attempts = 8;
  for (int attempt = 1;; ++attempt) {
    try {
      return f();
    } catch (const std::exception&) {
      if (attempt >= max_attempts)
        throw;
      std::this_thread::sleep_for(std::chrono::milliseconds(30 * (1 << (attempt - 1))));
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

Locker::Locker(fs::path path) : m_path(lock_file(std::move(path))) {}
Locker::~Locker() = default;

void Locker::add_bolt() {
  auto this_thread = std::this_thread::get_id();
  {
    std::lock_guard state_lock(m_state_mutex);
    // This check has to happen before we know whether this thread already holds m_mutex, so it
    // can't itself be guarded by m_mutex -- m_state_mutex exists purely to make this read (and
    // every write to the same fields, below and in remove_bolt()) race-free instead of racing
    // against another thread's genuinely-mutex-protected acquire or release happening right now.
    if (m_owning_thread == this_thread && m_bolts > 0) {
      m_bolts++;
      return;
    }
  }
  m_lock.reset(new std::scoped_lock<std::mutex>(m_mutex));
  {
    std::lock_guard state_lock(m_state_mutex);
    m_owning_thread = this_thread;
    m_bolts = 1;
  }
  try {
    // Opened here rather than in the constructor: this Locker is cached process-wide, keyed by
    // path, for as long as any Project referencing that path is alive (see make_locker() in
    // sjef.cpp) -- an eagerly-opened, never-closed handle would leak one file descriptor per
    // distinct project path ever opened, for the rest of the process, regardless of whether that
    // path's Project is still doing anything. Opening it fresh here and closing it again in
    // remove_bolt() below means the descriptor is only live for as long as a bolt actually is.
    // m_file_lock itself is only ever touched while m_mutex is held (here, and in remove_bolt()),
    // so it doesn't need m_state_mutex's protection the way m_bolts/m_owning_thread do.
    if (!m_file_lock) {
      // Recreate the lock file if it's gone missing since this Locker was constructed or last
      // used: this same process-wide, path-keyed Locker cache (see make_locker() in sjef.cpp) can
      // hand back a cached-but-idle Locker for a path whose directory was deleted and recreated in
      // the meantime -- e.g. Project::copy()'s destination reusing a path an earlier
      // Project::move() vacated -- since copying/recreating a project bundle doesn't itself copy
      // the old .lock file. An eagerly-opened, always-live file descriptor never needed this (the
      // descriptor stays valid via the file's inode regardless of what happens to the path
      // afterwards); reopening on demand does.
      lock_file(m_path);
      m_file_lock = retry_transient_io_error([this] {
        return std::make_unique<boost::interprocess::file_lock>(fs::absolute(m_path).string().c_str());
      });
    }
    retry_transient_io_error([this] { m_file_lock->lock(); return 0; });
  } catch (...) {
    // Roll back: without this, a lock() failure (even after retries) leaves m_mutex held with no
    // Bolt object ever fully constructed to release it via remove_bolt() -- the RAII Bolt whose
    // constructor called add_bolt() never finishes constructing, so its destructor never runs
    // either. Every subsequent bolt() call on this Locker, from any thread, would then block on
    // m_mutex forever.
    std::lock_guard state_lock(m_state_mutex);
    m_bolts = 0;
    m_owning_thread = {};
    m_lock.reset(nullptr);
    throw;
  }
}
void Locker::remove_bolt() {
  std::lock_guard state_lock(m_state_mutex);
  --m_bolts;
  if (m_bolts < 0)
    throw std::out_of_range("Locker::remove_bolt called too many times");
  if (m_bolts == 0) {
    m_file_lock->unlock();
    m_file_lock.reset(); // release the file descriptor now that nothing needs it locked
    // Without this, m_owning_thread keeps naming the thread that last held the bolt even though
    // m_bolts is back to 0 -- harmless on its own, but if that same thread's id is later reused
    // (or, before this fix, simply read without synchronization) it could make the reentrant check
    // above appear to match a thread that does not actually hold anything.
    m_owning_thread = {};
    m_lock.reset(nullptr);
  }
}

// RAII
Locker::Bolt Locker::bolt() { return Bolt(*this); }
Locker::Bolt::Bolt(Locker& locker) : m_locker(locker) { m_locker.add_bolt(); }
Locker::Bolt::~Bolt() { m_locker.remove_bolt(); }
} // namespace sjef
