#include <sjef/util/Locker.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>
std::string msg() {
  std::stringstream s;
  using namespace std::chrono;
  auto timepoint = system_clock::now();
  auto coarse = system_clock::to_time_t(timepoint);
  auto fine = time_point_cast<std::chrono::milliseconds>(timepoint);

  char buffer[sizeof "9999-12-31 23:59:59.999"];
  std::snprintf(buffer + std::strftime(buffer, sizeof buffer - 3, "%F %T.", std::localtime(&coarse)), 4, "%03lu",
                static_cast<unsigned long>(fine.time_since_epoch().count() % 1000));

  s << buffer << " " << getpid();
  return s.str();
}
int main(int argc, char* argv[]) {
  std::string logfile(argc > 1 ? argv[1] : "");
  sjef::util::Locker locker(logfile + ".lock");
  auto bolt = locker.bolt();

  if (argc > 1)
    std::ofstream(argv[1], std::ofstream::app) << msg() << std::endl;
  else
    std::cout << msg() << std::endl;
  if (argc > 2) {
    auto ms = std::stoi(argv[2]);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    if (argc > 1)
      std::ofstream(argv[1], std::ofstream::app) << msg() << std::endl;
    else
      std::cout << msg() << std::endl;
  }
  return 0;
}