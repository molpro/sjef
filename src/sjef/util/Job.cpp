#include "Job.h"
#include "Shell.h"
#include "util.h"
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <signal.h>
#include <sstream>
#include <stdlib.h>
#include <string>
#if defined(WIN32) || defined(__WIN64)
#if defined(_M_AMD64) || defined(_M_X64)
#define _AMD64_
#elif defined(_M_IX86)
#define _X86_
#endif
#include <handleapi.h>
#include <processthreadsapi.h>
#endif
namespace fs = std::filesystem;

namespace sjef::util {

namespace {
// Bounds how many Job constructors may simultaneously be inside the "new session handshake" (which
// rsync / rsync --version / remote cache setup, over a freshly-spawned ssh session) for the same
// remote host. Launching many jobs in parallel against a real remote login node was observed to spawn
// that many brand-new ssh sessions at once, each immediately sending its handshake commands -- and a
// burst like that can make even a login node that is perfectly healthy for one connection at a time
// become unresponsive (well within OpenSSH's own default connection-flood defenses, e.g. MaxStartups),
// producing exactly the "no response" timeouts this throttle exists to avoid triggering in the first
// place. It only gates the handshake, not the job's subsequent run/poll lifecycle, so once a session is
// past this, it proceeds fully independently and concurrently with every other job as before.
class HostConnectionThrottle {
public:
  explicit HostConnectionThrottle(int max_concurrent) : m_max(max_concurrent) {}
  void acquire() {
    std::unique_lock l(m_mutex);
    m_cv.wait(l, [&] { return m_count < m_max; });
    ++m_count;
  }
  void release() {
    std::lock_guard l(m_mutex);
    --m_count;
    m_cv.notify_one();
  }

private:
  std::mutex m_mutex;
  std::condition_variable m_cv;
  int m_count = 0;
  const int m_max;
};

class ThrottleGuard {
public:
  explicit ThrottleGuard(HostConnectionThrottle& throttle) : m_throttle(throttle) { m_throttle.acquire(); }
  ~ThrottleGuard() { m_throttle.release(); }
  ThrottleGuard(const ThrottleGuard&) = delete;

private:
  HostConnectionThrottle& m_throttle;
};

HostConnectionThrottle& throttle_for_host(const std::string& host, int max_concurrent,
                                          std::map<std::string, std::unique_ptr<HostConnectionThrottle>>& registry,
                                          std::mutex& registry_mutex) {
  std::lock_guard l(registry_mutex);
  auto [it, inserted] = registry.try_emplace(host, nullptr);
  if (inserted)
    it->second = std::make_unique<HostConnectionThrottle>(max_concurrent);
  return *it->second;
}

HostConnectionThrottle& handshake_throttle_for_host(const std::string& host) {
  // How many new-session handshakes to a single host may be in flight at once. Deliberately well under
  // OpenSSH's own conservative defaults for simultaneous unauthenticated/new connections (e.g.
  // MaxStartups 10:30:100), so this throttle -- not the remote sshd's own flood defenses -- is what
  // determines how launches against a busy or fragile login node are paced.
  constexpr int max_concurrent_handshakes_per_host = 3;
  static std::mutex registry_mutex;
  static std::map<std::string, std::unique_ptr<HostConnectionThrottle>> registry;
  return throttle_for_host(host, max_concurrent_handshakes_per_host, registry, registry_mutex);
}

HostConnectionThrottle& rsync_throttle_for_host(const std::string& host) {
  // How many rsync invocations (push_rundir/pull_rundir) against a single host may be in flight at
  // once. Unlike the handshake throttle above, this bounds a genuinely different, and far more
  // frequently hit, server-side resource: every rsync call's --rsh multiplexes onto the *one* shared
  // ssh ControlMaster connection per host (see system_specific_ssh_options()), so all of them together
  // -- across every concurrently-running job for this host, for the job's entire lifetime, not just its
  // launch -- compete for that one connection's session slots. A real login node was observed refusing
  // sessions outright ("mux_client_request_session: session request failed: Session open refused by
  // peer") once enough concurrent jobs' launches and poll cycles overlapped, which is exactly OpenSSH's
  // own MaxSessions (default 10) being exceeded. This keeps concurrent usage well under that.
  constexpr int max_concurrent_rsyncs_per_host = 4;
  static std::mutex registry_mutex;
  static std::map<std::string, std::unique_ptr<HostConnectionThrottle>> registry;
  return throttle_for_host(host, max_concurrent_rsyncs_per_host, registry, registry_mutex);
}
} // namespace

///> @private
const bool Job::localhost() const { return (m_backend.host.empty() || m_backend.host == "localhost"); }
sjef::util::Job::Job(const sjef::Project& project)
    : m_project(project), m_backend(m_project.backends().at(m_project.property_get("backend"))),
      m_remote_cache_directory(m_backend.cache + "/" +
                               std::to_string(std::hash<std::string>{}(m_project.filename("", "", 0).string()))),
      m_backend_command_server(new Shell(m_backend.host)),
      m_job_number(std::stoi("0" + m_project.property_get("jobnumber"))),
      m_initial_status(static_cast<sjef::status>(std::stoi("0" + m_project.property_get("_status")))) {
  //  std::cout << "Job constructor, m_job_number=" << m_job_number << std::endl;
  if (!localhost()) {
    ThrottleGuard throttle(handshake_throttle_for_host(m_backend.host));
    m_remote_rsync =
        (*m_backend_command_server)("PATH=$HOME/bin:/usr/local/bin:/opt/homebrew/bin:/opt/bin:$PATH which rsync");
    if (m_remote_rsync.empty())
      m_remote_rsync = "rsync";
    m_remote_rsync_version = (*m_backend_command_server)(m_remote_rsync + " --version|head -1");
    m_remote_rsync_version =
        std::regex_replace(m_remote_rsync_version, std::regex{R"( *rsync *version *([0-9.]*) .*)"}, "$1");
    if (std::stoi(m_remote_rsync_version.substr(0, 1)) < 3)
      throw std::runtime_error("rsync on remote " + m_backend.host + " (" + m_remote_rsync + ") is version " +
                               m_remote_rsync_version + ", which is too old");
    //        std::cout << "remote rsync: " << m_remote_rsync << std::endl;
    // don't allow remote cache directory name that could lead to shell expansion
    //    std::cout << m_remote_cache_directory<<std::endl;
    if (not std::regex_search(m_remote_cache_directory, std::regex("^[-A-Za-zÀ-ú0-9_=\\./]*$")))
      throw std::runtime_error("Invalid remote cache directory " + m_remote_cache_directory);
    ensure_remote_cache_directory(); // to ensure cache is set up before any polling
  }
  m_poll_task = std::async(std::launch::async, [this]() { this->poll_job(); });
  //  std::cout << "Job constructor has launched poll task" << std::endl;
}

void setup_rsync_path() {
#ifdef __APPLE__
  // don't want to end up with /usr/bin/rsync which (2023) is too old
  auto oldpath = std::string{std::getenv("PATH")};
  auto needed_path = std::string{"/opt/homebrew/bin:/usr/local/bin:/opt/local/bin:"};
  if (oldpath.substr(0, needed_path.length()) != needed_path)
    setenv("PATH", (needed_path + oldpath).c_str(), 1);
#endif
#ifdef WIN32
  auto conda_prefix = getenv("CONDA_PREFIX");
  //  std::cout << "getenv(PATH) " << getenv("PATH") << std::endl;
  //  std::cout << "conda_prefix " << conda_prefix << std::endl;
  if (conda_prefix != NULL) {
    _putenv_s("PATH", (std::string(conda_prefix) + "\\rsync\\bin;" + getenv("PATH")).c_str());
  }
//  std::cout << "getenv(PATH) " << getenv("PATH") << std::endl;
#endif
}

static std::string system_specific_ssh_options() {
  std::string result;
#ifdef WIN32
  // ControlPath socket special files do not work on Windows
  auto user_profile = getenv("USERPROFILE");
  result = std::string{"--rsh='ssh -o UserKnownHostsFile="} + user_profile + "\\.ssh\\known_hosts";
  //  result += " -F $HOMEPATH/.ssh/config";
  result += "'";
//                     " -i $HOMEPATH/.ssh/id_rsa -i $HOMEPATH/.ssh/id_dsa -i $HOMEPATH/.ssh/id_ed25519 -i
//                     $HOMEPATH/.ssh/id_ed25519_sk -i $HOMEPATH/.ssh/id_ecdsa -i $HOMEPATH/.ssh/id_ecdsa_sk'";
#else
  result = "--rsh 'ssh -o ControlPath=~/.ssh/sjef-control-%h-%p-%r -o ControlMaster=auto -o ControlPersist=300'";
#endif
  return result;
}

std::tuple<bool, std::string, std::string> sjef::util::Job::push_rundir(int verbosity) {
  if (localhost())
    return {true, "", ""};
  setup_rsync_path();
  std::string command = "rsync --archive --copy-links --timeout=5 -s -v";
  command += " --rsync-path=" + m_remote_rsync;
  command += " --exclude=Info.plist --exclude=.Info.plist.writing_object";
  command += " " + system_specific_ssh_options();
#ifdef WIN32
  // rsync interprets c:\a\b as a remote filename so windows filenames cause it to fail
  //   in cwrsync the C: drive is mounted as /cygdrive/c/
  //   convert c:\A\B -> c/a/b and pre-prepend /cygdrive/
  std::string fwin = m_project.filename("", "", 0).string();
  std::replace(fwin.begin(), fwin.end(), '\\', '/');
  std::replace(fwin.begin(), fwin.end(), ':', '/');
  command += " '/cygdrive/" + fwin + "/'";
#else
  command += " '" + m_project.filename("", "", 0).string() + "/'";
#endif
  command += " " + m_backend.host + ":'" + m_remote_cache_directory + "'";
  if (verbosity > 0)
    command += " -v";
  m_project.m_trace(2 - verbosity) << "Push rsync: " << command << std::endl;
  auto start_time = std::chrono::steady_clock::now();
  ensure_remote_cache_directory();
  ThrottleGuard rsync_throttle(rsync_throttle_for_host(m_backend.host));
  const Shell& shell = Shell("localhost", "");
  std::string rsync_out;
  try {
    rsync_out = shell(command, true, ".", verbosity);
  } catch (const sjef::util::Shell::runtime_error& e) {
    std::cout << "caught exception in Job::push_rundir(): " << e.what() << std::endl;
    throw sync_error(e.what());
  }
  if (verbosity > 1)
    m_project.m_trace(3 - verbosity)
        << "time for push_rundir() rsync "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << "ms" << std::endl
        << "Output from rsync\n:" << rsync_out << std::endl
        << "Error stream from rsync\n:" << shell.err() << std::endl;
  return {shell.err().find("rsync error:") == std::string::npos, shell.out(),
          shell.err()}; // TODO: implement more robust error checking
}

void sjef::util::Job::ensure_remote_cache_directory() const {
  if (m_remote_cache_directory_verified)
    return;
  (*m_backend_command_server)("mkdir -p '" + m_remote_cache_directory + "'");
  auto test_remote_cache_directory = (*m_backend_command_server)("ls -d '" + m_remote_cache_directory + "'");
  if (test_remote_cache_directory != m_remote_cache_directory)
    throw std::runtime_error("Error in making remote cache directory " + m_remote_cache_directory + " on remote host " +
                             m_backend.host);
  m_remote_cache_directory_verified = true;
}

std::tuple<bool, std::string, std::string> sjef::util::Job::pull_rundir(int verbosity) {
  m_trace(3 - verbosity) << "pull_rundir " << verbosity << std::endl;
  if (localhost())
    return {true, "", ""};
  setup_rsync_path();
  std::string command = "rsync --archive --copy-links --timeout=5 -s -v";
  command += " --rsync-path=" + m_remote_rsync;
  command += " --exclude=backup --exclude=*.d";
  command += " --exclude=Info.plist --exclude=.Info.plist.writing_object";
  command += " " + system_specific_ssh_options();
  command += " " + m_backend.host + ":'" + m_remote_cache_directory + "/'";
#ifdef WIN32
  std::string fwin = m_project.filename("", "", 0).string();
  std::replace(fwin.begin(), fwin.end(), '\\', '/');
  std::replace(fwin.begin(), fwin.end(), ':', '/');
  command += " '/cygdrive/" + fwin + "'";
#else
  command += " '" + m_project.filename("", "", 0).string() + "'";
#endif
  if (verbosity > 0)
    command += " -v";
  m_project.m_trace(2 - verbosity) << "Pull rsync: " << command << std::endl;
  auto start_time = std::chrono::steady_clock::now();
  ThrottleGuard rsync_throttle(rsync_throttle_for_host(m_backend.host));
  const Shell& shell = Shell("localhost", "");
  std::string rsync_out;
  try {
    rsync_out = shell(command, true, ".", verbosity);
  } catch (const sjef::util::Shell::runtime_error& e) {
    std::cout << "caught exception in Job::pull_rundir()" << e.what() << std::endl;
    throw std::runtime_error(e.what());
    throw static_cast<std::exception>(e);
  }
  if (verbosity > 1)
    m_project.m_trace(3 - verbosity)
        << "time for pull_rundir() rsync "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << "ms" << std::endl
        << "Output from rsync\n:" << rsync_out << std::endl;
  return {shell.err().find("rsync error:") == std::string::npos, shell.out(),
          shell.err()}; // TODO: implement more robust error checking
}

sjef::util::Job::~Job() {
  //  if (m_status==sjef::status::completed || m_status==sjef::status::killed) {
  //    end_job();
  //  }
  {
    std::lock_guard lock(m_closing_mutex);
    m_closing = true;
  }
  m_poll_task.wait();
}

inline std::string slurp(const std::filesystem::path& path) {
  std::ostringstream buf;
  std::ifstream input(path);
  buf << input.rdbuf();
  return buf.str();
}

std::string Job::run(const std::string& command, int verbosity, bool wait) {
  // Guarded the same way ~Job() guards its own m_closing=true below: poll_job() reads m_closing
  // under m_closing_mutex (see the lock_guard around its "m_closing or status==completed or
  // m_killed" check), so writing it here without the same lock is a data race even though the two
  // sides never observe an actually-wrong value in practice. Released before m_poll_task.wait()
  // (which blocks until poll_job() returns) to avoid deadlocking against poll_job() itself trying
  // to acquire this same mutex.
  {
    std::lock_guard lock(m_closing_mutex);
    m_closing = true;
  }
  m_poll_task.wait();
  {
    std::lock_guard lock(m_closing_mutex);
    m_closing = false;
  }
  m_backend_command_server.reset(new Shell(m_backend.host));
  std::string run_output;
  {
    auto l = std::lock_guard(m_kill_mutex);
    //  const auto& substr = std::regex_replace(command, std::regex{"'"}, "").substr(0, m_backend.run_command.size());
    m_trace(4 - verbosity) << "Job::run() command=" << command << std::endl;
    //  m_trace(4 - verbosity) << "Job::run substr=" << substr << " m_backend.run_command=" << m_backend.run_command
    //                         << std::endl;
    //  auto is_run_command = substr == m_backend.run_command;
    m_initial_status = waiting;
    m_job_number = 0; // pauses status polling
                      //    std::cout << "Job::run() set initial status to waiting, and pause polling"<<std::endl;
    set_status(waiting);
    auto backend_submits_batch = m_backend.run_jobnumber != "([0-9]+)";
    if (!localhost()) {
      const auto& push_rundir_result = push_rundir(verbosity);
      if (!std::get<0>(push_rundir_result))
        throw std::runtime_error("Push of data to remote cache has failed\nOutput:\n" +
                                 std::get<1>(push_rundir_result) + "\nError:" + std::get<2>(push_rundir_result));
    }
    push_rundir(verbosity); // do it again to allow time to settle
    m_trace(4 - verbosity) << "Job::run() gives directory " << m_project.filename("", "", 0) << std::endl;
    m_trace(4 - verbosity) << "before submit, m_backend_command_server? " << (m_backend_command_server == nullptr)
                           << std::endl;
    (*m_backend_command_server)(command, wait or backend_submits_batch,
                                localhost() ? m_project.filename("", "", 0).string() : m_remote_cache_directory,
                                verbosity, m_project.filename("stdout", "", 0).filename().string(),
                                m_project.filename("stderr", "", 0).filename().string());
    pull_rundir();
    run_output = slurp(m_project.filename("stdout", "", 0)) + "\n" + slurp(m_project.filename("stderr", "", 0));
    if (backend_submits_batch) {
      std::smatch match;
      if (std::regex_search(run_output, match, std::regex{m_backend.run_jobnumber})) {
        //        m_trace(5 - verbosity) << "... a match was found: " << match[1] << std::endl;
        m_job_number = std::stoi(match[1]);
        m_trace(4 - verbosity) << "Job::run backend_submits_batch m_job_number=" << m_job_number << std::endl;
      }
    } else {
      m_trace(4 - verbosity) << "before job_number(), m_backend_command_server? "
                             << (m_backend_command_server == nullptr) << std::endl;
      m_job_number = m_backend_command_server->job_number();
      m_trace(4 - verbosity) << "Job::run is_run_command m_job_number=" << m_job_number << std::endl;
    }
    m_poll_task = std::async(std::launch::async, [this]() { this->poll_job(); });
  }
  // give the poll task time to get a first result
  //  std::cout << "status immediately after launching poll_task "<<m_project.status_message()<<" id
  //  "<<std::this_thread::get_id()<<std::endl;
  for (int i = 0; i < 20 && m_project.status() == sjef::status::waiting; ++i) {
    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(50ms);
    //  std::cout << "status after waiting "<<m_project.status_message()<<std::endl;
  }
  m_trace(2 - verbosity) << "Job::run() returns " << run_output << std::endl;
  return run_output;
}

void Job::set_status(status stat) {
  const_cast<Project&>(m_project).property_set("_status", std::to_string(static_cast<int>(stat)));
}

status Job::get_status(int verbosity) {
  if (m_job_number == 0)
    return unknown;
  std::string status_string;
  sjef::status result = unknown;
  try {
    status_string = (*m_backend_command_server)(m_backend.status_command + " " + std::to_string(m_job_number), true,
                                                ".", verbosity);
    //  std::cout << "status_string:\n" << status_string << std::endl;
    std::stringstream ss(status_string);
    for (std::string line; std::getline(ss, line);) {
      if ((" " + line).find(" " + std::to_string(m_job_number) + " ") != std::string::npos) {
        std::smatch match;
        m_trace(4 - verbosity) << "line" << line << std::endl;
        m_trace(4 - verbosity) << "status_running " << m_backend.status_running << std::endl;
        m_trace(4 - verbosity) << "status_waiting " << m_backend.status_waiting << std::endl;
        if (std::regex_search(line, match, std::regex{m_backend.status_waiting})) {
          result = waiting;
        }
        if (std::regex_search(line, match, std::regex{m_backend.status_running})) {
          result = running;
        }
      }
    }
  } catch (...) {
  }
  //  std::cout << "running pattern: " << m_backend.status_running << std::endl;
  //  std::cout << Command()("ps -p "+std::to_string(m_job_number)) << std::endl;
  //
  //  std::cout << Command()("lsof -p "+std::to_string(m_job_number)) << std::endl;
  //  std::cout << Command()("ls -laR  /tmp/sjef/cmake-build-release-marat/test/H2_local.molpro/run") << std::endl;
  //  std::cout << "Job::status() returns " << result << std::endl;
  return result;
}
void Job::kill(int verbosity) {
  m_trace(4 - verbosity) << "Job::kill()" << std::endl;
  if (localhost()) {
    // catch failure to kill local jobs
    auto pid = m_project.local_pid_from_output();
    if (pid > 0) {
#if !defined(WIN32) && !defined(__WIN64)
      ::kill(pid, SIGTERM);
#else
      HANDLE handle = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
      if (NULL != handle) {
        TerminateProcess(handle, 0);
        CloseHandle(handle);
      }
#endif
      // wait a second for main script to cleanup and exit before trying to kill
      using namespace std::literals::chrono_literals;
      std::this_thread::sleep_for(1000ms);
    }
  }
  {
    auto l = std::lock_guard(m_kill_mutex);
    //    std::cout << "Job::kill() gets mutex"<<std::endl;
    if (m_backend_command_server != nullptr) {
      auto status_string = (*m_backend_command_server)(m_backend.kill_command + " " + std::to_string(m_job_number),
                                                       true, ".", verbosity);
      //    std::cout << "Job::kill() finished killing"<<std::endl;
    }
    set_status(killed);
    //    std::cout << "Job::kill() finished set_status()"<<std::endl;
  }

  // Atomic (see the member declaration): poll_job() reads this both under m_kill_mutex and under
  // m_closing_mutex at different points, so no single mutex here would actually synchronize both.
  m_killed = true;
  //  std::cout << "Job::kill() set sentinel"<<std::endl;
}

template <class T>
std::set<T> vector_to_set(std::vector<T>&& v) {
  auto s = std::set<T>(std::make_move_iterator(v.begin()), std::make_move_iterator(v.end()));
  return s;
}
void Job::poll_job(int verbosity) {
  using Clock = std::chrono::high_resolution_clock;
  status status;
  auto start = Clock::now();
  auto stop = Clock::now();
  //    std::cout << "Polling starts" << std::endl;
  while (true) {
    //    std::cout << "m_killed " << m_killed << std::endl;
    {
      auto l = std::lock_guard(m_kill_mutex);
      //      std::cout << "active polling cycle starts"<<std::endl;
      //      if (m_killed)
      //        std::cout << "poll_job received kill sentinel" << std::endl;

      start = Clock::now();
      status = m_killed ? killed : get_status(verbosity);
      if (status == running or status == waiting)
        m_seen_running = true;
      if (status == unknown) {
        if (m_initial_status == killed) {
          status = killed;
        } else if (m_seen_running or m_initial_status == completed) {
          // Either this job has actually been observed alive during this Job instance's lifetime and
          // has now disappeared (the normal way to detect completion on a backend with no batch
          // scheduler to query), or this instance exists only to check on a job that a previous
          // invocation already recorded as completed.
          status = completed;
        } else if (++m_unconfirmed_polls >= 5) {
          // We've polled several times since submission without ever catching the job running, and
          // without any other trustworthy signal either. Most likely it failed and exited before any
          // status check could see it (e.g. the remote command line itself is rejected immediately --
          // this is not hypothetical, it happens whenever a backend's run_command has gone stale). Give
          // up waiting for confirmation rather than polling forever, so that whatever output/error the
          // job did produce still gets pulled back and reported. This does not reopen the premature-
          // cleanup bug below: that still requires m_seen_running, so at worst a fast-failing job's
          // remote cache directory is left behind instead of being cleaned up immediately.
          status = completed;
        } else {
          // Still within the grace period for a freshly submitted job that has not yet been confirmed
          // running. Report "waiting" rather than the raw "unknown" reading: callers (e.g. pysjef's
          // Project.wait(), which only loops on "running"/"waiting") treat "unknown" as a terminal,
          // stop-waiting state, so leaking a merely-transient "unknown" here would make them give up
          // before this grace period has had a chance to resolve to a real answer. Do NOT infer
          // completion straight from m_initial_status == waiting -- that value just means a submission
          // is in progress (see Job::run()), so a single "unknown" read here only means we haven't
          // caught up with the freshly submitted job yet.
          status = waiting;
        }
      } else {
        m_unconfirmed_polls = 0;
      }
      m_trace(4 - verbosity) << "got status " << status << std::endl;
      try {
        pull_rundir(verbosity);
      } catch (const std::exception& e) {
        // A failed sync here (e.g. the remote host/session was briefly unresponsive) must not be
        // allowed to propagate: this whole function runs on a detached std::async thread that
        // nothing ever calls get() on, so an uncaught exception here silently kills polling for
        // good, leaving status stuck at whatever it last was (typically "running"/"waiting") and
        // an external caller like pysjef's Project.wait() looping forever with no error at all.
        // Log and retry on the next cycle instead -- exactly as if this cycle's pull had simply
        // seen nothing new.
        m_trace(-verbosity) << "pull_rundir() failed during polling, will retry: " << e.what() << std::endl;
      }
      // Don't publish a terminal status (completed/killed) for a remote job yet: the block below
      // this loop still has to pull the run directory one or more times more, compare manifests
      // against the remote cache, and remove that remote cache -- all further I/O against this
      // same local project directory. Publishing "completed" here lets an external caller (e.g.
      // pysjef's Project.wait(), which only loops on "running"/"waiting") see the job as finished
      // and start touching or deleting the local project directory while that trailing I/O is
      // still in flight, racing a pull_rundir() rsync against a directory being removed out from
      // under it. Local jobs have no such follow-up I/O (see the localhost() guard below), so
      // publishing immediately for them is safe and keeps existing behaviour.
      bool about_to_finish = (status == completed or status == killed or m_killed) and !localhost();
      if (!about_to_finish)
        set_status(status);
      //    std::cout << "set status " << m_project.status_message() << std::endl;
      stop = Clock::now();
      {
        std::lock_guard lock(m_closing_mutex);
        if (m_closing or status == completed or m_killed) {
          using namespace std::literals::chrono_literals;
          std::this_thread::sleep_for(10ms);
          try {
            pull_rundir(verbosity);
          } catch (const std::exception& e) {
            // Same reasoning as above: don't let a failed final pull abort the function before it
            // reaches the terminal set_status() below (or the cleanup block, which retries the pull
            // anyway). Losing this one pull is recoverable; losing the terminal status is not.
            m_trace(-verbosity) << "final pull_rundir() before break failed: " << e.what() << std::endl;
          }
          break;
        }
      }
      m_trace(4 - verbosity) << "active polling cycle stops" << std::endl;
    }
    using namespace std::literals::chrono_literals;
    // For a remote backend, each cycle above is a real network round trip (a status check plus an
    // rsync pull), and a Molpro calculation's state does not change on a sub-second timescale, so
    // polling much faster than that is pure overhead -- and doing so concurrently across many jobs at
    // once (e.g. right after a large batch is launched, before this cycle's own backoff below has had
    // a chance to grow from a fast initial cycle) is exactly what was observed overloading a real
    // login node into refusing new sessions and going unresponsive. Local jobs have no such cost, so
    // keep their much tighter floor.
    std::this_thread::sleep_for(localhost() ? 10ms : 500ms);
    std::this_thread::sleep_for((stop - start) * 2);
  }
  // Only perform the remote-cache cleanup (which can delete the remote run directory) when we have
  // genuine confidence in the verdict. "killed" only ever comes from an explicit Job::kill() call (this
  // session) or a status genuinely persisted as killed by a previous one, so it's trustworthy as-is. But
  // "completed" can also arise from poll_job()'s status-defaulting above (or any future bug in it)
  // without this instance ever really having tracked the job -- e.g. the brief window right after
  // construction, before Job::run() has paused this polling cycle, where get_status() could still be
  // querying a stale job number left over from a previous run of the same project. Require m_seen_running
  // too in that case, so cleanup only fires once we've actually confirmed the job was alive.
  if (!localhost() and m_backend_command_server != nullptr and
      (status == killed or (status == completed and m_seen_running))) {
    // This whole block is best-effort: it can fail (a timed-out pull, an unresponsive remote ls/rm) for
    // exactly the same reasons a mid-poll sync can, and for exactly the same reason as the try/catches
    // around pull_rundir() above, none of that may be allowed to propagate out of this function. This
    // is the *last* code before the terminal set_status() below; leaving the remote cache behind
    // undeleted (it can be cleaned up by a later Job for the same project, or manually) is a much
    // smaller problem than never publishing a terminal status at all and hanging every external caller
    // forever.
    try {
      m_backend_command_server.reset(new Shell(m_backend.host)); // so that any zombie is resolved or similar
      m_trace(4 - verbosity) << "Pull run directory at end of job " << std::endl;
      m_trace(4 - verbosity) << Shell()("echo local rundir;ls -lta '" + m_project.filename("", "", 0).string() + "'")
                             << std::endl;
      m_trace(4 - verbosity) << "remote cache directory: " << m_remote_cache_directory << std::endl;
      m_trace(4 - verbosity) << (*m_backend_command_server)("echo remote cache;ls -lta '" + m_remote_cache_directory +
                                                            "' 2>&1")
                             << std::endl;
      auto rundir_result = pull_rundir(verbosity);
      m_trace(4 - verbosity) << Shell()("echo local rundir;ls -lta '" + m_project.filename("", "", 0).string() + "'")
                             << std::endl;
      m_trace(4 - verbosity) << (*m_backend_command_server)("echo remote cache;ls -lta '" + m_remote_cache_directory +
                                                            "'  2>&1")
                             << std::endl;
      auto remote_manifest = vector_to_set(util::splitString(
          (*m_backend_command_server)("ls -1 '" + m_remote_cache_directory + "' 2>&1 | grep -v Info.plist"), '\n'));
      auto local_manifest = vector_to_set(util::splitString(
          Shell()("ls -1 '" + m_project.filename("", "", 0).string() + "' 2>&1 | grep -v Info.plist"), '\n'));
      //    std::cout << "rundir_result " << std::get<0>(rundir_result) << std::endl;
      if (!std::get<0>(rundir_result) or remote_manifest == local_manifest) {
        {
          m_trace(4 - verbosity) << "remove run directory " + m_remote_cache_directory + " at end of job "
                                 << std::endl;
          auto slash = m_remote_cache_directory.rfind("/");
          (*m_backend_command_server)("cd '" + m_remote_cache_directory.substr(0, slash) + "' && rm -rf '" +
                                      m_remote_cache_directory.substr(slash + 1) + "'");
        }
      } else if (remote_manifest.count("No such file") !=
                 0) { // sometimes sync will be tried before the remote cache
                     // exists, so stay quiet when that happens
        m_trace(-verbosity) << "Not removing remote cache " << m_backend.host + ":'" + m_remote_cache_directory + "'"
                            << " because master local copy " << m_project.filename("", "", 0) << " has failed to update"
                            << std::endl;
        m_trace(-verbosity) << "remote manifest:\n" << remote_manifest << std::endl;
        m_trace(-verbosity) << "local manifest:\n" << local_manifest << std::endl;
        m_trace(-verbosity) << "Output stream from rsync:\n" << std::get<1>(rundir_result) << std::endl;
        m_trace(-verbosity) << "Error stream from rsync:\n" << std::get<2>(rundir_result) << std::endl;
        m_trace(-verbosity) << "To recover manually, try\n"
                            << "rsync -asv " << m_backend.host + ":'" + m_remote_cache_directory + "/'" << " '"
                            << m_project.filename("", "", 0).string() + "'" << std::endl;
      }
    } catch (const std::exception& e) {
      m_trace(-verbosity) << "End-of-job remote cache cleanup failed, leaving remote cache " << m_backend.host
                          << ":'" << m_remote_cache_directory << "' in place: " << e.what() << std::endl;
    }
  }
  // Publish the terminal status only now, after all the trailing pull_rundir()/remote-cache
  // cleanup above has actually finished touching the local project directory (see the deferral
  // at the top of the loop). status_from_output() re-derives from status() by default, so this
  // still applies its own override (downgrading to "failed" if the pulled output records an
  // error) exactly as before -- only the timing of the first, terminal set_status() moved.
  set_status(status);
  m_project.m_xml_cached = "";
  set_status(m_project.status_from_output());
  m_backend_command_server.reset(); // close down backend server as no longer needed
  m_trace(4 - verbosity) << "Polling stops" << std::endl;
}

} // namespace sjef::util
