#include "Command.h"
#include <chrono>
#include <regex>
#include <thread>

namespace sjef::util {

Command::Command(std::string host, std::string shell) : m_host(std::move(host)) {
  if (!localhost()) {
    m_out.reset(new bp::ipstream);
    m_err.reset(new bp::ipstream);
    m_process = bp::child(bp::search_path("ssh"), m_host, std::move(shell), bp::std_in<m_in, bp::std_err> * m_err,
                          bp::std_out > *m_out);
  }
  // TODO error checking
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

std::string Command::operator()(const std::string& command, bool wait, std::string directory, int verbosity) const {
  std::lock_guard lock(m_run_mutex);
  m_trace(2 - verbosity) << command << std::endl;
  m_last_out.clear();
  if (localhost()) {
    m_trace(2 - verbosity) << "run local command: " << command << std::endl;
    auto spl = splitString(command);
    auto run_command = spl.front();
    std::string optionstring;
    for (auto sp = spl.rbegin(); sp < spl.rend() - 1; sp++)
      optionstring = "'" + *sp + "' " + optionstring;
    if (executable(run_command).empty()) {
      for (const auto& p : ::boost::this_process::path())
        m_warn.error() << "path " << p << std::endl;
      throw std::runtime_error("Cannot find run command " + run_command);
    }
    m_trace(3 - verbosity) << "run local job executable=" << executable(run_command) << " " << optionstring << " "
                           << std::endl;
    for (const auto& o : splitString(optionstring))
      m_trace(4 - verbosity) << "option " << o << std::endl;
    fs::path current_path_save;
    try {
      current_path_save = fs::current_path();
    } catch (...) {
      current_path_save = "";
    }
    fs::current_path(directory);

    m_out.reset(new bp::ipstream);
    m_err.reset(new bp::ipstream);
    m_process = bp::child(executable(run_command), bp::args(splitString(optionstring)), bp::std_out > *m_out,
                          bp::std_err > *m_err);
    fs::current_path(current_path_save);
    if (!m_process.valid())
      throw std::runtime_error("Spawning run process has failed");
    m_process.detach();
    m_job_number = m_process.id();
    m_trace(3 - verbosity) << "jobnumber " << m_process.id() << ", running=" << m_process.running() << std::endl;
    if (!wait)
      return "";
    std::string line;
    while (m_process.running() && std::getline(*m_out, line)) {
      m_trace(4 - verbosity) << "out line from local command " << command << ": " << line << std::endl;
      m_last_out += line + '\n';
    }
    m_trace(3 - verbosity) << "out from local command " << command << ": " << m_last_out << std::endl;
    m_process.wait();
  } else {
    //    if (!wait) {
    //      m_in << command << " >/dev/null 2>/dev/null & echo $!" << std::endl;
    //      return "";
    //    }
    const std::string terminator{"@@@!!EOF"};
    const std::string jobnumber_tag{"@@@!!JOBNUMBER"};
    m_in << std::string{"(cd "} + directory + "; " + command +
                (wait ? ")" : std::string{")& echo "} + jobnumber_tag + " $! 1>&2")
         << std::endl;
    m_in << ">&2 echo '" << terminator << "' $?" << std::endl;
    m_in << "echo '" << terminator << "'" << std::endl;
    std::string line;
    while (std::getline(*m_out, line) && line != terminator) {
      m_trace(4 - verbosity) << "out line from remote command " << line << std::endl;
      m_last_out += line + '\n';
    }
    m_trace(3 - verbosity) << "out from remote command\n" << command << ": " << m_last_out << std::endl;
    m_last_err.clear();
    while (std::getline(*m_err, line) && line.substr(0, terminator.size()) != terminator) {
      m_trace(4 - verbosity) << "err line from remote command " << line << std::endl;
      std::smatch match;
      if (std::regex_search(line, match, std::regex{jobnumber_tag + "\\s*(\\d+)"})) {
        m_trace(5 - verbosity) << "... a match was found: " << match[1] << std::endl;
        m_job_number = std::stoi(match[1]);
      } else
        m_last_err += line + '\n';
    }
    m_trace(3 - verbosity) << "err from remote command " << command << ":\n" << m_last_err << std::endl;
    m_trace(3 - verbosity) << "last line=" << line << std::endl;
    auto rc = line.empty() ? -1 : std::stoi(line.substr(terminator.size() + 1));
    m_trace(3 - verbosity) << "rc=" << rc << std::endl;
    if (rc != 0)
      throw std::runtime_error("Host " + m_host + "; failed remote command: " + command + "\nStandard output:\n" +
                               m_last_out + "\nStandard error:\n" + m_last_err);
  }
  if (m_last_out[m_last_out.size() - 1] == '\n')
    m_last_out.resize(m_last_out.size() - 1);
  return m_last_out;
}

void Command::wait() const {
  using namespace std::chrono_literals;
  while (running())
    std::this_thread::sleep_for(100ms); // TODO more sophisticated interval
}

bool Command::running() const {
  if (localhost())
    return m_process.running();
  return (*this)(std::string{"ps -p "} + std::to_string(m_job_number) + " > /dev/null 2>/dev/null; echo $?") == "0";
}
} // namespace sjef::util