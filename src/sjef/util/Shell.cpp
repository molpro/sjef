#include "Shell.h"
#include <boost/process/search_path.hpp>
#include <boost/process/spawn.hpp>
#include <chrono>
#include <filesystem>
#include <regex>
#include <thread>

namespace fs = std::filesystem;

namespace sjef::util {

Shell::Shell(std::string host, std::string shell) : m_host(std::move(host)) {
  if (!localhost()) {
    m_out.reset(new bp::ipstream);
    m_err.reset(new bp::ipstream);
    m_process = bp::child(bp::search_path("ssh"), m_host, std::move(shell), "-l", bp::std_in<m_in, bp::std_err> * m_err,
                          bp::std_out > *m_out);
    if (!m_process.valid())
      throw std::runtime_error("Spawning run process has failed");
  }
}

///> @private
static std::string executable(const fs::path& command) {
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

std::string Shell::operator()(const std::string& command, bool wait, const std::string& directory, int verbosity,
                              const std::string& out, const std::string& err) const {
  std::lock_guard lock(m_run_mutex);
  m_trace(2 - verbosity) << "Command::operator() " << command << std::endl;
  m_trace(2 - verbosity) << "Command::operator() m_host=" << m_host << ", wait=" << wait
                         << ", localhost()=" << localhost() << std::endl;
  m_last_out.clear();
  const std::string jobnumber_tag{"@@@JOBNUMBER"};
  const std::string terminator{"@@@EOF"};
  auto pipeline = command;
  if (!wait)
    pipeline = "(( " + command + " >" + out + " 2>" + err + ") & echo " + jobnumber_tag + " $! 1>&2)";
  m_trace(2 - verbosity) << "Command::operator() pipeline=" << pipeline << std::endl;
  if (localhost()) {

    fs::path current_path_save;
    try {
      current_path_save = fs::current_path();
    } catch (...) {
      current_path_save = "";
    }
    fs::current_path(directory);

    m_out.reset(new bp::ipstream);
    m_err.reset(new bp::ipstream);
    m_trace(2 - verbosity) << "launching local process" << std::endl;
    m_process = bp::child(executable("nohup"), "sh", "-c", pipeline,
                          //        bp::child(executable("sh"),"-vxc",pipeline,
                          //        bp::args(splitString(pipeline)),
                          bp::std_out > *m_out, bp::std_err > *m_err);
    fs::current_path(current_path_save);
    if (!m_process.valid())
      throw std::runtime_error("Spawning run process has failed");
    m_process.detach();
    //      m_trace(3 - verbosity) << "jobnumber " << m_process.id() << ", running=" << m_process.running() <<
    //      std::endl; std::string line; while (std::getline(*m_out, line)) {
    //        m_trace(4 - verbosity) << "out line from command " << command << ": " << line << std::endl;
    //        m_jobnumber = wait ? 0 : std::to_string(line);
    //        m_last_out += line + '\n';
    //      }
    //      m_trace(3 - verbosity) << "out from command " << command << ": " << m_last_out << std::endl;
  } else {
    m_in << std::string{"cd '"} + directory + "'" << std::endl;
    m_in << pipeline << std::endl;
    m_in << ">&2 echo '" << terminator << "' $?" << std::endl;
    m_in << "echo '" << terminator << "'" << std::endl;
  }
  std::string line;
  m_job_number = 0;
  while (std::getline(*m_out, line) && line != terminator) {
    m_trace(4 - verbosity) << "out line from command " << line << std::endl;
    m_last_out += line + '\n';
  }
  m_trace(3 - verbosity) << "out from command " << command << ":\n" << m_last_out << std::endl;
  m_last_err.clear();
  while (std::getline(*m_err, line) && line.substr(0, terminator.size()) != terminator) {
    m_trace(4 - verbosity) << "err line from command " << line << std::endl;
    std::smatch match;
    if (std::regex_search(line, match, std::regex{jobnumber_tag + "\\s*(\\d+)"})) {
      m_trace(5 - verbosity) << "... a match was found: " << match[1] << std::endl;
      m_job_number = std::stoi(match[1]);
    } else
      m_last_err += line + '\n';
  }
  m_trace(3 - verbosity) << "err from command " << command << ":\n" << m_last_err << std::endl;
  if (localhost())
    m_process.wait();
  //  m_trace(3 - verbosity) << "last line=" << line << std::endl;
  //  auto rc = line.empty() ? -1 : std::stoi(line.substr(terminator.size() + 1));
  //  m_trace(3 - verbosity) << "rc=" << rc << std::endl;
  //  if (rc != 0)
  //    throw std::runtime_error("Host " + m_host + "; failed remote command: " + command + "\nStandard output:\n" +
  //                             m_last_out + "\nStandard error:\n" + m_last_err);
  if (m_last_out[m_last_out.size() - 1] == '\n')
    m_last_out.resize(m_last_out.size() - 1);
  return m_last_out;
}

void Shell::wait(int min_wait_milliseconds, int max_wait_milliseconds) const {
  using namespace std::chrono_literals;
  if (max_wait_milliseconds <= 0)
    max_wait_milliseconds = min_wait_milliseconds;
  constexpr auto radix = 2;
  auto wait_milliseconds = min_wait_milliseconds;
  while (running()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(wait_milliseconds));
    wait_milliseconds = std::max(std::min(wait_milliseconds * radix, max_wait_milliseconds),
                                 min_wait_milliseconds <= 0 ? 1 : min_wait_milliseconds);
  }
}

bool Shell::running() const {
  if (localhost() and m_job_number==0)
    return m_process.running();
  return (*this)(std::string{"ps -p "} + std::to_string(m_job_number) + " > /dev/null 2>/dev/null; echo $?") == "0";
}
} // namespace sjef::util