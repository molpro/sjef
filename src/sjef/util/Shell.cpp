#include "Shell.h"
#if __has_include(<boost/process/child.hpp>)
#include <boost/process/spawn.hpp>
#else
#include <boost/process/v1/spawn.hpp>
#endif
#include <chrono>
#include <filesystem>
#include <regex>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace sjef::util {
static std::string executable(const std::string& command);
const std::string jobnumber_tag{"@@@JOBNUMBER"};
const std::string terminator{"@@@EOF"};

Shell::Shell(std::string host, std::string shell) : m_host(std::move(host)), m_shell((shell)) {
  if (!localhost()) {
    if (shell.empty())
      throw std::runtime_error("Shell() cannot run remote commands with a null shell");
    m_out.reset(new bp::ipstream);
    m_err.reset(new bp::ipstream);
#ifdef WIN32
    auto ssh = "C:\\Windows\\System32\\OpenSSH\\ssh.exe";
#else
    auto ssh = executable("ssh");
#endif
    m_process =
        bp::child(ssh, m_host, std::move(shell), "-l", bp::std_in<m_in, bp::std_err> * m_err, bp::std_out > *m_out);
    //    std::cout << "ssh is"<<ssh<<", "<<m_process.valid()<<", "<<m_process.running()<<std::endl;
    if (!m_process.valid() || !m_process.running())
      throw Shell::runtime_error("Spawning run process has failed");
  }
}

