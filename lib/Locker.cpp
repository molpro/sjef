#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
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
std::mutex s_mutex;

inline std::string hash_path(const fs::path& path) {
  auto parent = path.parent_path();
  if (not parent.string().empty())
    fs::create_directories(parent);
  auto x = std::hash<std::string>{}(fs::absolute(path).string());
  return std::to_string(x);
}
inline fs::path lock_file(fs::path path) {
  auto result = fs::is_directory(path) ? std::move(path) / Interprocess_lock::directory_lock_file : std::move(path);
  if (not fs::exists(result))
    try {
      const auto& parent_path = result.parent_path();
      if (not parent_path.empty()) {
//        std::cout << "create directory " << parent_path.string() << std::endl;
        fs::create_directories(parent_path);
      }
//      std::cout << "create " << result.string() << std::endl;
      std::ofstream(result.string()) << "";
    } catch (...) {
      throw std::runtime_error("Cannot create lock file " + result.string());
    }
  return result;
}

Locker::Locker(fs::path path)
    : m_path(lock_file(std::move(path))), m_bolts(0),
      m_file_lock(boost::interprocess::file_lock{fs::absolute(m_path).string().c_str()}) {}
Locker::~Locker() {}

void Locker::add_bolt() {
//  std::cout << "Locker::add_bolt() " << this << " " << std::this_thread::get_id() << " " << m_path << std::endl;
  auto this_thread = std::this_thread::get_id();
  if (m_owning_thread == this_thread and m_bolts > 0) {
    m_bolts++;
    return;
  }
  m_lock.reset(new std::lock_guard<std::mutex>(m_mutex));
  m_owning_thread = this_thread;
  m_bolts = 1;
  m_file_lock.lock();
//  std::cout << "Interprocess_lock acquired " << std::this_thread::get_id() << " " << m_path << std::endl;
}
void Locker::remove_bolt() {
  --m_bolts;
  //  std::cout << "remove_bolt decreases bolts to " << s_bolts[{std::this_thread::get_id(),m_path}] << std::endl;
  if (m_bolts < 0)
    throw std::logic_error("Locker::remove_bolt called too many times");
  if (m_bolts == 0) {
    //    std::cout << "Locker::remove_bolt() releases mutex " << std::this_thread::get_id() << m_path << std::endl;
    m_file_lock.unlock();
//    std::cout << "Interprocess_lock relinquished " << std::this_thread::get_id() << " " << m_path << std::endl;
    m_lock.reset(nullptr);
  }
}

// RAII
const Locker::Bolt Locker::bolt() { return Bolt(*this); }
Locker::Bolt::Bolt(Locker& locker) : m_locker(locker) { m_locker.add_bolt(); }
Locker::Bolt::~Bolt() { m_locker.remove_bolt(); }
} // namespace sjef
