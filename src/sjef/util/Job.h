#ifndef SJEF_JOB_SERVER_H
#define SJEF_JOB_SERVER_H
#include "../sjef-backend.h"
#include "../sjef.h"
#include "Logger.h"
#include "Shell.h"
#include <atomic>
#include <future>

namespace sjef::util {
class Shell; ///< @private
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
  mutable bool m_remote_cache_directory_verified = false;
  std::future<void> m_poll_task;
  mutable std::shared_ptr<Shell> m_backend_command_server;
  int m_job_number=0;
  mutable Logger m_trace;
  // atomic, not guarded by m_kill_mutex like the rest of the members below: poll_job() also reads
  // this under m_closing_mutex (its "m_closing or status==completed or m_killed" check), a
  // different mutex than kill()'s m_kill_mutex-guarded write, so mutex protection alone -- correct
  // as it looked at either call site individually -- didn't actually synchronize the two.
  std::atomic<bool> m_killed = false;
  bool m_closing = false; //!< set to signal that polling should be stopped
  std::mutex m_closing_mutex;
  //! Guards this job's own m_backend_command_server/m_job_number against a concurrent kill()
  //! racing run()'s (re)creation of the backend server, and against poll_job()'s status reads
  //! racing kill()'s status write. Deliberately a member, not a process-wide global: this job's
  //! launch/poll/kill must never serialize against an unrelated Job's, since each Job's
  //! push/submit/pull is itself blocking network I/O that can take seconds against a real remote
  //! host, and a shared lock would turn concurrent parallel launches into a queue where one slow
  //! or stuck remote call freezes every other job's launch and status polling too.
  std::mutex m_kill_mutex;
  status m_initial_status;
  //! Set once get_status() has genuinely (not by default/fallback) observed this job as running or
  //! waiting. Used to distinguish a trustworthy "it was running and has now disappeared, so it must
  //! have finished" inference from a mere guess made before the job was ever confirmed to exist.
  bool m_seen_running = false;
  //! Counts consecutive poll cycles where the job has never been seen running and its status is
  //! unknown -- e.g. because it failed and exited before any status check could catch it. Bounds how
  //! long poll_job() will wait for confirmation before concluding the job must be finished, so that a
  //! fast-failing job (bad command line, immediate crash, ...) is reported rather than polled forever.
  int m_unconfirmed_polls = 0;
  std::tuple<bool, std::string, std::string> push_rundir(int verbosity = 0);
  std::tuple<bool, std::string, std::string> pull_rundir(int verbosity = 0);
  std::string m_remote_rsync;
  std::string m_remote_rsync_version;
  std::string m_local_rsync_version;
  const bool localhost() const;
  void poll_job(int verbosity = 0);
  void set_status(status stat);

public:
  class sync_error : public sjef::util::Shell::runtime_error {
  public:
    explicit sync_error(const char* msg) : sjef::util::Shell::runtime_error(msg){}
  };

//  private:
//    std::string m_msg;
//  };
//using sync_error = sjef::util::Shell::runtime_error;
//  using other_error = sjef::util::Shell::runtime_error;

  void ensure_remote_cache_directory() const;
};
} // namespace sjef::util

#endif // SJEF_JOB_SERVER_H
