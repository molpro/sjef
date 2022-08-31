#include "Job.h"
#include "Command.h"
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

sjef::util::Job::Job(const sjef::Project& project)
    : m_project(project), m_backend(m_project.backends().at(m_project.property_get("backend"))),
      m_remote_cache_directory(m_backend.cache + "/" +
                               std::to_string(std::hash<std::string>{}(m_project.filename("", "", 0).string()))),
      m_backend_command_server(new Command(m_backend.host)) {
  m_poll_task = std::async(std::launch::async, [this]() { this->poll_job(); });
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
  Command()(command, verbosity);
  if (verbosity > 1)
    m_project.m_trace(3 - verbosity)
        << "time for push_rundir() rsync "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << "ms" << std::endl;
  return true; // TODO: implement more robust error checking
}

bool sjef::util::Job::pull_rundir(int verbosity) {
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
  Command()(command, verbosity);
  if (verbosity > 1)
    m_project.m_trace(3 - verbosity)
        << "time for pull_rundir() rsync "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << "ms" << std::endl;
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
  auto is_run_command = std::string_view(command).substr(0, command.find(' ')) == m_backend.run_command;
  auto backend_submits_batch = is_run_command and m_backend.run_jobnumber != "([0-9]+)";
  if (backend_submits_batch)
    push_rundir(verbosity);
  auto run_output = (*m_backend_command_server)(command, wait or backend_submits_batch, ".", verbosity);
  if (backend_submits_batch) {
    std::smatch match;
    if (std::regex_search(run_output, match, std::regex{m_backend.run_jobnumber})) {
      //        m_trace(5 - verbosity) << "... a match was found: " << match[1] << std::endl;
      m_job_number = std::stoi(match[1]);
    }
  } else if (is_run_command)
    m_job_number = m_backend_command_server->job_number();
  return run_output;
}

void Job::set_status(status stat) {
  const_cast<Project&>(m_project).property_set("_status", std::to_string(static_cast<int>(stat)));
}

status Job::get_status(int verbosity) {
  auto status_string = (*m_backend_command_server)(m_backend.status_command + " " + std::to_string(m_job_number));
  std::stringstream ss(status_string);
  sjef::status result = completed;
  // TODO check this parsing works for local jobs, including zombie process management
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
  return result;
}

void Job::kill(int verbosity) {
  auto status_string = (*m_backend_command_server)(m_backend.kill_command + " " + std::to_string(m_job_number));
  //   TODO more sophisticated checking that the kill worked
  //  set_status(killed);
  //    std::lock_guard lock(m_closing_mutex);
  m_killed = true;
  //  m_closing = true;
}
void Job::poll_job(int verbosity) {
  status status;
  while (true) {
    if (m_killed)
      sleep(1); //   TODO more sophisticated checking that the kill worked
    status = m_killed ? killed : get_status(verbosity);
    pull_rundir(verbosity);
    set_status(status);
    {
      std::lock_guard lock(m_closing_mutex);
      if (m_closing or status == completed or status == killed)
        break;
    }
  }
  if (status == completed or status == killed) {
    (*m_backend_command_server)("rm -rf "+m_remote_cache_directory);
  }
  m_backend_command_server.reset(); // close down backend server as no longer needed
}

} // namespace sjef::util
