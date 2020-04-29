#ifndef SJEF_LIB_FILELOCK_H_
#define SJEF_LIB_FILELOCK_H_
#include <memory>
#include <shared_mutex>

namespace sjef {

/*!
 * @brief A thread-safe class for managing exclusive and non-exclusive access to a file
 * Implementation within a process is via a mutex, and between processes via boost::interprocess:file_lock.
 * On creation of a class instance, execution will wait until access to the file can be obtained,
 * and the access obtained will influence other subsequent access requests until the object is destroyed.
 */
class FileLock {
 public:
  /*!
   * @brief Assert and obtain access to a file for the lifetime of a class instance.
   * @param path The file to be locked
   * @param exclusive Whether to have sole access (as needed if the file is to be written), or shared access (for example, just for reading the file). Note that shared access between threads in the same process is not implemented - all such accesses are mutually exclusive.
   * @param erase_if_created If path did not exist on construction, whether to destroy it on destruction
   */
  explicit FileLock(const std::string& path, bool exclusive = true, bool erase_if_created = true);
  ~FileLock();
  ///> @private
  class Unique_FileLock;
 private:
  using mutex_t = std::shared_timed_mutex;
  bool m_exclusive;
  std::shared_ptr<Unique_FileLock> m_unique;
  std::unique_ptr<std::lock_guard<mutex_t> > m_lock_guard;
  std::unique_ptr<std::shared_lock<mutex_t> > m_shared_lock;
};

}
#endif //SJEF_LIB_FILELOCK_H_
