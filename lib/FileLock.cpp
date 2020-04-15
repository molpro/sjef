#include <map>
#include <iostream>
#include <set>
#include <mutex>
#include <thread>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
namespace fs = boost::filesystem;

#include "FileLock.h"

struct sjef::Unique_FileLock { // process-level locking
  bool m_preexisting;
  std::string m_lockfile;
  std::unique_ptr<boost::interprocess::file_lock> m_lock;
  std::thread::id m_exclusive_owning_thread;
  std::set<std::thread::id> m_shared_using_threads;
  std::mutex m_mutex;
  std::mutex m_mutex_sharable;
  std::mutex m_mutex_acquire;
  std::condition_variable m_cv;
  std::condition_variable m_cv_sharable;
 public:
  Unique_FileLock(const std::string& path)
      :
      m_preexisting(boost::filesystem::exists(path)),
      m_lockfile(lockfile(path)),
      m_lock(new boost::interprocess::file_lock(m_lockfile.c_str())) {
  }
  ~Unique_FileLock() {
//    std::cout << "entering ~Unique_FileLock()" << std::endl;
//    std::cerr << "completely unlocking " << m_lockfile << std::endl;
    m_lock.reset(nullptr);
    if (not m_preexisting) fs::remove(m_lockfile);
//    system((std::string{"ls -l "} + m_lockfile).c_str());
//    std::cout << "leaving ~Unique_FileLock()" << std::endl;
  }
 private:
  std::string lockfile(const std::string& path) {
    if (!fs::exists(path))
      auto x = fs::ofstream(path);
    return path;
  }
};

std::map<std::string, std::shared_ptr<sjef::Unique_FileLock> > s_Unique_FileLocks;

sjef::FileLock::FileLock(const std::string& path, bool exclusive)
    :
    m_exclusive(exclusive) {
  if (s_Unique_FileLocks[path].use_count() <= 0)
    s_Unique_FileLocks[path] = std::make_shared<Unique_FileLock>(path);
  m_unique = s_Unique_FileLocks[path];
  while (m_unique->m_exclusive_owning_thread != std::thread::id()
      and m_unique->m_exclusive_owning_thread
          != std::this_thread::get_id()) { // wait for exclusive access to be relinquished
    std::unique_lock<std::mutex> mulock(m_unique->m_mutex);
//    std::cerr << "wait for m_mutex"<<std::endl;
    m_unique->m_cv.wait(mulock);
    mulock.unlock();
  }
//  m_unique->m_mutex_sharable.lock();
  if (exclusive) {
    std::lock_guard<std::mutex> mulock_acquire(m_unique->m_mutex_acquire);
//    std::cerr << "!! exclusively locking " << path
//    <<", this_thread="<<std::this_thread::get_id()
//    << std::endl;
    while (m_unique->m_shared_using_threads.count(std::this_thread::get_id())
        < m_unique->m_shared_using_threads.size()
//!=0
//      and m_unique->m_exclusive_owning_thread != std::this_thread::get_id()
        ) { // wait until other threads sharing relinquish their shared access
      std::unique_lock<std::mutex> mulock(m_unique->m_mutex_sharable);
      m_unique->m_cv_sharable.wait(mulock);
      mulock.unlock();
    }
    while (
        m_unique->m_exclusive_owning_thread != std::this_thread::get_id()
        and
 m_unique->m_exclusive_owning_thread != std::thread::id()
        ) { // wait until other threads sharing relinquish their exclusive access
      std::unique_lock<std::mutex> mulock(m_unique->m_mutex);
      m_unique->m_cv.wait(mulock);
      mulock.unlock();
    }
    m_unique->m_lock->lock();
    std::unique_lock<std::mutex> mulock(m_unique->m_mutex);
//    std::cerr << "lock m_mutex"<<std::endl;
    if (m_unique->m_exclusive_owning_thread != std::this_thread::get_id()) {
      m_unique->m_exclusive_owning_thread = std::this_thread::get_id();
    }
//    std::cerr << ".... acquired exclusive access" << std::endl;
  } else {
//    std::cerr << "!! sharable locking " << path << std::endl;
    m_unique->m_shared_using_threads.insert(std::this_thread::get_id());
    m_unique->m_lock->lock_sharable();
  }
}
sjef::FileLock::~FileLock() {
//  std::cout << "!! entering ~FileLock()" << std::endl;
  if (m_exclusive) {
    std::unique_lock<std::mutex> mulock(m_unique->m_mutex);
    m_unique->m_exclusive_owning_thread = std::thread::id();
    mulock.unlock();
    m_unique->m_cv.notify_all();
  }
  {
    std::unique_lock<std::mutex> mulock(m_unique->m_mutex_sharable);
    mulock.unlock();
    m_unique->m_cv_sharable.notify_all();
  }

//  std::cout << "unlocking " << m_unique->m_lockfile << fs::exists(m_unique->m_lockfile) << std::endl;
//  system((std::string{"ls -l "} + m_unique->m_lockfile).c_str());
//  std::cout << "m_unique use count " << m_unique.use_count() << std::endl;
//  std::cout << "s_Unique_FileLocks.size()" << s_Unique_FileLocks.size() << std::endl;
  if (m_unique.use_count() == 2) // I am the last user of the Unique_FileLock so it can be trashed
    s_Unique_FileLocks.erase(s_Unique_FileLocks.find(m_unique->m_lockfile));
//  std::cout << "m_unique use count " << m_unique.use_count() << std::endl;
  m_unique.reset();
//  std::cout << "leaving ~FileLock()" << std::endl;
}
std::string sjef::FileLock::lockfile(const std::string& path) {
  if (!fs::exists(path))
    auto x = fs::ofstream(path);
  return path;
}
