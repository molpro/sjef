#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

#include "Locker.h"

std::map<std::string, std::mutex> mutexes;

namespace sjef {
const fs::path Interprocess_lock::directory_lock_file = ".lock";

Locker::Locker(fs::path path) : m_path(std::move(path)) {}
Locker::~Locker() {}

void Locker::add_bolt() {
  {
    std::lock_guard<std::mutex> lock(m_bolts_mutex);
    if (m_bolts.count(std::this_thread::get_id()) == 0)
      m_bolts[std::this_thread::get_id()] = 0;
  }
  if (m_bolts[std::this_thread::get_id()] == 0) {
    m_lock_guard.reset(new std::lock_guard<std::mutex>(m_mutex));
    m_interprocess_lock.reset(new Interprocess_lock(m_path));
  }
  ++m_bolts[std::this_thread::get_id()];
//  std::cout << "add_bolt increases bolts to " << m_bolts[std::this_thread::get_id()] << std::endl;
}
void Locker::remove_bolt() {
  --m_bolts[std::this_thread::get_id()];
//  std::cout << "remove_bolt decreases bolts to " << m_bolts[std::this_thread::get_id()] << std::endl;
  if (m_bolts[std::this_thread::get_id()] < 0)
    throw std::logic_error("Locker::remove_bolt called too many times");
  if (m_bolts[std::this_thread::get_id()] == 0) {
    m_interprocess_lock.reset(nullptr);
    m_lock_guard.reset(nullptr);
  }
}

// RAII
const Locker::Bolt Locker::bolt() { return Bolt(*this); }
Locker::Bolt::Bolt(Locker& locker) : m_locker(locker) { m_locker.add_bolt(); }
Locker::Bolt::~Bolt() { m_locker.remove_bolt(); }

// std::mutex s_constructor_mutex;
Interprocess_lock::Interprocess_lock(const fs::path& path, const fs::path& directory_lock_file)
    : m_path(fs::is_directory(path) ? path / directory_lock_file : path) {
//  std::lock_guard<std::mutex> thread_lock(
//      s_constructor_mutex); // helps atomicity of the following, but does not guarantee it between processes
//  std::cerr << "Lock "<<m_path<<std::this_thread::get_id()<<std::endl;
#ifdef _WIN32
// TODO restore Windows interprocess lock
//  if ((m_lock = CreateFileA(m_path.string().c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL)) ==
//      INVALID_HANDLE_VALUE)
if (false)
#else
  m_lock = open(m_path.string().c_str(), O_RDWR | O_CREAT, 0666);
  if ((m_lock >= 0 and flock(m_lock, LOCK_EX) != 0) or (m_lock < 0 && (close(m_lock) or true)))
#endif
    throw std::runtime_error("Cannot create a lock on file " + path.string());
}

Interprocess_lock::~Interprocess_lock() {
//    std::cerr << "~Lock "<<m_path<<std::this_thread::get_id()<<std::endl;
#ifdef _WIN32
// TODO restore Windows interprocess lock
  if (false and m_lock != INVALID_HANDLE_VALUE)
    CloseHandle((HANDLE)m_lock);
#else
  if (m_lock >= 0) {
    flock(m_lock, LOCK_UN);
    close(m_lock);
  }
#endif
}
} // namespace sjef
