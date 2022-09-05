#ifndef SJEF_BACKEND_H
#define SJEF_BACKEND_H

#include <pugixml.hpp>
#include <stddef.h>
#include <string.h>
#include <string>
#include <vector>
namespace sjef {

class Backend {
public:
  std::string name;
  std::string host;
  std::string cache;
  std::string run_command;
  std::string run_jobnumber;
  std::string status_command;
  std::string status_running;
  std::string status_waiting;
  std::string kill_command;
  static std::string default_name;
  static std::string dummy_name;
  Backend(std::string name, std::string host, std::string cache, std::string run_command, std::string run_jobnumber,
          std::string status_command, std::string status_running, std::string status_waiting, std::string kill_command)
      : name(std::move(name)), host(std::move(host)), cache(std::move(cache)), run_command(std::move(run_command)),
        run_jobnumber(std::move(run_jobnumber)), status_command(std::move(status_command)),
        status_running(std::move(status_running)), status_waiting(std::move(status_waiting)),
        kill_command(std::move(kill_command)) {}
  // default constructor so that std::map::operator[ can be used
  Backend() {};
  struct Linux {};
  Backend(Linux x, std::string name = Backend::default_name, std::string host = "localhost",
          std::string cache = ".sjef/cache", std::string run_command = "dummy", std::string run_jobnumber = "([0-9]+)",
          std::string status_command = "/bin/ps -o pid,state -p", std::string status_running = "^ *[0-9][0-9]* *[DIRSTUtWx]",
          std::string status_waiting = " [Tt]", std::string kill_command = "pkill -P")
      : Backend(std::move(name), std::move(host), std::move(cache), std::move(run_command), std::move(run_jobnumber),
                std::move(status_command), std::move(status_running), std::move(status_waiting),
                std::move(kill_command)) {}

  struct Windows {};
  Backend(Windows x, std::string name = Backend::default_name, std::string host = "localhost",
          std::string cache = "sjef\\cache", std::string run_command = "dummy", std::string run_jobnumber = "([0-9]+)",
          std::string status_command = "tasklist /FO LIST /FI \"PID eq \"",
          std::string status_running = "^PID: *[0-9][0-9]* *[DIRSTUtWx]", std::string status_waiting = " ",
          std::string kill_command = "taskkill /f /PID ")
      : Backend(std::move(name), std::move(host), std::move(cache), std::move(run_command), std::move(run_jobnumber),
                std::move(status_command), std::move(status_running), std::move(status_waiting),
                std::move(kill_command)) {}
#ifdef WIN32
  using local = Windows;
#else
  using local = Linux;
#endif

  static const std::vector<std::string> s_keys;
  std::string str() const;
  static const std::vector<std::string>& keys();
};

} // namespace sjef

#endif // SJEF_BACKEND_H
