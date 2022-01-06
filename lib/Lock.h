#ifndef SJEF_LIB_LOCK_H_
#define SJEF_LIB_LOCK_H_
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
namespace fs = std::filesystem;

namespace sjef {

class Lock;
/*!
 * @brief A thread-safe class for an inter-thread/inter-process lock.
 * The lock mechanism is based on std::mutex between threads, and a locked file in the file system.
 * The locked file is a file that may or may not already exist; if it doesn't, it is created.
 * The file contents are not altered, and the file is not deleted.
 * If the specified file is a directory, the lock is instead made on a file in that directory.
 *
 * The lock is initially open, and is closed by calling add_bolt() and reopened with remove_bolt(); add_bolt() can be
 * called multiple times, with only the first instance having a real effect, and the lock being released when the last
 * bolt is removed. It is recommended not to call add_bolt() directly, but to use the RAII pattern provided by the
 * bolt() function.
 */
class Locker {
public:
  explicit Locker(fs::path path);
  virtual ~Locker();
  void add_bolt();
  void remove_bolt();

private:
  fs::path m_path;
  std::map<std::thread::id, int> m_bolts;
  std::mutex m_mutex;
  std::mutex m_bolts_mutex;
  std::unique_ptr<std::lock_guard<std::mutex>> m_lock_guard;
  std::unique_ptr<Lock> m_interprocess_lock;

public:
  // RAII
  struct Bolt {
    explicit Bolt(Locker& locker);
    ~Bolt();
    Bolt() = delete;
    Bolt(const Bolt&) = delete;
    Bolt& operator=(const Bolt&) = delete;

  private:
    Locker& m_locker;
  };
  const Bolt bolt();
};

/*!
 * @brief A thread-unsafe class for a lock based on a global mutex in the file system.
 * The mutex is a file that may or may not already exist; if it doesn't, it is created.
 * The file contents are not altered, and the file is not deleted.
 * If the specified file is a directory, the lock is instead made on a file in that directory.
 * On creation of a class instance, execution will wait until access to the file can be obtained,
 * and the access obtained will block other subsequent access requests until the object is destroyed.
 */
class Lock {
public:
  /*!
   * @brief Assert and obtain access to a file for the lifetime of a class instance.
   * @param path The file to be locked.
   * @param directory_lock_file In the case that path is a directory, the name of the file in that directory that will
   * be used for the lock
   */
  explicit Lock(const fs::path& path, const fs::path& directory_lock_file = Lock::directory_lock_file);
  virtual ~Lock();
  const static fs::path directory_lock_file;

private:
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;

#ifdef _WIN32
  using handle_t = void*;
#else
  using handle_t = int;
#endif
  handle_t m_lock;
  fs::path m_path;
};

} // namespace sjef
#endif // SJEF_LIB_LOCK_H_
