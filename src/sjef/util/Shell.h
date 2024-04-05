#ifndef SJEF_LIB_UTIL_SHELL_H_
#define SJEF_LIB_UTIL_SHELL_H_
#include "Logger.h"
#include <boost/process/child.hpp>
#include <boost/process/io.hpp>
namespace bp = boost::process;

namespace sjef::util {

/*!
 * @brief Execute an external command locally or on a remote machine via ssh.
 *
 * The command can be anything that can be understood by standard shell.
 * Execution can either by synchronous (output and error streams available via out() and err())
 * or asynchronous (output and error streams can be directed to a file).
 * Standard input is not supported.
 */
class Shell {

public:
  /*!
   * @brief Construct a shell instance
   * @param host hostname passed to ssh. If "localhost", ssh will not be used.
   * @param shell Shell command. If empty, then operator() will run commands directly and synchronously rather than in a shell.
   */
  Shell(std::string host, std::string shell = "bash");
  Shell() : Shell("localhost") {}
  std::string operator()(const std::string& command, bool wait = true, const std::string& directory = ".",
                         int verbosity = 0, const std::string& out = "/dev/null",
                         const std::string& err = "/dev/null") const;
  const std::string& out() const { return m_last_out; }
  const std::string& err() const { return m_last_err; }
  int job_number() const { return m_job_number; }
  void wait(int min_wait_milliseconds = 1, int max_wait_milliseconds = 1000) const;
  bool running() const;
  /*!
   * @brief Whether local asynchronous commands are supported
   * @return
   */
  static bool local_asynchronous_supported();

  class runtime_error : public std::exception {
  public:
    explicit runtime_error(const char* message) : m_msg(message) {}
    runtime_error(runtime_error const&) noexcept = default;
    runtime_error& operator=(runtime_error const&) noexcept = default;
    ~runtime_error() override = default;
    [[nodiscard]] const char* what() const noexcept override { return m_msg.c_str(); }

  private:
    std::string m_msg;
  };

private:
  const std::string m_host;
  const std::string m_shell;
  mutable bp::opstream m_in;
  mutable std::unique_ptr<bp::ipstream> m_out;
  mutable std::unique_ptr<bp::ipstream> m_err;
  mutable std::string m_last_out;
  mutable std::string m_last_err;
  mutable Logger m_trace;
  mutable Logger m_warn;
  mutable std::mutex m_run_mutex;
  mutable int m_job_number = 0;
  mutable bp::child m_process;

  bool localhost() const { return (m_host.empty() || m_host == "localhost"); }
};

std::vector<std::string> tokenise(const std::string& command);

} // namespace sjef::util

#endif // SJEF_LIB_UTIL_SHELL_H_
