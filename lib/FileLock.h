#ifndef SJEF_LIB_FILELOCK_H_
#define SJEF_LIB_FILELOCK_H_
#include <mutex>
#include <memory>
#include <string>

class Unique_FileLock;
/*!
 * @brief A thread-safe class for managing exclusive and non-exclusive access to a file
 * Implementation within a process is via std::mutex, and between processes via boost::interprocess:file_lock
 * On creation of a class instance, execution will wait until access to the file can be obtained,
 * and the access obtained will influence other subsequent access requests until the object is destroyed.
 */
namespace sjef {

class FileLock {
 public:
  bool m_exclusive;
  std::shared_ptr<Unique_FileLock> m_unique;
  explicit FileLock(const std::string& path, bool exclusive = true);
  ~FileLock();
  /*!
   * @brief Produces a printable representation of the state of the lock
   * @return
   */
  std::string str() const;
  static void show_FileLocks();
 private:
  std::string lockfile(const std::string& path);
};
inline std::ostream& operator<<(std::ostream& s, const FileLock& lock) {
  s << lock.str();
  return s;
}

void show_FileLocks();

}
#endif //SJEF_LIB_FILELOCK_H_
