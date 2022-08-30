#ifndef SJEF_LIB_UTIL_COMMAND_H_
#define SJEF_LIB_UTIL_COMMAND_H_
#include "Logger.h"
#include <boost/process/args.hpp>
#include <boost/process/child.hpp>
#include <boost/process/io.hpp>
#include <boost/process/search_path.hpp>
#include <boost/process/spawn.hpp>
namespace bp = boost::process;
namespace fs = std::filesystem;

namespace sjef::util {

/*!
 * @brief Execute an external command locally or on a remote machine via ssh.
 *
 * Execution can either by synchronous or asynchronous, but in that case the output and error streams are not available.
 * Standard input is not supported.
 */
class Command {

public:
  Command(std::string host, std::string shell = "/bin/sh");
  Command() : Command("localhost") {}
  std::string operator()(const std::string& command, bool wait = true, std::string directory = ".",
                         int verbosity = 0) const;
  const std::string& out() const { return m_last_out; }
  const std::string& err() const { return m_last_err; }
  int job_number() const { return m_job_number; }
  void wait() const;
  const bp::child& process() const { return m_process; }
  bool running() const;

protected:
  const std::string m_host;
  mutable bp::opstream m_in;
  mutable std::unique_ptr<bp::ipstream> m_out;
  mutable std::unique_ptr<bp::ipstream> m_err;
  mutable std::string m_last_out;
  mutable std::string m_last_err;
  mutable Logger m_trace;
  mutable Logger m_warn;
  mutable std::mutex m_run_mutex;
  mutable int m_job_number=0;
  mutable bp::child m_process;

protected:
  const bool localhost() const { return (m_host.empty() || m_host == "localhost" || m_host == "127.0.0.1"); }
};

} // namespace sjef::util

#endif // SJEF_LIB_UTIL_COMMAND_H_
