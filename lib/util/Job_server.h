#ifndef SJEF_JOB_SERVER_H
#define SJEF_JOB_SERVER_H
#include "../sjef.h"
#include "sjef-backend.h"
#include <future>



namespace sjef::util {
class Command;     ///< @private
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
class Job_server {
public:
  /*!
   * @brief Initiate job server
   * @param project Although marked const, server will update property status
   * @param new_job If true, and a remote backend, initialise remote run directory
   */
  Job_server(const Project& project, bool new_job=true);
  Job_server() = delete;
  Job_server(const Job_server&) = delete;
  Job_server(Job_server&&) = delete;
  ~Job_server();
  std::string run(const std::string& command, int verbosity = 0, bool wait = true) ;
protected:
  const Project& m_project;
  const sjef::Backend& m_backend;
  const std::string m_remote_cache_directory;
  std::future<void> m_poll_task;
  sjef::status m_status;
  mutable std::shared_ptr<Command> m_remote_server;
  int m_job_number;
  /*!
   *
   * @return The path on the remote backend that will be synchronized with run directory
   */
  bool push_rundir( int verbosity=0);
  bool pull_rundir(int verbosity=0);
  const bool localhost() const;
  void end_job();
  void poll_job();
};
} // namespace sjef::util

#endif // SJEF_JOB_SERVER_H
