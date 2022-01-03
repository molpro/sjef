#ifndef SJEF_LIB_LOCK_H_
#define SJEF_LIB_LOCK_H_
#include <string>

namespace sjef {

/*!
 * @brief A thread-safe class for a lock based on a global mutex in the file system.
 * The mutex is a file that should not already exist.
 * On creation of a class instance, execution will wait until access to the file can be obtained,
 * and the access obtained will block other subsequent access requests until the object is destroyed.
 */
class Lock {
public:
  /*!
   * @brief Assert and obtain access to a file for the lifetime of a class instance.
   * @param path The file to be locked. It should not be an existing file.
   */
  explicit Lock(const std::string& path);
  virtual ~Lock();

private:
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;

#ifdef _WIN32
  using handle_t = void*;
#else
  using handle_t = int;
#endif
  handle_t m_lock;
  std::mutex m_mutex;
  std::string m_path;
};

} // namespace sjef
#endif // SJEF_LIB_LOCK_H_
