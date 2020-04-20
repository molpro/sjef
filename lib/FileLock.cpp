#include <map>
#include <iostream>
#include <mutex>
#include <thread>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
namespace fs = boost::filesystem;
const int debug = 0;

#include "FileLock.h"

static std::string s_lockfile(const std::string& path) {
  if (!fs::exists(path))
    auto x = fs::ofstream(path);
  return path;
}

struct sjef::FileLock::Unique_FileLock { // process-level locking
  bool m_preexisting;
  std::string m_lockfile;
  std::unique_ptr<boost::interprocess::file_lock> m_file_lock;
  std::recursive_mutex m_mutex;
  int m_entry_count;
  std::map<std::thread::id, int> m_thread_entry_count;
 public:
  Unique_FileLock(const std::string& path, bool erase_if_created = true)
      :
      m_preexisting(boost::filesystem::exists(path) or not erase_if_created),
      m_lockfile(s_lockfile(path)),
      m_file_lock(new boost::interprocess::file_lock(m_lockfile.c_str())),
      m_entry_count(0) {
  }
  ~Unique_FileLock() {
    if (debug > 1) {
      std::cout << "entering ~Unique_FileLock()" << std::endl;
      std::cerr << "completely unlocking " << m_lockfile << std::endl;
    }
    m_file_lock.reset(nullptr);
    if (not m_preexisting) {
      if (debug > 0)
        std::cout << "removing " << m_lockfile << std::endl;
      fs::remove(m_lockfile);
    }
    if (debug > 1)
      std::cout << "leaving ~Unique_FileLock()" << std::endl;
  }
};

std::map<std::string, std::shared_ptr<sjef::FileLock::Unique_FileLock> > s_Unique_FileLocks;
std::mutex s_Unique_FileLocks_mutex;

sjef::FileLock::FileLock(const std::string& path, bool exclusive, bool erase_if_created)
    :
    m_exclusive(exclusive) {
  {
    std::lock_guard<std::mutex> s_Unique_FileLocks_guard(s_Unique_FileLocks_mutex);
    if (s_Unique_FileLocks.count(path) <= 0)
      s_Unique_FileLocks[path] = std::make_shared<Unique_FileLock>(path, erase_if_created);
    m_unique = s_Unique_FileLocks[path];
  }
  m_lock_guard.reset(new std::lock_guard<std::recursive_mutex>(m_unique->m_mutex));

  if (m_exclusive)
    m_unique->m_file_lock->lock();
  else
    m_unique->m_file_lock->lock_sharable();
  m_unique->m_entry_count++;
  m_unique->m_thread_entry_count[std::this_thread::get_id()]++;
  if (debug > 0)
    std::cout << "!!! FileLock " << path << " : " << std::this_thread::get_id() << " entry count "
              << m_unique->m_entry_count << " " << &(m_unique->m_mutex) << " exclusive = " << exclusive
              << " erase_if_created = " << erase_if_created << std::endl;
}

sjef::FileLock::~FileLock() {
  m_unique->m_entry_count--;
  m_unique->m_thread_entry_count[std::this_thread::get_id()]--;
  if (debug > 0)
    std::cout << "!!! ~FileLock " << m_unique->m_lockfile << " : " << std::this_thread::get_id()
              << " entry count " << m_unique->m_entry_count
              << " thread entry count " << m_unique->m_thread_entry_count[std::this_thread::get_id()]
              << std::endl;
  if (m_unique->m_entry_count == 0) {
    if (m_exclusive)
      m_unique->m_file_lock->unlock();
    else
      m_unique->m_file_lock->unlock_sharable();
    if (debug > 0)
      std::cout << "!!  ~FileLock " << m_unique->m_lockfile << " : " << std::this_thread::get_id()
                << " has unlocked file"
                << std::endl;
  }

  {
    std::lock_guard<std::mutex> s_Unique_FileLocks_guard(s_Unique_FileLocks_mutex);
    if
        (m_unique->m_entry_count == 0
        ) // I am the last user of the Unique_FileLock so it can be trashed
      s_Unique_FileLocks.erase((m_unique->m_lockfile));
    if (debug > 0)
      std::cout << "!!  erasing entry in Unique_Filelock table" << std::endl;
  }
  if (debug > 0)
    std::cout << "!!! leaving ~FileLock()" << std::endl;
}
