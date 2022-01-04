#ifndef SJEF_LIB_LOCK_H_
#define SJEF_LIB_LOCK_H_
#include <filesystem>
#include <string>
namespace fs = std::filesystem;

namespace sjef {

/*!
 * @brief A thread-safe class for a lock based on a global mutex in the file system.
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
