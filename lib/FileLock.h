#ifndef SJEF_LIB_FILELOCK_H_
#define SJEF_LIB_FILELOCK_H_
#include <memory>

/*!
 * @brief A thread-safe class for managing exclusive and non-exclusive access to a file
 * Implementation within a process is via std::mutex, and between processes via boost::interprocess:file_lock
 * On creation of a class instance, execution will wait until access to the file can be obtained,
 * and the access obtained will influence other subsequent access requests until the object is destroyed.
 */
namespace sjef {

class Unique_FileLock;
class FileLock {
 public:
  /*!
   * @brief Assert and obtain access to a file for the lifetime of a class instance.
   * @param path The file to be locked
   * @param exclusive Whether to have sole access (as needed if the file is to be written), or shared access (for example, just for reading the file)
   * @param erase_if_created If path did not exist on construction, whether to destroy it on destruction
   */
  explicit FileLock(const std::string& path, bool exclusive = true, bool erase_if_created = true);
  ~FileLock();
 protected:
  bool m_exclusive;
  std::shared_ptr<Unique_FileLock> m_unique;
};

}
#endif //SJEF_LIB_FILELOCK_H_
