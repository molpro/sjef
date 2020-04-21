#include <map>
#include <iostream>
#include <mutex>
#include <thread>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
namespace fs = boost::filesystem;
constexpr int debug = 0;

#include "FileLock.h"

std::ostream&
print_one(std::ostream& os) {
  return os;
}

template<class A0, class ...Args>
std::ostream&
print_one(std::ostream& os, const A0& a0, const Args& ...args) {
  os << a0;
  return print_one(os, args...);
}

template<class ...Args>
std::ostream&
print(std::ostream& os, const Args& ...args) {
  return print_one(os, args...);
}

std::mutex&
get_cout_mutex() {
  static std::mutex m;
  return m;
}

template<class ...Args>
std::ostream&
print(const Args& ...args) {
  std::lock_guard<std::mutex> _(get_cout_mutex());
  return print(std::cout, args..., "\n");
}
static std::map<std::thread::id, int> symbolic_threads;
static int symbolic_thread = 0;

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
    if (debug)
      print("Unique_FileLock m_preexisting=", m_preexisting);
  }
  ~Unique_FileLock() {
    if (debug > 1) {
      print("!u  entering ~Unique_FileLock() on thread ", symbolic_threads[std::this_thread::get_id()]);
      print("!u  completely unlocking ", m_lockfile, " on thread ", symbolic_threads[std::this_thread::get_id()]);
    }
    m_file_lock.reset(nullptr);
    if (not m_preexisting) {
      if (debug > 0)
        print("!u  removing ", m_lockfile);
      fs::remove(m_lockfile);
    }
    if (debug > 1)
      print("!u  leaving ~Unique_FileLock()", " on thread ", symbolic_threads[std::this_thread::get_id()]);
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
  if (debug > 0)
    print("!!f FileLock ",
          m_unique->m_lockfile,
          " thread=",
          symbolic_threads[std::this_thread::get_id()],
          " has locked file, exclusive=", m_exclusive);
  m_unique->m_entry_count++;
  m_unique->m_thread_entry_count[std::this_thread::get_id()]++;
  if (debug > 0) {
    if (symbolic_threads.count(std::this_thread::get_id()) == 0)
      symbolic_threads[std::this_thread::get_id()] = symbolic_thread++;
    print("!!! FileLock ",
          path,
          " thread=",
          symbolic_threads[std::this_thread::get_id()],
          " entry count ",
          m_unique->m_entry_count,
          " exclusive = ",
          exclusive,
          " erase_if_created = ",
          erase_if_created);
  }
}

sjef::FileLock::~FileLock() {
  m_unique->m_entry_count--;
  m_unique->m_thread_entry_count[std::this_thread::get_id()]--;
  if (debug > 0)
    print("!!! ~FileLock ",
          m_unique->m_lockfile,
          " thread=",
          symbolic_threads[std::this_thread::get_id()],
          " entry count ",
          m_unique->m_entry_count,
          " thread entry count ",
          m_unique->m_thread_entry_count[std::this_thread::get_id()]
    );
  if (m_unique->m_entry_count == 0) {
    if (m_exclusive)
      m_unique->m_file_lock->unlock();
    else
      m_unique->m_file_lock->unlock_sharable();
    if (debug > 0)
      print("!!f ~FileLock ",
            m_unique->m_lockfile,
            " thread=",
            symbolic_threads[std::this_thread::get_id()],
            " has unlocked file, exclusive=", m_exclusive);
  }

  {
    std::lock_guard<std::mutex> s_Unique_FileLocks_guard(s_Unique_FileLocks_mutex);
//    print("s_Unique_FileLocks[m_unique->m_lockfile].use_count() ",s_Unique_FileLocks[m_unique->m_lockfile].use_count());
    if (s_Unique_FileLocks[m_unique->m_lockfile].use_count() <= 2) // I am the last user of the Unique_FileLock so it can be trashed
    {
      s_Unique_FileLocks.erase((m_unique->m_lockfile));
      if (debug > 0)
        print("!!  erasing entry in Unique_Filelock table",
              " thread=",
              symbolic_threads[std::this_thread::get_id()],
              " has unlocked file");
    }
  }
  if (debug > 0)
    print("!!! leaving ~FileLock()", " thread=", symbolic_threads[std::this_thread::get_id()]);
}
