#include "Shell.h"
#if __has_include(<boost/process/child.hpp>)
#include <boost/process/search_path.hpp>
#include <boost/process/spawn.hpp>
#else
#include <boost/process/v1/search_path.hpp>
#include <boost/process/v1/spawn.hpp>
#endif
#include <chrono>
#include <filesystem>
#include <regex>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace sjef::util {

Shell::Shell(std::string host, std::string shell) : m_host(std::move(host)), m_shell((shell)) {
  if (!localhost()) {
    if (shell.empty())
      throw std::runtime_error("Shell() cannot run remote commands with a null shell");
    m_out.reset(new bp::ipstream);
    m_err.reset(new bp::ipstream);
#ifdef WIN32
    auto ssh = "C:\\Windows\\System32\\OpenSSH\\ssh.exe";
#else
    auto ssh = bp::search_path("ssh");
#endif
    m_process =
        bp::child(ssh, m_host, std::move(shell), "-l", bp::std_in<m_in, bp::std_err> * m_err, bp::std_out > *m_out);
    //    std::cout << "ssh is"<<ssh<<", "<<m_process.valid()<<", "<<m_process.running()<<std::endl;
    if (!m_process.valid() || !m_process.running())
      throw Shell::runtime_error("Spawning run process has failed");
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

std::vector<std::string> tokenise(const std::string& command) {
  std::vector<std::string> tokens;
  std::stringstream ss(command);
  std::string token;
  std::string quoted_token;
  while (getline(ss, token, ' ')) {
    auto first_quote_pos = token.find_first_of("'\"");
    auto last_quote_pos = token.find_last_of("'\"");
    if (!quoted_token.empty()) {
      if (first_quote_pos == std::string::npos) {
        quoted_token += " " + token;
      } else {
        tokens.push_back(quoted_token + " " + token.substr(0, first_quote_pos) + token.substr(first_quote_pos + 1));
        quoted_token.clear();
      }
    } else if (first_quote_pos != std::string::npos && last_quote_pos != first_quote_pos) {
      tokens.push_back(token.substr(0, first_quote_pos) +
                       token.substr(first_quote_pos + 1, last_quote_pos - first_quote_pos - 1) +
                       token.substr(last_quote_pos + 1));
    } else if (first_quote_pos != std::string::npos) {
      quoted_token = token.substr(0, first_quote_pos) + token.substr(first_quote_pos + 1);
    } else
      tokens.push_back(token);
  }
  return tokens;
}

std::string Shell::operator()(const std::string& command, bool wait, const std::string& directory, int verbosity,
                              const std::string& out, const std::string& err) const {
  std::lock_guard lock(m_run_mutex);
#ifdef WIN32
  //  _putenv_s("PATH", (fs::current_path().string() + ";C:\\msys64\\usr\\bin;C:\\Program
  //  Files\\Molpro\\bin;/usr/local/bin;/usr/bin;/bin").c_str());
  // TODO reconsider this when implementing remote backend for Windows
  m_trace(2 - verbosity) << "Shell() PATH in environment " << getenv("PATH") << std::endl;
  // set $SCRATCH to directory which can be safely resolved in MSYS2 and native Windows
  if (NULL == std::getenv("SCRATCH")) {
    const char* scratch = std::getenv("TEMP");
    std::string scratch_env = scratch;
    if (!scratch_env.empty()) {
      std::replace(scratch_env.begin(), scratch_env.end(), '\\', '/');
      _putenv_s("SCRATCH", scratch_env.c_str());
    }
  }
#endif
  m_trace(2 - verbosity) << "Command::operator() " << command << std::endl;
  m_trace(2 - verbosity) << "Command::operator() m_host=" << m_host << ", wait=" << wait
                         << ", localhost()=" << localhost() << std::endl;
  m_last_out.clear();
  wait = wait || m_shell.empty();
  const std::string jobnumber_tag{"@@@JOBNUMBER"};
  const std::string terminator{"@@@EOF"};
  auto pipeline = command;
  if (!local_asynchronous_supported() and !wait and localhost())
    throw std::logic_error("Shell::operator() with wait==false is not supported on Windows");
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
    if (m_shell.empty()) {
      m_trace(2 - verbosity) << "launching local process: " << command << std::endl;
      auto tokens = tokenise(command);
      if (!tokens.empty())
        tokens[0] = executable(tokens[0]);
//      for (const auto& token : tokens)
//        std::cout << "token " << token << std::endl;
      m_process = bp::child(tokens, bp::std_out > *m_out, bp::std_err > *m_err);
    } else {
      m_trace(2 - verbosity) << "launching local process: " << executable("nohup") << " " << m_shell << " -c "
                             << pipeline << std::endl;
      m_process = bp::child(executable("nohup"), m_shell, "-c", pipeline, bp::std_out > *m_out, bp::std_err > *m_err);
    }
    fs::current_path(current_path_save);
    if (!m_process.valid())
      throw Shell::runtime_error("Spawning run process has failed");
    m_process.detach();
    //      m_trace(3 - verbosity) << "jobnumber " << m_process.id() << ", running=" << m_process.running() <<
    //      std::endl; std::string line; while (std::getline(*m_out, line)) {
    //        m_trace(4 - verbosity) << "out line from command " << command << ": " << line << std::endl;
    //        m_jobnumber = wait ? 0 : std::to_string(line);
    //        m_last_out += line + '\n';
    //      }
    //      m_trace(3 - verbosity) << "out from command " << command << ": " << m_last_out << std::endl;
  } else {
    try {
      if (!m_process.valid() || !m_process.running())
        throw Shell::runtime_error("remote server process has died");
      m_in << std::string{"cd '"} + directory + "'" << std::endl;
      m_in << pipeline << std::endl;
      m_in << ">&2 echo '" << terminator << "' $?" << std::endl;
      m_in << "echo '" << terminator << "'" << std::endl;
    } catch (const std::exception& e) {
      throw Shell::runtime_error((std::string{"Spawning run process has failed: "} + e.what()).c_str());
    }
  }
  std::string line;
  m_job_number = 0;
  m_last_err.clear();
  try {
    while (std::getline(*m_out, line) && line != terminator) {
      m_trace(4 - verbosity) << "out line from command " << line << std::endl;
      m_last_out += line + '\n';
    }
    m_trace(3 - verbosity) << "out from command " << command << ":\n" << m_last_out << std::endl;
    while (std::getline(*m_err, line) && line.substr(0, terminator.size()) != terminator) {
      m_trace(4 - verbosity) << "err line from command " << line << std::endl;
      std::smatch match;
      if (std::regex_search(line, match, std::regex{jobnumber_tag + "\\s*(\\d+)"})) {
        m_trace(5 - verbosity) << "... a match was found: " << match[1] << std::endl;
        m_job_number = std::stoi(match[1]);
      } else
        m_last_err += line + '\n';
    }
  } catch (const std::exception& e) {
    throw runtime_error(
        (std::string{"Shell(\""} + command +
         "\") has failed whilst capturing the output.\nExit code: " + std::to_string(m_process.exit_code()) +
         "\n\nstdout:\n" + m_last_out + "\nstderr:\n" + m_last_err + "\nException thrown:" + e.what())
            .c_str());
  }
  m_trace(3 - verbosity) << "err from command " << command << ":\n" << m_last_err << std::endl;
  if (localhost()) {
    m_process.wait();
    if (m_process.exit_code()) {
      throw runtime_error((std::string{"Shell(\""} + command +
                           "\") has failed.\nExit code: " + std::to_string(m_process.exit_code()) + "\n\nstdout:\n" +
                           m_last_out + "\nstderr:\n" + m_last_err)
                              .c_str());
    }
  }
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
  if (localhost() and m_job_number == 0)
    return m_process.running();
  return (*this)(std::string{"ps -p "} + std::to_string(m_job_number) + " > /dev/null 2>/dev/null; echo $?") == "0";
}
bool Shell::local_asynchronous_supported() {
  // #ifdef WIN32
  //   return false;
  // #endif
  return true;
}
} // namespace sjef::util
