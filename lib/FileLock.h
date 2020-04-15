#ifndef SJEF_LIB_FILELOCK_H_
#define SJEF_LIB_FILELOCK_H_
#include <memory>
#include <string>

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
  explicit FileLock(const std::string& path, bool exclusive = true);
  ~FileLock();
 protected:
  bool m_exclusive;
  std::shared_ptr<Unique_FileLock> m_unique;
};

}
#endif //SJEF_LIB_FILELOCK_H_
