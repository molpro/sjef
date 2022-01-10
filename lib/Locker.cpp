#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <boost/interprocess/sync/file_lock.hpp>
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
std::mutex s_mutex;
std::map<std::pair<std::thread::id,fs::path>, int> s_bolts;

Locker::Locker(fs::path path) : m_path((std::move(path))) {}
Locker::~Locker() {}

void Locker::add_bolt() {
//  std::cout << "Locker::add_bolt() " << this << " " << std::this_thread::get_id() << m_path << std::endl;
  {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_bolts.count({std::this_thread::get_id(),m_path}) == 0)
      s_bolts[{std::this_thread::get_id(),m_path}] = 0;
  }
  if (s_bolts[{std::this_thread::get_id(),m_path}] == 0) {
//    std::cout << "Locker::add_bolt() tries to acquire mutex thread=" << std::this_thread::get_id() << ", path=" << m_path
//              << std::endl;
    m_interprocess_lock.reset(new Interprocess_lock(m_path));
//    std::cout << "Locker::add_bolt() acquires mutex thread=" << std::this_thread::get_id() << ", path=" << m_path
//        << std::endl;
  }
  ++s_bolts[{std::this_thread::get_id(),m_path}];
//  std::cout << "add_bolt increases bolts to " << s_bolts[{std::this_thread::get_id(),m_path}] << std::endl;
}
void Locker::remove_bolt() {
//  std::cout << "Locker::remove_bolt() " << std::this_thread::get_id() << " " << m_path << std::endl;
  --s_bolts[{std::this_thread::get_id(),m_path}];
//  std::cout << "remove_bolt decreases bolts to " << s_bolts[{std::this_thread::get_id(),m_path}] << std::endl;
  if (s_bolts[{std::this_thread::get_id(),m_path}] < 0)
    throw std::logic_error("Locker::remove_bolt called too many times");
  if (s_bolts[{std::this_thread::get_id(),m_path}] == 0) {
//    std::cout << "Locker::remove_bolt() releases mutex " << std::this_thread::get_id() << m_path << std::endl;
    m_interprocess_lock.reset(nullptr);
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
//    std::cout << "Interprocess_lock acquiring "<<std::this_thread::get_id()<<" "<<m_path<<std::endl;
  std::stringstream ss;
  ss << std::this_thread::get_id();
#ifdef _WIN32
  //  if ((m_lock = CreateFileA(m_path.string().c_str(), GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, 0, NULL)) ==
  //      INVALID_HANDLE_VALUE)
  // if (((m_lock = CreateMutex(0, FALSE, m_path.string().c_str())) == INVALID_HANDLE_VALUE) ||
  // (WaitForSingleObject(m_lock,INFINITE)==WAIT_FAILED))
  if (false)
#else
  m_lock = open(m_path.string().c_str(), O_RDWR | O_CREAT, 0666);
  if ((m_lock >= 0 and flock(m_lock, LOCK_EX) != 0) or (m_lock < 0 && (close(m_lock) or true)))
//  if (false)
#endif
  {
#ifdef _WIN32
    std::cerr << "GetLastError: " << GetLastError() << std::endl;
#endif
    throw std::runtime_error("Cannot create a lock on file " + path.string() + " from thread" + ss.str());
  }
//  std::cout << "Interprocess_lock acquired "<<std::this_thread::get_id()<<" "<<m_path<<std::endl;
}

Interprocess_lock::~Interprocess_lock() {
//    std::cerr << "~Interprocess_lock "<<std::this_thread::get_id()<<" "<<m_path<<std::endl;
#ifdef _WIN32
// TODO restore Windows interprocess lock
//  if (m_lock != INVALID_HANDLE_VALUE) {
//    ReleaseMutex(m_lock);
//    CloseHandle((HANDLE)m_lock);
//  }
#else
  if (m_lock >= 0) {
    flock(m_lock, LOCK_UN);
    close(m_lock);
  }
#endif
}
} // namespace sjef
