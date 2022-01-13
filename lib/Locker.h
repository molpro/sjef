#ifndef SJEF_LIB_LOCKER_H_
#define SJEF_LIB_LOCKER_H_
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
namespace fs = std::filesystem;

namespace boost::interprocess {
class file_lock; ///< @private
}

namespace sjef {
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
 */
class Locker {
public:
  explicit Locker(fs::path path);
  virtual ~Locker();
  const fs::path& path() const { return m_path; }

public:
  void add_bolt();
  void remove_bolt();

private:
  const fs::path m_path;
  std::unique_ptr<std::lock_guard<std::mutex>> m_lock;
  std::mutex m_mutex;
  int m_bolts;
  const std::unique_ptr<boost::interprocess::file_lock> m_file_lock;
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
  const Bolt bolt();
};

} // namespace sjef
#endif // SJEF_LIB_LOCKER_H_
