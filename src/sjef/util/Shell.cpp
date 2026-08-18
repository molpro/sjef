#include "Shell.h"
#if __has_include(<boost/process/child.hpp>)
#include <boost/process/spawn.hpp>
#ifdef WIN32
#include <boost/process/windows.hpp>
#endif
#else
#include <boost/process/v1/spawn.hpp>
#ifdef WIN32
#include <boost/process/v1/windows.hpp>
#endif
#endif
#include <chrono>
#include <filesystem>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// Prevent Windows from auto-allocating a visible console window for each
// spawned child process (nohup, and the bash/mpiexec/etc it in turn execs)
// when the parent has no console of its own to inherit. Without this, every
// local job launch or quick synchronous shell call briefly flashes a new
// console window, and unreliable stdout/stderr redirection for such
// children is a known consequence of the same underlying Windows console
// allocation behaviour.
#ifdef WIN32
#define SJEF_NO_CONSOLE_WINDOW , bp::windows::create_no_window
#else
#define SJEF_NO_CONSOLE_WINDOW
#endif

namespace sjef::util {
static std::string executable(const std::string& command);
const std::string jobnumber_tag{"@@@JOBNUMBER"};
const std::string terminator{"@@@EOF"};

Shell::Shell(std::string host, std::string shell) : m_host(std::move(host)), m_shell((shell)) {
  // std::cout << "Shell() host=" << m_host << ", local? " << localhost() << ", shell=" << m_shell << std::endl;
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
        bp::child(ssh, m_host, std::move(shell), "-l", bp::std_in<m_in, bp::std_err> * m_err, bp::std_out > *m_out SJEF_NO_CONSOLE_WINDOW);
    //    std::cout << "ssh is"<<ssh<<", "<<m_process.valid()<<", "<<m_process.running()<<std::endl;
    if (!m_process.valid() || !m_process.running())
      throw Shell::runtime_error("Spawning run process has failed");
  }
}

///> @private
static std::string search_path_dirs(const std::string& path_string, const std::string& command) {
#ifdef WIN32
  constexpr char path_separator = ';';
  // Windows resolves a bare command name against PATHEXT-style extensions
  // (.exe, .bat, ...) -- that's what lets `where nohup` find nohup.exe.
  // std::filesystem does no such resolution on its own, so try the bare
  // name first, then with .exe appended, since that's what these
  // POSIX-style utilities (nohup, bash, ...) actually get installed as.
  const std::vector<std::string> candidates = {command, command + ".exe"};
#else
  constexpr char path_separator = ':';
  const std::vector<std::string> candidates = {command};
#endif
  std::stringstream path{path_string};
  std::string elem;
  while (std::getline(path, elem, path_separator)) {
    for (const auto& candidate : candidates) {
      auto resolved = fs::path{elem} / candidate;
      if (fs::is_regular_file(resolved))
        return resolved.string();
    }
  }
  return "";
}

