#ifndef SJEF_LIB_UTIL_SHELL_H_
#define SJEF_LIB_UTIL_SHELL_H_
#define BOOST_ALL_NO_LIB
#define BOOST_PROCESS_USE_STD_FS
#include "Logger.h"
#if __has_include(<boost/process/v1/child.hpp>)
#define BOOST_PROCESS_VERSION 1
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/io.hpp>
#else
#include <boost/process/child.hpp>
#include <boost/process/io.hpp>
#endif
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
  Shell(std::string host, std::string shell = "/bin/bash");
  Shell() : Shell("localhost") {}
  /*!
   *@brief Execute a command. For a remote host, the command is sent to the remote shell already set up in the class
   * constructor. For a local host, a new process is created.
   *
   * @param command Any valid input for /bin/sh
   * @param wait If true, block until the process has been completed. The standard output and error streams are
   * subsequently available through the out() and err() class functions. If false, only for local jobs, return
   * immediately after launching the process. Standard output and error are written to specified files; the process id
   * is available through job_number()
   * @param directory Working directory.
   * @param verbosity
   * @param out When wait is true for a local job, this will be used as a file name to receive standard output.
   * @param err When wait is true for a local job, this will be used as a file name to receive standard error.
   * @return The command standard output, except for asynchronous local jobs, which return an empty string.
   */
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
  mutable std::future<void> m_stdout_future;
  mutable bool m_stdout_future_running = false;

protected:
  void run_local_sync(const std::string& command, const std::string& directory, int verbosity, const std::string& out,
                      const std::string& err) const;
  void capture_job_number_from_error(const std::string& command) const;
  void run_local_async(const std::string& command, const std::string& directory, int verbosity, const std::string& out,
                       const std::string& err) const;
  void run_remote(std::string command, const std::string& directory, bool wait, int verbosity, const std::string& out,
                  const std::string& err) const;
  void capture_out() const;
  bool localhost() const { return (m_host.empty() || m_host == "localhost"); }
};

std::vector<std::string> tokenise(const std::string& command);

} // namespace sjef::util

#endif // SJEF_LIB_UTIL_SHELL_H_