///> @private
static std::string executable(const std::string& command) {
  if (fs::path(command).is_absolute())
    return command;
  else {
    std::stringstream path{std::string{getenv("PATH")}};
    std::string elem;
    while (std::getline(path, elem, ':')) {
      auto resolved = elem / fs::path{command};
      if (fs::is_regular_file(resolved))
        return resolved.string();
    }
    return "";
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

void Shell::run_local_sync(const std::string& command, const std::string& directory, int verbosity,
                           const std::string& out, const std::string& err) const {
  auto wait = true;
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
    m_trace(2 - verbosity) << "launching no-shell local process: " << command << std::endl;
    auto tokens = tokenise(command);
    if (!tokens.empty())
      tokens[0] = executable(tokens[0]);
    if (out == "/dev/null" and err == "/dev/null")
      m_process = bp::child(tokens, bp::std_out > *m_out, bp::std_err > *m_err);
    else
      m_process = bp::child(tokens, bp::std_out > out, bp::std_err > err);
    m_job_number = m_process.id();
  } else {
    m_trace(2 - verbosity) << "launching shell local process: " << executable("nohup") << " " << m_shell << " -c "
                           << command << std::endl;
    if (out == "/dev/null" and err == "/dev/null")
      m_process = bp::child(executable("nohup"), m_shell, "-c", command, bp::std_out > *m_out, bp::std_err > *m_err);
    else
      m_process = bp::child(executable("nohup"), m_shell, "-c", command, bp::std_out > out, bp::std_err > err);
  }
  if (!m_process.valid())
    throw Shell::runtime_error("Spawning run process has failed");
  std::string line;
  try {
    while (std::getline(*m_err, line) && line.substr(0, terminator.size()) != terminator) {
      m_last_err += line + '\n';
    }
    // std::cout << "wait , read output" << std::endl;
    while (std::getline(*m_out, line) && line != terminator) {
      // std::cout << "out line from command " << line << std::endl;
      m_last_out += line + '\n';
    }
  } catch (const std::exception& e) {
    throw runtime_error(
        (std::string{"Shell(\""} + command +
         "\") has failed whilst capturing the output.\nExit code: " + std::to_string(m_process.exit_code()) +
         "\n\nstdout:\n" + m_last_out + "\nstderr:\n" + m_last_err + "\nException thrown:" + e.what())
            .c_str());
  }
  fs::current_path(current_path_save);
  m_process.wait();
  if (m_process.exit_code()) {
    throw runtime_error((std::string{"Shell(\""} + command +
                         "\") has failed.\nExit code: " + std::to_string(m_process.exit_code()) + "\n\nstdout:\n" +
                         m_last_out + "\nstderr:\n" + m_last_err)
                            .c_str());
  }
}
void Shell::capture_job_number_from_error(const std::string& command) const {
  m_last_err.clear();
  m_job_number = 0;
  std::string line;
  try {
    while (std::getline(*m_err, line)) {
      std::smatch match;
      if (std::regex_search(line, match, std::regex{jobnumber_tag + "\\s*(\\d+)"})) {
        m_job_number = std::stoi(match[1]);
        break;
      }
    }
  } catch (const std::exception& e) {
    throw runtime_error(
        (std::string{"Shell(\""} + command + "\") has failed whilst capturing the job number.\nExit code: " +
         std::to_string(m_process.exit_code()) + "\nstderr:\n" + m_last_err + "\nException thrown:" + e.what())
            .c_str());
  }
  // std::cout << "capture_job_number returning, m_job_number="<<m_job_number<<std::endl;
}

void Shell::run_local_async(const std::string& command, const std::string& directory, int verbosity,
                            const std::string& out) const {
  fs::path current_path_save;
  try {
    current_path_save = fs::current_path();
  } catch (...) {
    current_path_save = "";
  }
  fs::current_path(directory);

  std::string pipeline{"(( " + command + ") 2>&1 & echo " + jobnumber_tag + " $! 1>&2)"};
  m_out.reset(new bp::ipstream);
  m_err.reset(new bp::ipstream);
  m_trace(2 - verbosity) << "launching shell local process: " << executable("nohup") << " " << m_shell << " -c '"
                         << pipeline << "'" << std::endl;
  if (out == "/dev/null") {
    m_process = bp::child(executable("nohup"), m_shell, "-c", pipeline, bp::std_out > *m_out, bp::std_err > *m_err);
    m_process.detach();
    m_stdout_future_running = true;
    m_stdout_future = std::async(std::launch::async, &Shell::capture_out, this);
  } else {
    m_process = bp::child(executable("nohup"), m_shell, "-c", pipeline, bp::std_out > out, bp::std_err > *m_err);
    m_process.detach();
  }
  capture_job_number_from_error(command);
  fs::current_path(current_path_save);
  if (!m_process.valid())
    throw Shell::runtime_error("Spawning run process has failed");
}

void Shell::run_remote(std::string command, const std::string& directory, bool wait, int verbosity,
                       const std::string& out) const {
  if (out == "/dev/null")
    command = "(( " + command + ") 2>&1 & echo " + jobnumber_tag + " $! 1>&2)";
  else
    command = "(( " + command + ") > " + out + " 2>&1 & echo " + jobnumber_tag + " $! 1>&2)";
  if (!m_process.valid() || !m_process.running())
    throw Shell::runtime_error("remote server process has died");
  m_stdout_future_running = true;
  m_stdout_future = std::async(std::launch::async, &Shell::capture_out, this);
  try {
    m_in << std::string{"cd '"} + directory + "'" << std::endl;
    m_in << std::string{"nohup /bin/sh -c '"} + command + "'" << std::endl;
    capture_job_number_from_error(command);
    m_in << ">&2 echo '" << terminator << "' $?" << std::endl;
    m_in << "echo '" << terminator << "'" << std::endl;
  } catch (const std::exception& e) {
    throw Shell::runtime_error((std::string{"Spawning run process has failed: "} + e.what()).c_str());
  }
  if (!m_process.valid() || !m_process.running())
    throw Shell::runtime_error("remote server process has died");
  if (wait)
    this->wait();
}

void Shell::capture_out() const {
  m_last_out.clear();
  std::string line;
  try {
    while (std::getline(*m_out, line) && line != terminator) {
      m_last_out += line + '\n';
    }
  } catch (const std::exception& e) {
    throw Shell::runtime_error((std::string{"Shell() has failed whilst capturing the output.\nExit code: "} +
                                std::to_string(m_process.exit_code()) + "\n\nstdout:\n" + m_last_out +
                                "\nException thrown:" + e.what())
                                   .c_str());
  }
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
  m_trace(2 - verbosity) << "Command::operator() out=" << out << std::endl;
  m_trace(2 - verbosity) << "Command::operator() err=" << err << std::endl;
  m_last_out.clear();
  wait = wait || m_shell.empty();
  if (!local_asynchronous_supported() and !wait and localhost())
    throw std::logic_error("Shell::operator() with wait==false is not supported on Windows");
  if (localhost()) {

    wait = wait || m_shell.empty();
    if (wait)
      run_local_sync(command, directory, verbosity, out, err);
    else
      run_local_async(command, directory, verbosity, out);
  } else {
    run_remote(command, directory, wait, verbosity, out);
  }
  //  m_trace(3 - verbosity) << "last line=" << line << std::endl;
  //  auto rc = line.empty() ? -1 : std::stoi(line.substr(terminator.size() + 1));
  //  m_trace(3 - verbosity) << "rc=" << rc << std::endl;
  //  if (rc != 0)
  //    throw std::runtime_error("Host " + m_host + "; failed remote command: " + command + "\nStandard output:\n" +
  //                             m_last_out + "\nStandard error:\n" + m_last_err);
  if (wait)
    this->wait();
  // std::cout << "wait=" << wait << ", running=" << running() << ", m_last_out=" << m_last_out
  // << ", m_last_err=" << m_last_err << "," << std::endl;
  if (!m_last_out.empty() && m_last_out[m_last_out.size() - 1] == '\n')
    m_last_out.resize(m_last_out.size() - 1);
  return m_last_out;
}

void Shell::wait(int min_wait_milliseconds, int max_wait_milliseconds) const {
  if (m_stdout_future_running)
    m_stdout_future.get();
  m_stdout_future_running = false;
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
  // if (m_process.running()) m_process.wait();
}

bool Shell::running() const {
  if (localhost() and m_job_number == 0)
    return m_process.running();
  bp::ipstream out;
  auto command = std::string{"ps -p "} + std::to_string(m_job_number) + " >/dev/null ; echo $?";
  auto proc = bp::child(std::vector<std::string>{"/bin/sh", "-c", command}, bp::std_out > out);
  proc.wait();
  std::string line;
  bool result = false;
  while (std::getline(out, line)) {
    result = std::stoi(line) == 0;
  }
  return result;
  // return (*this)(std::string{"ps -p "} + std::to_string(m_job_number) + " > /dev/null 2>/dev/null; echo $?") == "0";
}
bool Shell::local_asynchronous_supported() {
  // #ifdef WIN32
  //   return false;
  // #endif
  return true;
}
} // namespace sjef::util
