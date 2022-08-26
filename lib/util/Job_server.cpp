#include "Job_server.h"
#include "../sjef-backend.h"
#include <functional>
#include <future>

sjef::util::Job_server::Job_server(const sjef::Project& project, bool new_job) : m_project(project) {
  auto backend = m_project.property_get("backend");
}

std::string sjef::util::Job_server::remote_cache_directory() const {
  std::string result;
  auto backend = m_project.backends().at(m_project.property_get("backend"));
  const std::string rundir = m_project.filename("", "", 0).string();
  result = backend.cache + "/" + std::to_string(std::hash<std::string>{}(rundir));
  return result;
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
