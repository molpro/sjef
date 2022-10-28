#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

#include "Locker.h"
#include <boost/interprocess/sync/file_lock.hpp>

namespace sjef::util {

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
    : m_path(lock_file(std::move(path))),
      m_file_lock(std::make_unique<boost::interprocess::file_lock>(fs::absolute(m_path).string().c_str())) {}
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
  m_file_lock->lock();
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
