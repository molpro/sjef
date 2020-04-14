#include <map>
#include <thread>
#include <iostream>
#include <mutex>
#include <boost/filesystem/file_status.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/filesystem/path.hpp>
namespace fs = boost::filesystem;

#include "FileLock.h"

struct Unique_FileLock { // process-level locking
  bool m_preexisting;
  std::string m_lockfile;
  std::unique_ptr<boost::interprocess::file_lock> m_lock;
  std::mutex m_mutex;
  std::mutex m_mutex_sharable;
 public:
  Unique_FileLock(const std::string& path)
      :
      m_preexisting(boost::filesystem::exists(path)),
      m_lockfile(lockfile(path)),
      m_lock(new boost::interprocess::file_lock(m_lockfile.c_str())) {
  }
  ~Unique_FileLock() {
    std::cout << "entering ~Unique_FileLock()" << std::endl;
    std::cerr << "completely unlocking " << m_lockfile << std::endl;
    m_lock.reset(nullptr);
    if (not m_preexisting) fs::remove(m_lockfile);
    system((std::string{"ls -l "} + m_lockfile).c_str());
    std::cout << "leaving ~Unique_FileLock()" << std::endl;
  }
 private:
  std::string lockfile(const std::string& path) {
    if (!fs::exists(path))
      auto x = fs::ofstream(path);
    return path;
  }
};

std::map<std::string, std::shared_ptr<Unique_FileLock> > s_Unique_FileLocks;

sjef::FileLock::FileLock(const std::string& path, bool exclusive)
    :
    m_exclusive(exclusive) {
  if (s_Unique_FileLocks[path].use_count() <= 0)
    s_Unique_FileLocks[path] = std::make_shared<Unique_FileLock>(path);
  m_unique = s_Unique_FileLocks[path];
  m_unique->m_mutex_sharable.lock();
  if (exclusive) {
    std::cerr << "!! exclusively locking " << path << std::endl;
    m_unique->m_lock->lock();
    m_unique->m_mutex.lock();
  } else {
    std::cerr << "!! sharable locking " << path << std::endl;
    m_unique->m_lock->lock_sharable();
  }
}
sjef::FileLock::~FileLock() {
  std::cout << "!! entering ~FileLock()" << std::endl;
  if (m_exclusive)
    m_unique->m_mutex.unlock();
  m_unique->m_mutex_sharable.unlock();
  std::cout << "unlocking " << m_unique->m_lockfile << fs::exists(m_unique->m_lockfile) << std::endl;
  system((std::string{"ls -l "} + m_unique->m_lockfile).c_str());
  std::cout << "m_unique use count " << m_unique.use_count() << std::endl;
  if (m_unique.use_count() == 2) // I am the last user of the Unique_FileLock so it can be trashed
    s_Unique_FileLocks.erase(s_Unique_FileLocks.find(m_unique->m_lockfile));
  std::cout << "m_unique use count " << m_unique.use_count() << std::endl;
  m_unique.reset();
  std::cout << "leaving ~FileLock()" << std::endl;
}
std::string sjef::FileLock::lockfile(const std::string& path) {
  if (!fs::exists(path))
    auto x = fs::ofstream(path);
  return path;
}
std::string sjef::FileLock::str() const {
  return "";
//  return std::string(m_exclusive ? "exclusive" : "sharable") + " lock "
//      + std::to_string(static_cast<const int64_t>(reinterpret_cast<intptr_t>(this))) + ":" + m_lockfile
//      + ", try_lock()=" + (m_lock->try_lock() ? "T" : "F") + ", try_lock_sharable()="
//      + (m_lock->try_lock_sharable() ? "T" : "F");
}
std::mutex s_FileLocks_mutex;
void sjef::FileLock::show_FileLocks() {
  const std::lock_guard<std::mutex> lock(s_FileLocks_mutex);
//  std::cerr << s_Unique_FileLocks.size() << " active FileLocks: " << std::endl;
//  for (const auto& entry : s_Unique_FileLocks)
//    std::cerr << "lock: " << *entry.first << ", thread: " << entry.second << std::endl;
}

