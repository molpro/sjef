#ifndef SJEF_LIB_LOCKER_H_
#define SJEF_LIB_LOCKER_H_
#define BOOST_ALL_NO_LIB
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
namespace fs = std::filesystem;

namespace boost::interprocess {
class file_lock; ///< @private
}

namespace sjef::util {
/*!
 * @brief A thread-safe class for an inter-thread/inter-process lock.
 * The lock mechanism is based on a locked file in the file system.
 * The locked file is a file that may or may not already exist; if it doesn't, it is created.
 * The file contents are not altered, and the file is not deleted.
 * If the specified file is a directory, the lock is instead made on a file in that directory.
 * All threads in the same process using the locker must do so through the same Locker object.
 *
 * The lock is initially open, and is closed by calling add_bolt() and reopened with remove_bolt(); add_bolt() can be
 * called multiple times, with only the first instance having a real effect, and the lock being released when the last
 * bolt is removed. It is recommended not to call add_bolt() directly, but to use the RAII pattern provided by the
 * bolt() function.
 *
 * The underlying file descriptor used to hold the lock is opened only while at least one bolt is held, and closed
 * again once the last one is removed -- not kept open for this object's whole lifetime.
 */
class Locker {
public:
  explicit Locker(fs::path path);
  virtual ~Locker();
  const fs::path& path() const { return m_path; }

  void add_bolt();
  void remove_bolt();

private:
  const fs::path m_path;
  std::unique_ptr<std::scoped_lock<std::mutex>> m_lock;
  std::mutex m_mutex;
  // Guards m_bolts, m_owning_thread and m_file_lock against add_bolt()'s reentrant-call check,
  // which (by design) has to read m_owning_thread/m_bolts *before* it knows whether this thread
  // already holds m_mutex -- so that read can't itself be protected by m_mutex. Without a separate
  // mutex for just these fields, that read races every other thread's writes to them.
  std::mutex m_state_mutex;
  int m_bolts = 0;
  // Opened lazily, only while a bolt is actually held (see add_bolt()/remove_bolt()), rather than
  // for this Locker's whole lifetime: a Locker lives in a process-wide, path-keyed cache for as
  // long as any Project pointing at that path is alive, so an eagerly-opened, never-closed handle
  // here means one leaked file descriptor per distinct project path ever opened, for the life of
  // the process -- fine for a handful of projects, but exhausted a constrained file descriptor
  // budget outright on a workload that legitimately visits thousands of distinct paths (many
  // parameter variants across many molecules) while keeping every resulting Project alive.
  std::unique_ptr<boost::interprocess::file_lock> m_file_lock;
  std::thread::id m_owning_thread;

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
  Bolt bolt();
};

} // namespace sjef::util
#endif // SJEF_LIB_LOCKER_H_
