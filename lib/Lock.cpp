#include <iostream>
#include <mutex>
#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

#include "Lock.h"

namespace sjef {

std::mutex s_constructor_mutex;

Lock::Lock(const std::string& path) : m_path(path) {
  std::lock_guard<std::mutex> thread_lock(
      s_constructor_mutex); // helps atomicity of the following, but does not guarantee it between processes
//  std::cerr << "Lock "<<m_path<<std::endl;
#ifdef _WIN32
  if ((m_lock = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL)) == INVALID_HANDLE_VALUE)
#else
  m_lock = open(path.c_str(), O_RDWR | O_CREAT, 0666);
  if ((m_lock >= 0 and flock(m_lock, LOCK_EX) != 0) or (m_lock < 0 && (close(m_lock) or true)))
#endif
    throw std::runtime_error("Cannot create a lock on file " + path);
}

Lock::~Lock() {
  //  std::cerr << "~Lock "<<m_path<<std::endl;
#ifdef _WIN32
  if (m_lock != INVALID_HANDLE_VALUE)
    CloseHandle((HANDLE)m_lock);
#else
  std::remove(m_path.c_str());
  if (m_lock >= 0) {
    flock(m_lock, LOCK_UN);
    close(m_lock);
  }
#endif
}
} // namespace sjef
