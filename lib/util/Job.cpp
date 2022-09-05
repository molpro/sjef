#include "Job.h"
#include "Shell.h"
#include "util.h"
#include <chrono>
#include <functional>
#include <future>
#include <regex>
#include <sstream>
#include <string>
namespace fs = std::filesystem;

namespace sjef::util {

///> @private
const bool Job::localhost() const {
  return (m_backend.host.empty() || m_backend.host == "localhost" || m_backend.host == "127.0.0.1");
}

std::mutex kill_mutex;
sjef::util::Job::Job(const sjef::Project& project)
    : m_project(project), m_backend(m_project.backends().at(m_project.property_get("backend"))),
      m_remote_cache_directory(m_backend.cache + "/" +
                               std::to_string(std::hash<std::string>{}(m_project.filename("", "", 0).string()))),
      m_backend_command_server(new Shell(m_backend.host)),
      m_job_number(std::stoi("0" + m_project.property_get("jobnumber"))),
      m_initial_status(static_cast<sjef::status>(std::stoi("0" + m_project.property_get("_status")))) {
  //  std::cout << "Job constructor, m_job_number=" << m_job_number << std::endl;
  m_poll_task = std::async(std::launch::async, [this]() { this->poll_job(); });
  //  std::cout << "Job constructor has launched poll task" << std::endl;
}
bool sjef::util::Job::push_rundir(int verbosity) {
  if (localhost())
    return true;
  std::string command = "rsync --archive --copy-links --timeout=5";
  command += " --exclude=*.out* --exclude=*.log* --exclude=*.xml*";
  command += " --exclude=Info.plist --exclude=.Info.plist.writing_object";
  command += " --rsh 'ssh -o ControlPath=~/.ssh/sjef-control-%h-%p-%r -o ControlMaster=auto -o ControlPersist=300'";
  command += " " + m_project.filename("", "", 0).string() + "/";
  command += " " + m_backend.host + ":" + m_remote_cache_directory;
  if (verbosity > 0)
    command += " -v";
  m_project.m_trace(2 - verbosity) << "Push rsync: " << command << std::endl;
  auto start_time = std::chrono::steady_clock::now();
  (*m_backend_command_server)("mkdir -p " + m_remote_cache_directory);
  Shell()(command, verbosity);
  if (verbosity > 1)
    m_project.m_trace(3 - verbosity)
        << "time for push_rundir() rsync "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << "ms" << std::endl;
  return true; // TODO: implement more robust error checking
}

bool sjef::util::Job::pull_rundir(int verbosity) {
  m_trace(3 - verbosity) << "pull_rundir " << verbosity << std::endl;
  if (localhost())
    return true;
  std::string command = "rsync --archive --copy-links --timeout=5";
  command += " --exclude=*.out_* --exclude=*.log_* --exclude=*.xml_* --exclude=backup --exclude=*.d";
  command += " --exclude=Info.plist --exclude=.Info.plist.writing_object";
  command += " --rsh 'ssh -o ControlPath=~/.ssh/sjef-control-%h-%p-%r -o ControlMaster=auto -o ControlPersist=300'";
  command += " " + m_backend.host + ":" + m_remote_cache_directory + "/";
  command += " " + m_project.filename("", "", 0).string();
  if (verbosity > 0)
    command += " -v";
  m_project.m_trace(2 - verbosity) << "Pull rsync: " << command << std::endl;
  auto start_time = std::chrono::steady_clock::now();
  auto rsync_out = Shell()(command, verbosity);
  if (verbosity > 1)
    m_project.m_trace(3 - verbosity)
        << "time for pull_rundir() rsync "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << "ms" << std::endl
        << "Output from rsync\n:" << rsync_out << std::endl;
  return true; // TODO: implement more robust error checking
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

std::string Job::run(const std::string& command, int verbosity, bool wait) {
  m_closing = true;
  m_poll_task.wait();
  m_closing = false;
  m_backend_command_server.reset(new Shell(m_backend.host));
  auto l = std::lock_guard(kill_mutex);
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
  if (!localhost())
    push_rundir(verbosity);
  m_trace(4 - verbosity) << "Job::run() gives directory " << m_project.filename("", "", 0) << std::endl;
  m_trace(4 - verbosity) << "before submit, m_backend_command_server? " << (m_backend_command_server == nullptr)
                         << std::endl;
  auto run_output = (*m_backend_command_server)(
      command, wait or backend_submits_batch,
      localhost() ? m_project.filename("", "", 0).string() : m_remote_cache_directory, verbosity);
  if (backend_submits_batch) {
    std::smatch match;
    if (std::regex_search(run_output, match, std::regex{m_backend.run_jobnumber})) {
      //        m_trace(5 - verbosity) << "... a match was found: " << match[1] << std::endl;
      m_job_number = std::stoi(match[1]);
      m_trace(4 - verbosity) << "Job::run backend_submits_batch m_job_number=" << m_job_number << std::endl;
    }
  } else {
    m_trace(4 - verbosity) << "before job_number(), m_backend_command_server? " << (m_backend_command_server == nullptr)
                           << std::endl;
    m_job_number = m_backend_command_server->job_number();
    m_trace(4 - verbosity) << "Job::run is_run_command m_job_number=" << m_job_number << std::endl;
  }
  m_poll_task = std::async(std::launch::async, [this]() { this->poll_job(); });
  return run_output;
  m_trace(2 - verbosity) << "Job::run() returns " << run_output << std::endl;
}

void Job::set_status(status stat) {
  const_cast<Project&>(m_project).property_set("_status", std::to_string(static_cast<int>(stat)));
}

status Job::get_status(int verbosity) {
  if (m_job_number == 0)
    return unknown;
  auto status_string =
      (*m_backend_command_server)(m_backend.status_command + " " + std::to_string(m_job_number), true, ".", verbosity);
  //  std::cout << "status_string:\n" << status_string << std::endl;
  std::stringstream ss(status_string);
  sjef::status result = unknown;
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
  //  std::cout << "Job::kill()"<<std::endl;
  {
    auto l = std::lock_guard(kill_mutex);
    //    std::cout << "Job::kill() gets mutex"<<std::endl;
    auto status_string =
        (*m_backend_command_server)(m_backend.kill_command + " " + std::to_string(m_job_number), true, ".", verbosity);
    //    std::cout << "Job::kill() finished killing"<<std::endl;
    set_status(killed);
    //    std::cout << "Job::kill() finished set_status()"<<std::endl;
  }
  m_killed = true;
  //  std::cout << "Job::kill() set sentinel"<<std::endl;
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
      auto l = std::lock_guard(kill_mutex);
      //      std::cout << "active polling cycle starts"<<std::endl;
      //      if (m_killed)
      //        std::cout << "poll_job received kill sentinel" << std::endl;

      start = Clock::now();
      status = m_killed ? killed : get_status(verbosity);
      if (status == unknown) {
        if (m_initial_status == killed)
          status = killed;
        if (m_initial_status == running or m_initial_status == completed or m_initial_status == waiting)
          status = completed;
      }
      m_trace(4 - verbosity) << "got status " << status << std::endl;
      pull_rundir(verbosity);
      set_status(status);
      //    std::cout << "set status " << m_project.status_message() << std::endl;
      stop = Clock::now();
      {
        std::lock_guard lock(m_closing_mutex);
        if (m_closing or status == completed or m_killed)
          break;
      }
      m_trace(4 - verbosity) << "active polling cycle stops" << std::endl;
    }
    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(10ms);
    std::this_thread::sleep_for((stop - start) * 2);
  }
  if (!localhost() and m_backend_command_server != nullptr and (status == completed or status == killed)) {
    m_trace(4 - verbosity) << "Pull run directory at end of job " << std::endl;
    m_trace(4 - verbosity) << Shell()("echo local rundir;ls -lta " + m_project.filename("", "", 0).string())
                           << std::endl;
    m_trace(4 - verbosity) << "remote cache directory: " << m_remote_cache_directory << std::endl;
    m_trace(4 - verbosity) << (*m_backend_command_server)("echo remote cache;ls -lta " + m_remote_cache_directory +
                                                          "  2>&1")
                           << std::endl;
    pull_rundir(verbosity); // TODO don't remove unless it worked!
    m_trace(4 - verbosity) << Shell()("echo local rundir;ls -lta " + m_project.filename("", "", 0).string())
                           << std::endl;
    m_trace(4 - verbosity) << (*m_backend_command_server)("echo remote cache;ls -lta " + m_remote_cache_directory +
                                                          "  2>&1")
                           << std::endl;
    auto remote_manifest = util::splitString(
        (*m_backend_command_server)("ls " + m_remote_cache_directory + " 2>&1 | grep -v Info.plist"), '\n');
    auto local_manifest =
        util::splitString(Shell()("ls " + m_project.filename("", "", 0).string() + " 2>&1 | grep -v Info.plist"), '\n');
    if (remote_manifest == local_manifest) {

      m_trace(4 - verbosity) << "remove run directory " + m_remote_cache_directory + " at end of job " << std::endl;
      (*m_backend_command_server)("rm -rf " + m_remote_cache_directory);
    } else {
      m_trace(-verbosity) << "Not removing remote cache " << m_backend.host + ":" + m_remote_cache_directory
                          << " because master local copy " << m_project.filename("", "", 0) << " has failed to update"
                          << std::endl;
      m_trace(-verbosity) << "remote manifest:\n" << remote_manifest << std::endl;
      m_trace(-verbosity) << "local manifest:\n" << local_manifest << std::endl;
      m_trace(-verbosity) << "To recover manually, try\n"
                          << "rsync -a " << m_backend.host + ":" + m_remote_cache_directory + "/"
                          << " " << m_project.filename("", "", 0).string() << std::endl;
    }
  }
  m_backend_command_server.reset(); // close down backend server as no longer needed
  //  m_trace(4-verbosity) << "Polling stops" << std::endl;
}

} // namespace sjef::util
