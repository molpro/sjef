#include "sjef-c.h"
#include "sjef-backend.h"
#include "sjef.h"
#include <array>
#include <chrono>
#include <ctype.h>
#include <functional>
#include <iostream>
#include <map>
#include <pugixml.hpp>
#include <string>
#include <thread>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/wait.h>
#endif
namespace fs = std::filesystem;
static std::map<std::string, std::unique_ptr<sjef::Project>> projects;
struct sjef::pugi_xml_document : public pugi::xml_document {};

static void error(std::exception& e) { std::cerr << "Exception: " << e.what() << std::endl; }

extern "C" {

int sjef_project_open(const char* project) {
  try {
    if (projects.count(project) > 0)
      throw std::runtime_error(std::string{"Attempt to open already-registered sjef_project "} + project);
    projects.emplace(std::make_pair(std::string{project}, std::make_unique<sjef::Project>(project)));
    return 1;
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return 0;
}
void sjef_project_close(const char* project) {
  try {
    if (projects.count(project) > 0)
      projects.erase(project);
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
}
int sjef_project_copy(const char* project, const char* destination_filename, int keep_hash, int keep_run_directories) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return (projects.at(project)->copy(destination_filename, false, keep_hash != 0, false, keep_run_directories) ? 1 : 0);
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return 0;
}
int sjef_project_move(const char* project, const char* destination_filename) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    auto success = projects.at(project)->move(destination_filename);
    sjef_project_close(project);
    return (success ? 1 : 0);
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return 0;
}
void sjef_project_erase(const char* project) {
  try {
    if (projects.count(project) != 0)
      sjef_project_close(project);
    fs::remove_all(sjef::Project(project, false).filename());
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
}
int sjef_project_import(const char* project, const char* file) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return (projects.at(project)->import_file(file) ? 1 : 0);
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return 0;
}
int sjef_project_export(const char* project, const char* file) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return (projects.at(project)->export_file(file) ? 1 : 0);
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return 0;
}

int sjef_project_run_needed(const char* project) {
  try {
    constexpr bool debug = false;
    //    fprintf(stderr,"sjef_project_run_needed: need to open the project? %d\n",projects.count(project) == 0);
    clock_t start_time = clock();
    if (projects.count(project) == 0)
      sjef_project_open(project);
    if (debug) {
      int result = (projects.at(project)->run_needed() ? 1 : 0);
      fprintf(stderr, "sjef_project_run_needed() returns %d after %lu ms CPU\n", result,
              (clock() - start_time) * 1000 / CLOCKS_PER_SEC);
      return result;
    } else
      return (projects.at(project)->run_needed(0) ? 1 : 0);
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return 0;
}

int sjef_project_synchronize(const char* project, const char* backend, int verbosity) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    projects.at(project)->change_backend(backend);
    return (projects.at(project)->synchronize(verbosity) ? 1 : 0);
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return 0;
}
int sjef_project_run(const char* project, const char* backend, int verbosity, int force, int wait) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return (projects.at(project)->run(backend, verbosity, force != 0, wait != 0) ? 1 : 0);
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return false;
}
static int sjef_project_status_asynchronous(const char* project, int verbosity, int wait) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return static_cast<int>(projects.at(project)->status(verbosity, wait != 0));
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return 0;
}
int sjef_project_status(const char* project, int verbosity) {
  return sjef_project_status_asynchronous(project, verbosity, 1);
}
const char* sjef_project_status_message(const char* project, int verbosity) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return strdup(projects.at(project)->status_message(verbosity).c_str());
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return NULL;
}
int sjef_project_status_initiate(const char* project, int verbosity) {
  return sjef_project_status_asynchronous(project, verbosity, 0);
}
void sjef_project_kill(const char* project) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    projects.at(project)->kill();
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
}
void sjef_project_property_set(const char* project, const char* key, const char* value) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    projects.at(project)->property_set(std::string{key}, std::string{value});
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
}
void sjef_project_properties_set(const char* project, const char** key, const char** value) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    std::map<std::string, std::string> keyval;
    for (int i = 0; key[i] != NULL; ++i) {
      keyval[key[i]] = value[i];
    }
    projects.at(project)->property_set(keyval);
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
}
char* sjef_project_property_get(const char* project, const char* key) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return strdup(projects.at(project)->property_get(std::string{key}).c_str());
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return NULL;
}
char** sjef_project_properties_get(const char* project, const char** key) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    std::vector<std::string> keys;
    for (int i = 0; key[i] != nullptr; ++i)
      keys.push_back(key[i]);
    auto keyval = projects.at(project)->property_get(keys);
    char** result = (char**)malloc(keys.size() + 1);
    for (int i = 0; key[i] != nullptr; ++i) {
      result[i] = strdup((keyval.count(keys[i]) ? keyval.at(keys[i]) : "").c_str());
    }
    result[keys.size()] = nullptr;
    return result;
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return NULL;
}
void sjef_project_property_delete(const char* project, const char* key) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    projects.at(project)->property_delete(std::string{key});
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
}
char* sjef_project_filename(const char* project) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return strdup(projects.at(project)->filename().string().c_str());
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return NULL;
}
char* sjef_project_name(const char* project) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return strdup(projects.at(project)->name().c_str());
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return NULL;
}
size_t sjef_project_project_hash(const char* project) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return projects.at(project)->project_hash();
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return 0;
}
size_t sjef_project_input_hash(const char* project) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return projects.at(project)->input_hash();
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return 0;
}
int sjef_project_recent_find(const char* filename) {
  try {
    const char* dot;
    for (dot = filename + strlen(filename); dot > filename && *dot != '.'; dot--)
      ;
    return sjef::Project::recent_find(std::string{dot + 1}, filename);
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return 0;
}

char* sjef_backend_value(const char* project, const char* backend, const char* key) {
  std::string backendName{((backend != nullptr and *backend != 0) ? backend : sjef::Backend::default_name)};
  try {
    auto& p = projects.at(project);
    return strdup(p->backend_get(backendName, key).c_str());
  } catch (const std::out_of_range& e) {
    return nullptr;
  }
}

char* sjef_project_backend_parameter_documentation(const char* project, const char* backend, const char* parameter) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return strdup(projects.at(project)->backend_parameter_documentation(backend, parameter).c_str());
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return NULL;
}

