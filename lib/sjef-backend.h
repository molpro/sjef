#ifndef SJEF_BACKEND_H
#define SJEF_BACKEND_H

#include <stddef.h>
#include <pugixml.hpp>
#include "sjef.h"
namespace sjef {

class Backend {
 public:
  std::string name;
  std::string host;
  std::string cache;
  std::string run_command;
  std::string run_jobnumber;
  std::string status_command;
  std::string status_waiting;
  std::string status_running;
  std::string kill_command;
  static std::string default_name;
  static std::string dummy_name;
  Backend(std::string name = default_name,
          std::string host = "localhost",
          std::string cache = "${PWD}",
          std::string run_command = "sjef",
          std::string run_jobnumber = "([0-9]+)",
          std::string status_command = "/bin/ps -o pid,state -p",
          std::string status_running = "^S$",
          std::string status_waiting = "^[^SZ]$",
          std::string kill_command = "pkill -P")
      : name(std::move(name)),
        host(std::move(host)),
        cache(std::move(cache)),
        run_command(std::move(run_command)),
        run_jobnumber(std::move(run_jobnumber)),
        status_command(std::move(status_command)),
        status_running(std::move(status_running)),
        status_waiting(std::move(status_waiting)),
        kill_command(std::move(kill_command)) {
  }
  static const std::vector<std::string> s_keys ;
  std::string str() const;
};
}

#endif //SJEF_BACKEND_H
