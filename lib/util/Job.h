#ifndef SJEF_JOB_SERVER_H
#define SJEF_JOB_SERVER_H
#include "../sjef.h"
#include "Logger.h"
#include "sjef-backend.h"
#include <future>

namespace sjef::util {
class Command; ///< @private
/*!
 * Class instance manages polling and service of local and remote jobs
 *
 * For local jobs
 * - regularly poll for status
 * For remote jobs
 * - set up ssh server
 * - set up remote command server
 * - if new_job, at construction, send the run directory to the remote machine
 * - regularly poll for status
 * - regularly rsync-pull the run directory from remote cache
 * - delete the remote cache, after a final pull, if the job status is finished or killed
 *
 * If the polling discovers that the job has finished, it shuts itself down.
 *
 * The property "status" of project is updated
 */
class Job {
public:
  /*!
   * @brief Initiate job server
   * @param project Although marked const, server will update property status
   * @param new_job If true, and a remote backend, initialise remote run directory
   */
  Job(const Project& project);
  Job() = delete;
  Job(const Job&) = delete;
  Job(Job&&) = delete;
  ~Job();
  /*!
   * @brief Run a command on the backend. If it is the job launch command, collect the job number
   * @param command Command and space-separated arguments. Space-containing arguments can be protected using single
   * quotation marks
   * @param verbosity
   * @param wait Whether to wait for the result, or launch asynchronously
   * @return
   */
  std::string run(const std::string& command, int verbosity = 0, bool wait = true);
  int job_number() const { return m_job_number;}
  void kill(int verbosity = 0);
  status get_status(int verbosity = 0);

protected:
  const Project& m_project;
  const sjef::Backend& m_backend;
  const std::string
      m_remote_cache_directory; //!< The path on the remote backend that will be synchronized with run directory
  std::future<void> m_poll_task;
  mutable std::shared_ptr<Command> m_backend_command_server;
  int m_job_number=0;
  mutable Logger m_trace;
  bool m_killed = false;
  bool m_closing = false; //!< set to signal that polling should be stopped
  std::mutex m_closing_mutex;
  status m_initial_status;
  bool push_rundir(int verbosity = 0);
  bool pull_rundir(int verbosity = 0);
  const bool localhost() const;
  void poll_job(int verbosity = 0);
  void set_status(status stat);
};
} // namespace sjef::util

#endif // SJEF_JOB_SERVER_H