char* sjef_project_backend_parameter_default(const char* project, const char* backend, const char* parameter) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return strdup(projects.at(project)->backend_parameter_default(backend, parameter).c_str());
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return NULL;
}

int sjef_project_change_backend(const char* project, const char* backend) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    projects.at(project)->change_backend(backend);
    return 1;
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return 0;
}

char* sjef_project_backend_parameter_get(const char* project, const char* backend, const char* parameter) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return strdup(projects.at(project)->backend_parameter_get(backend, parameter).c_str());
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return NULL;
}

char* sjef_project_backend_parameter_expand(const char* project, const char* backend, const char* templ) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return strdup(projects.at(project)->backend_parameter_expand(backend, templ).c_str());
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return NULL;
}

void sjef_project_backend_parameter_set(const char* project, const char* backend, const char* parameter,
                                        const char* value) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    projects.at(project)->backend_parameter_set(backend, parameter, value);
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
}

void sjef_project_backend_parameter_delete(const char* project, const char* backend, const char* parameter) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    projects.at(project)->backend_parameter_delete(backend, parameter);
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
}

char** sjef_project_backend_parameters(const char* project, const char* backend, int def) {
  char** result = NULL;
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    auto parameters = projects.at(project)->backend_parameters(backend);
    result = (char**)malloc(sizeof(char*) * (parameters.size() + 1));
    size_t i = 0;
    for (const auto& p : parameters)
      result[i++] = strdup(def ? p.second.c_str() : p.first.c_str());
    result[i] = NULL;
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return result;
}

char** sjef_project_backend_names(const char* project) {
  char** result = NULL;
  bool unopened = true;
  try {
    unopened = (projects.count(project) == 0);
    if (unopened)
      sjef_project_open(project);
    auto names = projects.at(project)->backend_names();
    result = (char**)malloc(sizeof(char*) * (names.size() + 1));
    size_t i = 0;
    for (const auto& p : names)
      result[i++] = strdup(p.c_str());
    result[i] = NULL;
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  if (unopened)
    sjef_project_close(project);
  return result;
}

char* sjef_project_recent(int number, const char* suffix) {
  return strdup(sjef::Project::recent(suffix, number).c_str());
}
char* sjef_expand_path(const char* path, const char* default_suffix) {
  try {
    return strdup(sjef::expand_path(path, default_suffix).string().c_str());
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return NULL;
}
char* sjef_project_filename_general(const char* project, const char* suffix, const char* name, int run) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return strdup(projects.at(project)->filename(suffix, name, run).string().c_str());
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return NULL;
}

char* sjef_project_run_directory(const char* project, int run) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return strdup(projects.at(project)->run_directory(run).string().c_str());
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return NULL;
}
int* sjef_project_run_list(const char* project) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    auto list = projects.at(project)->run_list();
    int* result = (int*)malloc((list.size() + 1) * sizeof(int*));
    int sequence = 0;
    for (const auto& item : list)
      result[sequence++] = item;
    result[sequence] = 0;
    return result;
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return NULL;
}
int sjef_project_run_directory_next(const char* project) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return projects.at(project)->run_directory_next();
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return 1;
}
void sjef_project_run_delete(const char* project, int run) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    projects.at(project)->run_delete(run);
    return;
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return;
}

void sjef_project_take_run_files(const char* project, int run, const char* fromname, const char* toname) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    projects.at(project)->take_run_files(run, fromname, toname);
    return;
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return;
}

void sjef_project_set_current_run(const char* project, unsigned int run) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    projects.at(project)->set_current_run(run);
    return;
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return;
}

unsigned int sjef_project_current_run(const char* project) {
  try {
    if (projects.count(project) == 0)
      sjef_project_open(project);
    return projects.at(project)->current_run();
  } catch (std::exception& e) {
    error(e);
  } catch (...) {
  }
  return 0;
}
}