static std::string executable(const std::string& command) {
  if (fs::path(command).is_absolute())
    return command;
#ifdef WIN32
  // A Unix-style absolute path (like sjef's own "/bin/bash" default shell)
  // has no drive letter, so it isn't caught by is_absolute() above -- but
  // its root directory still causes path::operator/ to discard the
  // left-hand side entirely whenever the right-hand side has a root
  // directory, so elem / "/bin/bash" collapses to just "/bin/bash" on
  // every iteration regardless of elem, and the search can never succeed.
  // Reduce it to its filename (e.g. "bash") before searching, since
  // that's genuinely what we're looking for on this platform.
  std::string search_name =
      (!command.empty() && command.front() == '/') ? fs::path(command).filename().string() : command;
#else
  const std::string& search_name = command;
#endif
  auto result = search_path_dirs(std::string{getenv("PATH")}, search_name);
  if (!result.empty())
    return result;
#ifdef WIN32
  // Git for Windows bundles POSIX utilities (nohup, bash, ...) in
  // <git-root>/usr/bin, which its own installer deliberately does not add
  // to PATH -- only <git-root>/cmd is added, to avoid flooding a user's
  // PATH with a parallel universe of Unix-named tools. Derive <git-root>
  // from wherever git.exe was actually found (git itself IS reliably on
  // PATH), rather than assuming a fixed install location like
  // "C:\Program Files\Git".
  auto git = search_path_dirs(std::string{getenv("PATH")}, "git");
  if (!git.empty()) {
    // git is at <git-root>/cmd/git.exe
    auto git_root = fs::path{git}.parent_path().parent_path();
    for (const auto& candidate : {search_name, search_name + ".exe"}) {
      auto resolved = git_root / "usr" / "bin" / candidate;
      if (fs::is_regular_file(resolved))
        return resolved.string();
    }
  }
#endif
  return "";
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
      m_process = bp::child(tokens, bp::std_out > *m_out, bp::std_err > *m_err SJEF_NO_CONSOLE_WINDOW);
    else
      m_process = bp::child(tokens, bp::std_out > out, bp::std_err > err SJEF_NO_CONSOLE_WINDOW);
    m_job_number = m_process.id();
  } else {
    auto shell_path = executable(m_shell);
    if (shell_path.empty())
      shell_path = m_shell; // preserve prior behaviour if resolution fails
    m_trace(2 - verbosity) << "launching shell local process: " << executable("nohup") << " " << shell_path << " -c "
                           << command << std::endl;
    std::string pipeline{std::regex_replace(command, std::regex{"'"}, "''")};
    // std::cout << "run_local_sync pipeline=" << pipeline << std::endl;
    // std::cout << "run_local_sync out=" << out << std::endl;
    // std::cout << "run_local_sync err=" << err << std::endl;
    if (out == "/dev/null" and err == "/dev/null")
      m_process = bp::child(executable("nohup"), shell_path, "-c", pipeline, bp::std_out > *m_out, bp::std_err > *m_err SJEF_NO_CONSOLE_WINDOW);
    else
      m_process = bp::child(executable("nohup"), shell_path, "-c", pipeline, bp::std_out > out, bp::std_err > err SJEF_NO_CONSOLE_WINDOW);
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
    m_job_number = 0;
    // std::cout << "finished wait , read output" << std::endl;
    // std::cout << "m_last_output " << m_last_out << std::endl;
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
      // std::cout << "capture_job_number_from_error, line=" << line << std::endl;
      std::smatch match;
      if (std::regex_search(line, match, std::regex{jobnumber_tag + "\\s*(\\d+)"})) {
        // std::cout << "capture_job_number_from_error, match=" << match[1] << std::endl;
        m_job_number = std::stoi(match[1]);
        // std::cout << "stoi survives, capture_job_number_from_error, match=" << m_job_number << std::endl;
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

  std::string pipeline{"(( " + std::regex_replace(command, std::regex{"'"}, "''") + ") 2>&1 & echo " + jobnumber_tag +
                       " $! 1>&2)"};
  m_out.reset(new bp::ipstream);
  m_err.reset(new bp::ipstream);
  // Resolve the shell to its full path ourselves, rather than relying on
  // nohup's own internal PATH-searching to find a bare name like "bash".
  // On a minimal/non-standard MSYS2-family environment (as opposed to a
  // full Git for Windows install), that internal resolution can fail
  // silently -- nohup starts fine (no exception here), but then can't
  // exec the shell, dies immediately, and produces no output, with the
  // failure visible only on nohup's own stderr (captured into *m_err,
  // not the run directory's output file). Handing nohup an absolute path
  // sidesteps its own internal search entirely.
  auto shell_path = executable(m_shell);
  if (shell_path.empty())
    shell_path = m_shell; // preserve prior behaviour if resolution fails
  m_trace(2 - verbosity) << "launching shell local process: " << executable("nohup") << " " << shell_path << " -c '"
                         << pipeline << "'" << std::endl;
  m_process = bp::child(executable("nohup"), shell_path, "-c", pipeline, bp::std_out > out, bp::std_err > *m_err SJEF_NO_CONSOLE_WINDOW);
  m_process.detach();
  capture_job_number_from_error(command);
  fs::current_path(current_path_save);
  if (!m_process.valid())
    throw Shell::runtime_error("Spawning run process has failed");
}

void Shell::run_remote_async(std::string command, const std::string& directory, int verbosity, const std::string& out,
                             const std::string& err) const {
  auto dir = std::regex_replace(directory, std::regex{" "}, "\\ ");
  // The redirection must be inside the "cd && ..." chain, applied to the command itself, not wrapped
  // around the whole "(cd dir && cmd)" subshell. "(cd dir && cmd) > out 2> err" sets up the redirection
  // before the subshell's cd has run, so relative "out"/"err" paths resolve against the shell's
  // original directory rather than "dir" -- silently sending stdout/stderr to $HOME instead of the run
  // directory, invisible whenever the job succeeds (its real output is Molpro's own .xml/.out files,
  // unaffected) but losing the only diagnostic evidence whenever the command fails outright.
  command =
      "(( cd ''" + dir + "'' && " + command + " > " + out + " 2> " + err + ") & echo " + jobnumber_tag + " $! 1>&2)";
  if (!m_process.valid() || !m_process.running())
    throw Shell::runtime_error("remote server process has died");
  try {
    m_in << std::string{"nohup /bin/sh -c '"} + command + "'" << std::endl;
    capture_job_number_from_error(command);
    // m_in << "echo '" << terminator << "'" << std::endl;
  } catch (const std::exception& e) {
    throw Shell::runtime_error((std::string{"Spawning run process has failed: "} + e.what()).c_str());
  }
  if (!m_process.valid() || !m_process.running())
    throw Shell::runtime_error("remote server process has died");
}
void Shell::run_remote_sync(std::string command, const std::string& directory, int verbosity,
                            const std::string& out) const {
  m_job_number = 0;
  auto dir = std::regex_replace(directory, std::regex{" "}, "\\ ");
  auto pipeline = "cd ''" + dir + "'' && " + command;
  if (out == "/dev/null") {
    command = pipeline + " 2>&1 ";
    // m_out.reset(new bp::ipstream);
  } else
    command = pipeline + " > " + out + " 2>&1 ";
  if (!m_process.valid() || !m_process.running())
    throw Shell::runtime_error("remote server process has died");
  try {
    m_in << command << std::endl;
    if (out == "/dev/null") {
      m_in << "echo '" << terminator << "'" << std::endl;
      capture_out();
    }
  } catch (const std::exception& e) {
    throw Shell::runtime_error((std::string{"Spawning run process has failed: "} + e.what()).c_str());
  }
  if (!m_process.valid() || !m_process.running())
    throw Shell::runtime_error("remote server process has died");
}

void Shell::capture_out() const {
  // std::cout << "capture_out" << m_last_out << std::endl;
  m_last_out.clear();
  std::string line;
  try {
    while (std::getline(*m_out, line) && line != terminator) {
      // std::cout << "capture_out, line=" << line << std::endl;
      m_last_out += line + '\n';
    }
  } catch (const std::exception& e) {
    throw Shell::runtime_error((std::string{"Shell() has failed whilst capturing the output.\nExit code: "} +
                                std::to_string(m_process.exit_code()) + "\n\nstdout:\n" + m_last_out +
                                "\nException thrown:" + e.what())
                                   .c_str());
  }
  // std::cout << "orphan line" << line << std::endl;
  // std::cout << "capture_out, m_last_out=" << m_last_out << std::endl;
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
    if (wait)
      run_remote_sync(command, directory, verbosity, out);
    else
      run_remote_async(command, directory, verbosity, out, err);
  }
  if (!m_last_out.empty() && m_last_out[m_last_out.size() - 1] == '\n')
    m_last_out.resize(m_last_out.size() - 1);
  // std::cout << "Shell::operator() returns m_last_out=" << m_last_out << std::endl;
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
  // if (m_process.running()) m_process.wait();
}

bool Shell::running() const {
  // std::cout << "running, m_job_number=" << m_job_number << localhost() << m_process.running() << std::endl;
  // if (localhost() and m_process.running()) return true;
  if (localhost() and m_job_number == 0)
    return m_process.running();
  bp::ipstream out;
  auto command = std::string{"ps -l -p "} + std::to_string(m_job_number) + "; echo $?";
  auto proc = bp::child(std::vector<std::string>{"/bin/sh", "-c", command}, bp::std_out > out SJEF_NO_CONSOLE_WINDOW);
  proc.wait();
  std::string line;
  bool result = false;
  while (std::getline(out, line)) {
    if (line.find(" " + std::to_string(m_job_number) + " ") != std::string::npos) {
      result = line.find(" Z") == std::string::npos;
    }
    if (line.size() == 1)
      result = result && line == "0";
  }
  return result;
}
bool Shell::local_asynchronous_supported() {
  // #ifdef WIN32
  //   return false;
  // #endif
  return true;
}
} // namespace sjef::util
