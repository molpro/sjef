#ifndef SJEF_JOB_SERVER_H
#define SJEF_JOB_SERVER_H
#include "../sjef.h"
#include <future>

namespace sjef::util {
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
  Job_server(const Project& project, bool new_job=true);
  Job_server() = delete;
  Job_server(const Job_server&) = delete;
  Job_server(Job_server&&) = delete;
  ~Job_server();
  std::future<void> m_poll_task;
  const Project& m_project;
  sjef::status m_status;
  /*!
   *
   * @return The path on the remote backend that will be synchronized with run directory
   */
  std::string remote_cache_directory() const;
  bool push_rundir();
  bool pull_rundir();
  void end_job();
  void poll_job();
};
} // namespace sjef::util

#endif // SJEF_JOB_SERVER_H
