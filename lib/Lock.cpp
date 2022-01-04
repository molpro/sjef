#include <iostream>
//#include <mutex>
#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

#include "Lock.h"

namespace sjef {
const fs::path Lock::directory_lock_file = ".lock";

// std::mutex s_constructor_mutex;

Lock::Lock(const fs::path& path,const fs::path& directory_lock_file) : m_path(fs::is_directory(path) ? path / directory_lock_file : path) {
//  std::lock_guard<std::mutex> thread_lock(
//      s_constructor_mutex); // helps atomicity of the following, but does not guarantee it between processes
//  std::cerr << "Lock "<<m_path<<std::endl;
#ifdef _WIN32
  if ((m_lock = CreateFileA(m_path.string().c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL)) ==
      INVALID_HANDLE_VALUE)
#else
  m_lock = open(m_path.string().c_str(), O_RDWR | O_CREAT, 0666);
  if ((m_lock >= 0 and flock(m_lock, LOCK_EX) != 0) or (m_lock < 0 && (close(m_lock) or true)))
#endif
    throw std::runtime_error("Cannot create a lock on file " + path.string());
}

Lock::~Lock() {
  //  std::cerr << "~Lock "<<m_path<<std::endl;
#ifdef _WIN32
  if (m_lock != INVALID_HANDLE_VALUE)
    CloseHandle((HANDLE)m_lock);
#else
  if (m_lock >= 0) {
    flock(m_lock, LOCK_UN);
    close(m_lock);
  }
#endif
}
} // namespace sjef
