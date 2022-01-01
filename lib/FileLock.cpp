#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <map>
#include <mutex>
#include <thread>
//namespace fs = boost::filesystem;
#include <filesystem>
#include <fstream>
#include <boost/interprocess/sync/file_lock.hpp>
namespace fs = std::filesystem;

#include "FileLock.h"
static std::string s_lockfile(const std::string& path) {
  if (!fs::exists(path))
    auto x = std::ofstream(path);
  return path;
}

struct sjef::FileLock::Unique_FileLock { // process-level locking
  bool m_preexisting;
  std::string m_lockfile;
  std::unique_ptr<boost::interprocess::file_lock> m_file_lock;
  mutex_t m_mutex;
  int m_entry_count;

public:
  Unique_FileLock(const std::string& path, bool erase_if_created = true)
      : m_preexisting(std::filesystem::exists(path) or not erase_if_created), m_lockfile(s_lockfile(path)),
        m_file_lock(new boost::interprocess::file_lock(m_lockfile.c_str())), m_entry_count(0) {}
  ~Unique_FileLock() {
    m_file_lock.reset(nullptr);
    if (not m_preexisting) {
      fs::remove(m_lockfile);
    }
  }
};

std::map<std::string, std::shared_ptr<sjef::FileLock::Unique_FileLock>> s_Unique_FileLocks;
std::mutex s_Unique_FileLocks_mutex;

sjef::FileLock::FileLock(const std::string& path, bool exclusive, bool erase_if_created) : m_exclusive(exclusive) {
  {
    std::lock_guard<std::mutex> s_Unique_FileLocks_guard(s_Unique_FileLocks_mutex);
    if (s_Unique_FileLocks.count(path) <= 0)
      s_Unique_FileLocks[path] = std::make_shared<Unique_FileLock>(path, erase_if_created);
    m_unique = s_Unique_FileLocks[path];
  }

  if (m_exclusive) {
    m_lock_guard.reset(new std::lock_guard(m_unique->m_mutex));
    m_unique->m_file_lock->lock();
  } else {
    m_shared_lock.reset(new std::shared_lock(m_unique->m_mutex));
    m_unique->m_file_lock->lock_sharable();
  }
  m_unique->m_entry_count++;
}

sjef::FileLock::~FileLock() {
  m_unique->m_entry_count--;
  if (m_unique->m_entry_count == 0) {
    if (m_exclusive)
      m_unique->m_file_lock->unlock();
    else
      m_unique->m_file_lock->unlock_sharable();
  }

  {
    std::lock_guard<std::mutex> s_Unique_FileLocks_guard(s_Unique_FileLocks_mutex);
    if (s_Unique_FileLocks[m_unique->m_lockfile].use_count() <=
        2) // I am the last user of the Unique_FileLock so it can be trashed
    {
      s_Unique_FileLocks.erase((m_unique->m_lockfile));
    }
  }
}
