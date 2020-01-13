#include "sjef-c.h"
#include "sjef.h"
#include <iostream>
#include <string>
#include <iostream>
#include <boost/algorithm/string.hpp>
#include <map>
#include <array>
#include <regex>
#include "sjef-backend.h"
#include <boost/process/search_path.hpp>
#include <boost/process/child.hpp>
#include <boost/process/spawn.hpp>
#include <boost/process/args.hpp>
#include <boost/process/io.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/filesystem.hpp>
#include <pugixml.hpp>
#include <unistd.h>
#include <ctype.h>
#include <functional>
#include <chrono>
#include <thread>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/wait.h>
#endif
namespace fs = boost::filesystem;
static std::map<std::string, sjef::Project> projects;
struct sjef::pugi_xml_document : public pugi::xml_document {};

static void error(std::exception& e) {
  std::cerr << "Exception: " << e.what() << std::endl;
}

extern "C" {

int sjef_project_open(const char* project) {
  try {
    if (projects.count(project) > 0)
      throw std::runtime_error(std::string{"Attempt to open already-registered sjef_project "} + project);
    projects.emplace(std::make_pair(std::string{project}, sjef::Project(project)));
    return 1;
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return 0;
}
void sjef_project_close(const char* project) {
  try {
    if (projects.count(project) > 0) projects.erase(project);
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
}
int sjef_project_copy(const char* project, const char* destination_filename, int keep_hash) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    return (projects.at(project).copy(destination_filename, keep_hash != 0) ? 1 : 0);
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return 0;
}
int sjef_project_move(const char* project, const char* destination_filename) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    auto success = projects.at(project).move(destination_filename);
    sjef_project_close(project);
    return (success ? 1 : 0);
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return 0;
}
void sjef_project_erase(const char* project) {
  try {
    if (projects.count(project) != 0) sjef_project_close(project);
    fs::remove_all(sjef::Project(project, nullptr, true, false).filename());
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
}
int sjef_project_import(const char* project, const char* file) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    return (projects.at(project).import_file(file) ? 1 : 0);
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return 0;
}
int sjef_project_export(const char* project, const char* file) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    return (projects.at(project).export_file(file) ? 1 : 0);
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return 0;
}

int sjef_project_run_needed(const char* project) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    return (projects.at(project).run_needed() ? 1 : 0);
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return 0;
}

int sjef_project_synchronize(const char* project, const char* backend, int verbosity) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    return (projects.at(project).synchronize(backend, verbosity) ? 1 : 0);
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return 0;
}
int sjef_project_run(const char* project,
                     const char* backend,
                     const char* options,
                     int verbosity,
                     int force,
                     int wait) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    return (projects.at(project).run(backend,
                                     std::vector<std::string>(1, options),
                                     verbosity, force != 0, wait != 0) ? 1 : 0);
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return false;
}
int sjef_project_status(const char* project, int verbosity) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    return static_cast<int>(projects.at(project).status(verbosity));
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return 0;
}
void sjef_project_kill(const char* project) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    projects.at(project).kill();
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
}
void sjef_project_property_set(const char* project, const char* key, const char* value) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    projects.at(project).property_set(std::string{key}, std::string{value});
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
}
char* sjef_project_property_get(const char* project,
                                const char* key) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    return strdup(projects.at(project).property_get(std::string{key}).c_str());
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return NULL;
}
void sjef_project_property_delete(const char* project, const char* key) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    projects.at(project).property_delete(std::string{key});
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
}
void sjef_project_property_rewind(const char* project) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    projects.at(project).property_rewind();
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
}
char* sjef_project_property_next(const char* project) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    return strdup(projects.at(project).property_next().c_str());
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return NULL;
}
char* sjef_project_filename(const char* project) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    return strdup(projects.at(project).filename().c_str());
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return NULL;
}
char* sjef_project_name(const char* project) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    return strdup(projects.at(project).name().c_str());
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return NULL;
}
size_t sjef_project_project_hash(const char* project) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    return projects.at(project).project_hash();
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return 0;
}
size_t sjef_project_input_hash(const char* project) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    return projects.at(project).input_hash();
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return 0;
}
int sjef_project_recent_find(const char* filename) {
  try {
    return sjef::Project("",
                         nullptr,
                         true,
                         false,
                         fs::path{filename}.extension().string().substr(1)).recent_find(std::string(filename));
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return 0;
}

char* sjef_backend_value(const char* project, const char* backend, const char* key) {
  std::string backendName{((backend != nullptr and *backend != 0) ? backend : sjef::Backend::default_name)};
  try {
    auto& p = projects.at(project);
    return strdup(p.backend_get(backendName, key).c_str());
  }
  catch (const std::out_of_range& e) {
    return nullptr;
  }
}

char* sjef_project_backend_parameter_get(const char* project,
                                         const char* backend,
                                         const char* parameter) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    return strdup(projects.at(project).backend_parameter_get(backend, parameter).c_str());
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return NULL;
}

void sjef_project_backend_parameter_set(const char* project,
                                        const char* backend,
                                        const char* parameter,
                                        const char* value) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    projects.at(project).backend_parameter_set(backend, parameter, value);
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
}

void sjef_project_backend_parameter_delete(const char* project, const char* backend, const char* parameter) {
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    projects.at(project).backend_parameter_delete(backend, parameter);
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
}

char** sjef_project_backend_parameters(const char* project, const char* backend, int def) {
  char** result = NULL;
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    auto parameters = projects.at(project).backend_parameters(backend);
    result = (char**) malloc(sizeof(char*) * (parameters.size() + 1));
    size_t i = 0;
    for (const auto& p : parameters)
      result[i++] = strdup(def ? p.second.c_str() : p.first.c_str());
    result[i] = NULL;
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return result;
}

char** sjef_project_backend_names(const char* project) {
  char** result = NULL;
  try {
    if (projects.count(project) == 0) sjef_project_open(project);
    auto names = projects.at(project).backend_names();
    result = (char**) malloc(sizeof(char*) * (names.size() + 1));
    size_t i = 0;
    for (const auto& p : names)
      result[i++] = strdup(p.c_str());
    result[i] = NULL;
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return result;
}

char* sjef_project_recent(int number, const char* suffix) {
  return strdup(sjef::Project("$TMPDIR/.sjef.recent",
                              nullptr,
                              true,
                              false,
                              suffix).recent(number).c_str());
}
char* sjef_expand_path(const char* path, const char* default_suffix) {
  try {
    return strdup(sjef::expand_path(std::string{path}, std::string{default_suffix}).c_str());
  }
  catch (std::exception& e) { error(e); }
  catch (...) {}
  return NULL;
}
}