#include "Job_server.h"
#include "../sjef-backend.h"
#include "Command.h"
#include <boost/process/args.hpp>
#include <boost/process/child.hpp>
#include <boost/process/io.hpp>
#include <boost/process/search_path.hpp>
#include <boost/process/spawn.hpp>
#include <functional>
#include <future>
namespace bp = boost::process;
namespace fs = std::filesystem;

namespace sjef::util {

///> @private
const bool Job_server::localhost() const {
  return (m_backend.host.empty() || m_backend.host == "localhost" || m_backend.host == "127.0.0.1");
}

sjef::util::Job_server::Job_server(const sjef::Project& project, bool new_job)
    : m_project(project), m_backend(m_project.backends().at(m_project.property_get("backend"))),
      m_remote_cache_directory(m_backend.cache + "/" +
                               std::to_string(std::hash<std::string>{}(m_project.filename("", "", 0).string()))),
      m_remote_server(new Command(m_backend.host)) {}

bool sjef::util::Job_server::push_rundir(int verbosity) {
  if (localhost())
    return true;
  std::string rsync = "rsync";
  auto rsync_command = bp::search_path("rsync");
  std::vector<std::string> rsync_options{
      "--timeout=5",
      "--exclude=backup",
      "--exclude=*.out_*",
      "--exclude=*.xml_*",
      "--exclude=*.log_*",
      "--exclude=*.d",
      "--exclude=Info.plist",
      "--exclude=.Info.plist.writing_object",
      "--inplace",
      "--rsh",
      "ssh -o ControlPath=~/.ssh/sjef-control-%h-%p-%r -o ControlMaster=auto -o ControlPersist=300",
      "--archive",
      "--copy-links",
      "--exclude=*.out",
      "--exclude=*.xml",
      "--exclude=*.log"};
  if (verbosity > 0)
    rsync_options.emplace_back("-v");
  //  rsync_options_first.emplace_back("--mkpath"); // needs rsync >= 3.2.3
  rsync_options.emplace_back(m_project.filename("", "", 0).string() + "/");
  rsync_options.emplace_back(m_backend.host + ":" + m_remote_cache_directory);
  m_project.m_trace(2 - verbosity) << "Push rsync: " << rsync_command;
  for (const auto& o : rsync_options)
    m_project.m_trace(2 - verbosity) << " '" << o << "'";
  m_project.m_trace(2 - verbosity) << std::endl;
  auto start_time = std::chrono::steady_clock::now();
  bp::child(bp::search_path(rsync), rsync_options).wait();
  if (verbosity > 1)
    m_project.m_trace(3 - verbosity)
        << "time for push_rundir() rsync "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << "ms" << std::endl;
  return true; // TODO: implement more robust error checking
}

bool sjef::util::Job_server::pull_rundir(int verbosity) {
  if (localhost())
    return true;
  std::string rsync = "rsync";
  auto rsync_command = bp::search_path("rsync");
  std::vector<std::string> rsync_options{
      "--timeout=5",
      "--exclude=backup",
      "--exclude=*.out_*",
      "--exclude=*.xml_*",
      "--exclude=*.log_*",
      "--exclude=*.d",
      "--exclude=Info.plist",
      "--exclude=.Info.plist.writing_object",
      "--inplace",
      "--rsh",
      "ssh -o ControlPath=~/.ssh/sjef-control-%h-%p-%r -o ControlMaster=auto -o ControlPersist=300",
      "--archive",
  };
  if (verbosity > 0)
    rsync_options.emplace_back("-v");
  //  rsync_options_first.emplace_back("--mkpath"); // needs rsync >= 3.2.3
  rsync_options.emplace_back(m_backend.host + ":" + m_remote_cache_directory + "/");
  rsync_options.emplace_back(m_project.filename("", "", 0).string());
  m_project.m_trace(2 - verbosity) << "Push rsync: " << rsync_command;
  for (const auto& o : rsync_options)
    m_project.m_trace(2 - verbosity) << " '" << o << "'";
  m_project.m_trace(2 - verbosity) << std::endl;
  auto start_time = std::chrono::steady_clock::now();
  bp::child(bp::search_path(rsync), rsync_options).wait();
  if (verbosity > 1)
    m_project.m_trace(3 - verbosity)
        << "time for push_rundir() rsync "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << "ms" << std::endl;
  return true; // TODO: implement more robust error checking
}

void sjef::util::Job_server::end_job() {
  pull_rundir();
  // TODO delete remote cache
}
sjef::util::Job_server::~Job_server() {
  //  if (m_status==sjef::status::completed || m_status==sjef::status::killed) {
  //    end_job();
  //  }
  m_poll_task.wait();
}

static std::vector<std::string> splitString(const std::string& input, char c = ' ', char quote = '\'') {
  std::vector<std::string> result;
  const char* str0 = strdup(input.c_str());
  const char* str = str0;
  do {
    while (*str == c && *str)
      ++str;
    const char* begin = str;
    while (*str && (*str != c || (*begin == quote && str > begin && *(str - 1) != quote)))
      ++str;
    if (*begin == quote && str > begin + 1 && *(str - 1) == quote)
      result.emplace_back(begin + 1, str - 1);
    else
      result.emplace_back(begin, str);
    if (result.back().empty())
      result.pop_back();
  } while (0 != *str++);
  free((void*)str0);
  return result;
}

///> @private
inline std::string executable(const fs::path& command) {
  if (command.is_absolute())
    return command.string();
  else {
    constexpr bool use_boost_search_path = true;
    if (use_boost_search_path) {
      return bp::search_path(command.string()).string();
    } else {
      std::stringstream path{std::string{getenv("PATH")}};
      std::string elem;
      while (std::getline(path, elem, ':')) {
        auto resolved = elem / command;
        if (fs::is_regular_file(resolved))
          return resolved.string();
      }
      return "";
    }
  }
}

std::string Job_server::run(const std::string& command, int verbosity, bool wait) {
  if (!localhost())
    return (*m_remote_server)(command, wait, ".",verbosity);
  return std::string();

  //  m_trace(2 - verbosity) << "run local job, backend=" << backend.name << std::endl;
  auto spl = splitString(command);
  auto run_command = spl.front();
  std::string optionstring;
  for (auto sp = spl.rbegin(); sp < spl.rend() - 1; sp++)
    optionstring = "'" + *sp + "' " + optionstring;
  if (executable(run_command).empty()) {
    //    for (const auto& p : ::boost::this_process::path())
    //      m_warn.error() << "path " << p << std::endl;
    throw runtime_error("Cannot find run command " + run_command);
  }
  //  m_trace(3 - verbosity) << "run local job executable=" << executable(run_command) << " " << optionstring << " "
  //                         << filename("inp", "", rundir) << std::endl;
  //  for (const auto& o : splitString(optionstring))
  //    m_trace(4 - verbosity) << "option " << o << std::endl;
  fs::path current_path_save;
  try {
    current_path_save = fs::current_path();
  } catch (...) {
    current_path_save = "";
  }
  fs::current_path(m_project.filename("", "", 0));
  //  bp::child(executable(run_command), fs::relative(filename("inp", "", rundir)).string()) // TODO do this in the
  //  caller

  auto c = optionstring.empty() ? bp::child(executable(run_command))
                                : bp::child(executable(run_command), bp::args(splitString(optionstring)));
  fs::current_path(current_path_save);
  if (!c.valid())
    throw runtime_error("Spawning run process has failed");
  c.detach();
  m_job_number = c.id();
  //  p_status_mutex.reset();
  //  status(0, false); // to force status cache
  //  m_trace(3 - verbosity) << "jobnumber " << c.id() << ", running=" << c.running() << std::endl;
  if (wait)
    c.wait();
  return "";
}
} // namespace sjef::util
