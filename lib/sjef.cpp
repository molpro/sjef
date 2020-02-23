#include "sjef.h"
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

namespace bp = boost::process;
namespace fs = boost::filesystem;

///> @private
struct sjef::pugi_xml_document : public pugi::xml_document {};

const std::string sjef::Project::s_propertyFile = "Info.plist";

inline fs::path executable(fs::path command) {
  if (command.is_absolute())
    return command;
  else
    return bp::search_path(command);
}

bool copyDir(
    fs::path const& source,
    fs::path const& destination,
    bool delete_source = false
) {
  // Check whether the function call is valid
  if (!fs::exists(source) || !fs::is_directory(source))
    throw std::runtime_error("Source directory " + source.string() + " does not exist or is not a directory.");
  if (fs::exists(destination))
    throw std::runtime_error("Destination directory " + destination.string() + " already exists.");
  // Create the destination directory
  if (!fs::create_directory(destination))
    throw std::runtime_error("Unable to create destination directory " + destination.string());
  // Iterate through the source directory
  for (
      fs::directory_iterator file(source);
      file != fs::directory_iterator(); ++file
      ) {
    fs::path current(file->path());
    if (fs::is_directory(current)) {
      // Found directory: Recursion
      if (
          !copyDir(
              current,
              destination / current.filename(),
              delete_source
          )
          ) {
        return false;
      }
    } else {
      // Found file: Copy
      fs::copy_file(
          current,
          destination / current.filename()
      );
    }
  }
  return true;
}

namespace sjef {
inline std::string getattribute(pugi::xpath_node node, std::string name) {
  return node.node().attribute(name.c_str()).value();
}
const std::vector<std::string> Project::suffix_keys{"inp", "out", "xml"};
Project::Project(const std::string& filename,
                 const Project* source,
                 bool erase_on_destroy,
                 bool construct,
                 const std::string& default_suffix,
                 const std::map<std::string, std::string>& suffixes) :
    m_project_suffix(get_project_suffix(filename, default_suffix)),
    m_filename(expand_path(filename, m_project_suffix)),
    m_reserved_files(std::vector<std::string>{"Info.plist"}),
    m_erase_on_destroy(erase_on_destroy),
    m_properties(std::make_unique<pugi_xml_document>()),
    m_suffixes(suffixes),
    m_backend_doc(std::make_unique<pugi_xml_document>()),
    m_status_lifetime(0),
    m_status_last(std::chrono::steady_clock::now()),
    m_status(unevaluated) {
//  std::cerr << "Project constructor filename="<<filename << "address "<< this<<std::endl;
  auto recent_projects_directory = expand_path(std::string{"~/.sjef/"} + m_project_suffix);
  fs::create_directories(recent_projects_directory);
  m_recent_projects_file = expand_path(recent_projects_directory + "/projects");
  for (const auto& key: suffix_keys)
    if (m_suffixes.count(key) < 1)
      m_suffixes[key] = key;
  if (suffixes.count("inp") > 0) m_reserved_files.push_back(this->filename("inp"));
  if (!fs::exists(m_filename))
    fs::create_directories(m_filename);
  else if (construct)
    m_erase_on_destroy = false;
//  std::cout << fs::system_complete(m_filename) << std::endl;
  if (!fs::exists(m_filename))
    throw std::runtime_error("project does not exist and could not be created: " + m_filename);
  if (!fs::is_directory(m_filename))
    throw std::runtime_error("project should be a directory: " + m_filename);

//  std::cerr << "constructor m_filename="<<m_filename<<" destroy="<<erase_on_destroy<<m_erase_on_destroy<<" construct="<<construct<<std::endl;
  if (!construct) return;
  if (!fs::exists(propertyFile())) {
    save_property_file();
    property_set("_private_sjef_project_backend_inactive", "1");
  }
  load_property_file();
  property_set("_private_sjef_project_backend_inactive_synced", "0");

  auto nimport = property_get("IMPORTED").empty() ? 0 : std::stoi(property_get("IMPORTED"));
//    std::cerr << "nimport "<<nimport<<std::endl;
  for (int i = 0; i < nimport; i++) {
//      std::cerr << "key "<<std::string{"IMPORT"}+std::to_string(i)<<", value "<<property_get(std::string{"IMPORT"}+std::to_string(i))<<std::endl;
    m_reserved_files.push_back(property_get(std::string{"IMPORT"} + std::to_string(i)));
  }
  recent_edit(m_filename);

  m_backends[sjef::Backend::default_name] =
      sjef::Backend(sjef::Backend::default_name, "localhost", "{$PWD}", m_project_suffix);
  m_backends[sjef::Backend::dummy_name] = sjef::Backend(sjef::Backend::dummy_name,
                                                        "localhost",
                                                        "{$PWD}",
                                                        "/bin/sh -c 'echo dummy > ${0%.*}.out; echo \"<?xml version=\\\"1.0\\\"?>\n<root/>\" > ${0%.*}.xml'");
  for (const auto& config_dir : std::vector<std::string>{"/usr/local/etc/sjef", "~/.sjef"}) {
    const auto config_file = expand_path(config_dir + "/" + m_project_suffix + "/backends.xml");
    if (fs::exists(config_file)) {
      m_backend_doc->load_file(config_file.c_str());
      auto backends = m_backend_doc->select_nodes("/backends/backend");
      for (const auto& be : backends) {
        auto kName = getattribute(be, "name");
        m_backends[kName] = Backend(kName);
        std::string kVal;
        if ((kVal = getattribute(be, "template")) != "") {
          m_backends[kName].host = m_backends[kVal].host;
          m_backends[kName].cache = m_backends[kVal].cache;
          m_backends[kName].run_command = m_backends[kVal].run_command;
          m_backends[kName].run_jobnumber = m_backends[kVal].run_jobnumber;
          m_backends[kName].status_command = m_backends[kVal].status_command;
          m_backends[kName].status_running = m_backends[kVal].status_running;
          m_backends[kName].status_waiting = m_backends[kVal].status_waiting;
          m_backends[kName].kill_command = m_backends[kVal].kill_command;
        }
        if ((kVal = getattribute(be, "host")) != "") m_backends[kName].host = kVal;
        if ((kVal = getattribute(be, "cache")) != "") m_backends[kName].cache = kVal;
        if ((kVal = getattribute(be, "run_command")) != "") m_backends[kName].run_command = kVal;
        if ((kVal = getattribute(be, "run_jobnumber")) != "") m_backends[kName].run_jobnumber = kVal;
        if ((kVal = getattribute(be, "status_command")) != "") m_backends[kName].status_command = kVal;
        if ((kVal = getattribute(be, "status_running")) != "") m_backends[kName].status_running = kVal;
        if ((kVal = getattribute(be, "status_waiting")) != "") m_backends[kName].status_waiting = kVal;
        if ((kVal = getattribute(be, "kill_command")) != "") m_backends[kName].kill_command = kVal;
      }
    }

  }
//  for (const auto& be : m_backends) std::cerr << "m_backend "<<be.first<<std::endl;

//std::cerr << "project constructor name()="<<name()<<std::endl;
  if (not name().empty() and name().front() != '.')
    change_backend(property_get("backend"));
}

Project::~Project() {
//  std::cerr << "destructor for project "<<name() << "address "<< this << std::endl;
//  std::cerr << "thread joinable? "<<m_backend_watcher.joinable()<<std::endl;
//  std::cerr << "shutdown_backend_watcher() starting" << std::endl;
  shutdown_backend_watcher();
//  std::cerr << "shutdown_backend_watcher() finished" << std::endl;
  properties_last_written_by_me(true);
  if (m_erase_on_destroy) erase();
//  std::cerr << "destructor for project "<<name() << "address "<< this << "finishes" << std::endl;
}

std::string Project::get_project_suffix(const std::string& filename, const std::string& default_suffix) const {
  auto suffix = fs::path{expand_path(filename, default_suffix)}.extension().string();
  if (suffix.empty())
    throw std::runtime_error(
        "Cannot deduce project suffix for \"" + filename + "\" with default suffix \"" + default_suffix + "\"");
  return suffix.substr(1);
}

bool Project::import_file(std::string file, bool overwrite) {
  auto to = fs::path{m_filename} / fs::path{file}.filename();
  for (const auto& key: suffix_keys)
    if (fs::path{file}.extension() == m_suffixes[key])
      to = fs::path{m_filename} / fs::path{name()} / m_suffixes[key];
  //TODO: implement generation of .inp from .xml
  boost::system::error_code ec;
//  std::cerr << "Import copies from "<<file<<" to "<<to<<std::endl;
  if (overwrite and exists(to))
    remove(to);
  fs::copy_file(file, to, ec);
  m_reserved_files.push_back(to.string());
  auto nimport = property_get("IMPORTED").empty() ? 0 : std::stoi(property_get("IMPORTED"));
  std::string key = "IMPORT" + std::to_string(nimport);
  property_set(key, to.filename().string());
  property_set("IMPORTED", std::to_string(nimport + 1));
  if (ec) throw std::runtime_error(ec.message());
  return true;
}

bool Project::export_file(std::string file, bool overwrite) {
  if (!property_get("backend").empty())synchronize(m_backends.at(property_get("backend")), 0);
  auto from = fs::path{m_filename};
  from /= fs::path{file}.filename();
  boost::system::error_code ec;
  if (overwrite and exists(fs::path{file}))
    remove(fs::path{file});
  fs::copy_file(from, file, ec);
  if (ec) throw std::runtime_error(ec.message());
  return true;
}

std::string Project::cache(const Backend& backend) const {
  return backend.cache + "/" + m_filename; // TODO: use boost::filesystem to avoid '/' on windows
}

bool Project::synchronize(std::string name, int verbosity) const {
  if (name.empty()) name = property_get("backend");
  if (name.empty()) name = sjef::Backend::default_name;
//  std::cerr << "name="<<name<<std::endl;
//  std::cerr << "m_backends.size()="<<m_backends.size()<<std::endl;
//  std::cerr << "m_backends.count(name)="<<m_backends.count(name)<<std::endl;
  if (m_backends.count(name) == 0)
    throw std::runtime_error("Non-existent backend " + name);
  return synchronize(m_backends.at(name), verbosity);
}

bool Project::synchronize(const Backend& backend, int verbosity, bool nostatus) const {
  if (verbosity > 1) std::cerr << "synchronize with " << backend.name << " (" << backend.host << ")" << std::endl;
  if (backend.host == "localhost") return true;
  if (verbosity > 1)
    std::cerr << "synchronize backend_inactive=" << property_get("_private_sjef_project_backend_inactive")
              << " backend_inactive_synced="
              << property_get("_private_sjef_project_backend_inactive_synced") << std::endl;
  // TODO check if any files have changed locally somehow. If they haven't, and backend_inactive_synced is set, then we could return immediately
  if (m_property_file_modification_time == fs::last_write_time(propertyFile())
    and property_get("_private_sjef_project_backend_inactive_synced") == "1") return true;
//  std::cerr << "really syncing"<<std::endl;
  //TODO: implement more robust error checking
  fs::path current_path_save;
  try {
    current_path_save = fs::current_path();
  }
  catch (...) {
    current_path_save = "";
  }
  fs::current_path(m_filename);
//  system(("ssh " + backend.host + " mkdir -p " + cache(backend)).c_str());
//  ensure_remote_server();
  m_remote_server.in <<  " mkdir -p " << cache(backend).c_str() << std::endl;
//  m_remote_server.in << "echo '@@@!!EOF'" << std::endl;
//  std::string line; while (std::getline(m_remote_server.out, line) && line != "@@@!!EOF");
  // absolutely send reserved files
  std::string rsync = "rsync";
  if (not this->m_control_path_option.empty())
    rsync += " -e 'ssh " + m_control_path_option + "'";
  if (verbosity > 2)
    std::cerr << "rsync: " << rsync << std::endl;
  bool send_only_reserved = false;
  if (send_only_reserved) { // 2020-01-29 not safe under all circumstances, and probably not faster
    std::string rfs;
    for (const auto& rf : m_reserved_files) {
      std::cerr << "reserved file pattern " << rf << std::endl;
      auto f = regex_replace(rf, std::regex(R"--(%)--"), name());
      std::cerr << "reserved file resolved " << f << std::endl;
      if (fs::exists(f))
        rfs += " " + f;
    }
    if (!rfs.empty())
      system((rsync + " -L -a " + (verbosity > 0 ? std::string{"-v "} : std::string{""}) + rfs + " " + backend.host
          + ":"
          + cache(backend)).c_str()); // TODO replace with bp::
    // send any files that do not exist on the backend yet
    system((rsync + " -L --ignore-existing -a " + (verbosity > 0 ? std::string{"-v "} : std::string{""}) + ". "
        + backend.host
        + ":"
        + cache(backend)).c_str()); // TODO replace with bp::
  } else {
    auto cmd =
        rsync + " -L -a --update " + (verbosity > 0 ? std::string{"-v "} : std::string{""}) + ". " + backend.host + ":"
            + cache(backend);
    if (verbosity > 1) std::cerr << cmd << std::endl;
    system(cmd.c_str()); // TODO replace with bp::
  }
  // fetch all newer files from backend
  if (property_get("_private_sjef_project_backend_inactive_synced") == "1") return true;
  auto cmd = rsync + " -a --update " + (verbosity > 0 ? std::string{"-v "} : std::string{""});
  for (const auto& rf : m_reserved_files) {
//    std::cerr << "reserved file pattern " << rf << std::endl;
    auto f = regex_replace(fs::path{rf}.filename().native(), std::regex(R"--(%)--"), name());
//    std::cerr << "reserved file resolved " << f << std::endl;
    if (fs::exists(f))
      cmd += "--exclude=" + f + " ";
  }
  cmd += backend.host + ":" + cache(backend) + "/ .";
  if (verbosity > 1) std::cerr << cmd << std::endl;
  system(cmd.c_str()); // TODO replace with bp::
  if (current_path_save != "")
    fs::current_path(current_path_save);
  if (not nostatus) // to avoid infinite loop with call from status()
    status(0); // to get backend_inactive
//  std::cerr << "synchronize backend_inactive=" << property_get("_private_sjef_project_backend_inactive") << std::endl;
  const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive_synced",
                                           property_get("_private_sjef_project_backend_inactive"));
//  std::cerr << "synchronize backend_inactive_synced=" << property_get("_private_sjef_project_backend_inactive_synced") << std::endl;
  return true;
}

void Project::force_file_names(const std::string& oldname) {

  fs::directory_iterator end_iter;
  for (fs::directory_iterator dir_itr(m_filename);
       dir_itr != end_iter;
       ++dir_itr) {
    auto path = dir_itr->path();
    try {
      if (path.stem() == oldname) {
        auto newpath = path.parent_path();
        newpath /= name();
        newpath.replace_extension(dir_itr->path().extension());
//        std::cerr << "rename(" << path.filename() << "," << newpath.filename() << ")" << std::endl;
        rename(path, newpath);

        if (newpath.extension() == ".inp")
          rewrite_input_file(newpath.string(), oldname);
      }
    }
    catch (const std::exception& ex) {
      throw std::runtime_error(dir_itr->path().leaf().native() + " " + ex.what());
    }
  }

  property_rewind();
  std::string key = property_next();
  for (; !key.empty(); key = property_next()) {
    auto value = property_get(key);
    boost::replace_first(value, oldname + ".", name() + ".");
    property_set(key, value);
  }
}

std::string Project::propertyFile() const { return (fs::path{m_filename} / fs::path{s_propertyFile}).string(); }

bool Project::move(const std::string& destination_filename, bool force) {
  auto stat = status(-1);
  if (stat == running or stat == waiting) return false;
  auto dest = fs::absolute(expand_path(destination_filename, fs::path{m_filename}.extension().string().substr(1)));
//  std::cerr << "move to "<<dest<<std::endl;
  if (force)
    fs::remove_all(dest);
  if (!property_get("backend").empty()) synchronize();
  auto namesave = name();
  auto filenamesave = m_filename;
  if (copyDir(fs::path(m_filename), dest, true)) {
    m_filename = dest.string();
    force_file_names(namesave);
    recent_edit(m_filename, filenamesave);
    return fs::remove_all(filenamesave) > 0;
  }
  return false;
}

bool Project::copy(const std::string& destination_filename, bool force, bool keep_hash) {
  auto dest = fs::absolute(expand_path(destination_filename, fs::path{m_filename}.extension().string().substr(1)));
  if (force)
    fs::remove_all(dest);
  if (!property_get("backend").empty()) synchronize();
  copyDir(fs::path(m_filename), dest, false);
  Project dp(dest.string());
  dp.force_file_names(name());
  recent_edit(dp.m_filename);
  dp.property_delete("jobnumber");
  dp.clean(true, true);
  if (!keep_hash)
    dp.property_delete("project_hash");
  return true;
}

void Project::erase() {
  shutdown_backend_watcher();
  if (fs::remove_all(m_filename)) {
    recent_edit("", m_filename);
    m_filename = "";
  }
}

static std::vector<std::string> splitString(std::string input, char c = ' ', char quote = '\'') {
  std::vector<std::string> result;
  const char* str0 = strdup(input.c_str());
  const char* str = str0;
  bool quoteActive = false;
  do {
    while (*str == c && *str) ++str;
    const char* begin = str;
//    if (*begin == quote) std::cerr << "opening quote found: " << begin << std::endl;
    while (*str && (*str != c || (*begin == quote && str > begin && *(str - 1) != quote))) ++str;
//    std::cerr << "rejecting " <<  *str << std::endl;
//    std::cerr << "taking " << std::string(begin, str) << std::endl;
    if (*begin == quote and str > begin + 1 and *(str - 1) == quote)
      result.push_back(std::string(begin + 1, str - 1));
    else
      result.push_back(std::string(begin, str));
    if (result.back().empty()) result.pop_back();
  } while (0 != *str++);
  free((void*) str0);
  return result;
}

std::string Project::backend_get(const std::string& backend, const std::string& key) const {
  auto& be = m_backends.at(backend);
  if (key == "name")
    return be.name;
  else if (key == "host")
    return be.host;
  else if (key == "cache")
    return be.cache;
  else if (key == "run_command")
    return be.run_command;
  else if (key == "run_jobnumber")
    return be.run_jobnumber;
  else if (key == "status_command")
    return be.status_command;
  else if (key == "status_waiting")
    return be.status_waiting;
  else if (key == "status_running")
    return be.status_running;
  else if (key == "kill_command")
    return be.kill_command;
  else
    throw std::out_of_range("Invalid key " + key);
}

std::string Project::backend_parameter_expand(const std::string& backend, const std::string& templ) {
  std::string output_text;
  std::regex re("\\{([^}]*)\\}");
  auto callback = [&](std::string m) {
    if (std::regex_match(m, re)) {
      m.pop_back();
      m.erase(0, 1);
      auto percent = m.find_first_of("%");
      if (percent == std::string::npos)
        throw std::runtime_error("Invalid template: " + templ + "\nMissing % in expression {" + m + "}");
      auto parameter_name = m.substr(percent + 1);
      auto defpos = parameter_name.find_first_of(":");
      std::string def;
      if (defpos != std::string::npos) {
        def = parameter_name.substr(defpos + 1);
        parameter_name.erase(defpos);
      }
      auto value = backend_parameter_get(backend, parameter_name);
      if (value.empty()) {
        if (not def.empty())
          output_text += m.substr(0, percent) + def;
      } else {
        output_text += m.substr(0, percent) + value;
      }
    } else
      output_text += m;
  };

  std::sregex_token_iterator
      begin(templ.begin(), templ.end(), re, {-1, 0}),
      end;
  std::for_each(begin, end, callback);
  return output_text;
}

std::map<std::string, std::string> Project::backend_parameters(const std::string& backend) const {
  std::map<std::string, std::string> result;

  auto templ = m_backends.at(backend).run_command;
  std::string output_text;
  std::regex re("\\{([^}]*)\\}");
  auto callback = [&](std::string m) {
    if (std::regex_match(m, re)) {
      m.pop_back();
      m.erase(0, 1);
      auto percent = m.find_first_of("%");
      if (percent == std::string::npos)
        throw std::runtime_error("Invalid template: " + templ + "\nMissing % in expression {" + m + "}");
      auto parameter_name = m.substr(percent + 1);
      auto defpos = parameter_name.find_first_of(":");
      std::string def;
      if (defpos != std::string::npos) {
        def = parameter_name.substr(defpos + 1);
        parameter_name.erase(defpos);
      }
      result[parameter_name] = def;
    }
  };

  std::sregex_token_iterator
      begin(templ.begin(), templ.end(), re, {-1, 0}),
      end;
  std::for_each(begin, end, callback);

  return result;
}

bool Project::run(std::string name, std::vector<std::string> options, int verbosity, bool force, bool wait) {
  auto& backend = m_backends.at(name);
  if (status(verbosity) != unknown && status(0) != completed) return false;
//  if (verbosity > 0)
//    std::cerr << "Project::run() run_needed()=" << run_needed(verbosity) << std::endl;
//  if (not force and not run_needed()) return false;
  property_set("backend", backend.name);
  fs::path current_path_save;
  try {
    current_path_save = fs::current_path();
  }
  catch (...) {
    current_path_save = "";
  }
  fs::current_path(m_filename);
  std::string line;
  bp::child c;
  std::string optionstring;
  for (const auto& o : options) optionstring += o + " ";
  property_set("run_options", optionstring);
//  std::cerr << "setting run_input_hash to input_hash()=" << input_hash() << std::endl;
  property_set("run_input_hash", std::to_string(input_hash()));
  if (verbosity > 0 and backend.name != sjef::Backend::dummy_name) optionstring += "-v ";
//  std::cerr << "backend.run_command before expand "<<backend.run_command<<std::endl;
  auto run_command = backend_parameter_expand(backend.name, backend.run_command);
//  std::cerr << "backend.run_command after expand "<<backend.run_command<<std::endl;
  if (backend.host == "localhost") {
    property_set("_private_sjef_project_backend_inactive", "0");
    property_set("_private_sjef_project_backend_inactive_synced", "0");
    if (verbosity > 0) std::cerr << "run local job, backend=" << backend.name << std::endl;
    auto spl = splitString(backend.run_command);
    run_command = spl.front();
//    spl.erase(spl.begin());
//    for (const auto& sp : spl)
    for (auto sp = spl.rbegin(); sp < spl.rend() - 1; sp++)
      optionstring = "'" + *sp + "' " + optionstring;
    if (verbosity > 1)
      std::cerr << executable(run_command) << " " << optionstring << " " << fs::path(this->name() + ".inp")
                << std::endl;
    if (verbosity > 2)
      for (const auto& o : splitString(optionstring))
        std::cerr << "option " << o << std::endl;
    if (optionstring.empty())
      c = bp::child(executable(run_command),
                    fs::path(this->name() + ".inp"));
    else
      c = bp::child(executable(run_command),
                    bp::args(splitString(optionstring)),
                    fs::path(this->name() + ".inp"));
    auto result = c.running();
    c.detach();
    property_set("jobnumber", std::to_string(c.id()));
    if (verbosity > 1) std::cerr << "jobnumber " << c.id() << ", running=" << c.running() << std::endl;
    fs::current_path(current_path_save);
    if (result and wait) this->wait();
    return result;
  } else { // remote host
    if (verbosity > 0) std::cerr << "run remote job on " << backend.name << std::endl;
    bp::ipstream c_err, c_out;
    synchronize(backend, verbosity);
    property_set("_private_sjef_project_backend_inactive", "0");
    property_set("_private_sjef_project_backend_inactive_synced", "0");
    if (verbosity > 3) std::cerr << "cache(backend) " << cache(backend) << std::endl;
    auto jobstring =
        "cd " + cache(backend) + "; nohup " + run_command + " " + optionstring
            + this->name()
            + ".inp& echo $! ";
    if (verbosity > 3) std::cerr << "jobstring " << jobstring << std::endl;
    c = bp::child(bp::search_path("ssh"), backend.host, jobstring, bp::std_err > c_err, bp::std_out > c_out);
    std::string sstdout, sstderr;
    if (verbosity > 2)
      std::cerr << "examine job submission output against regex: \"" << backend.run_jobnumber << "\"" << std::endl;
    while (std::getline(c_out, line)) {
      sstdout += line + "\n";
      std::smatch match;
      if (verbosity > 1)
        std::cerr << "\"" << line << "\"" << std::endl;
      if (std::regex_search(line, match, std::regex{backend.run_jobnumber})) {
        if (verbosity > 2)
          std::cerr << "... a match was found: " << match[1] << std::endl;
        if (verbosity > 1) status(verbosity - 2);
        property_set("jobnumber", match[1]);
        fs::current_path(current_path_save);
        if (wait) this->wait();
        return true;
      }
    }
    while (std::getline(c_err, line))
      sstderr += line + "\n";
//    std::cerr << "Remote job number not captured for backend \"" + backend.name + "\":\n" << sstdout << "\n" << sstderr
//              << std::endl;
  }
  if (current_path_save != "")
    fs::current_path(current_path_save);
  return false;
}

void Project::clean(bool oldOutput, bool output, bool unused) {
  if (oldOutput or output) {
    fs::remove_all(fs::path{filename()} / fs::path{name() + ".d"});
    for (int i = 0; i < 100; ++i) {
      fs::remove_all(fs::path{m_filename} / fs::path{name() + "." + m_suffixes["out"] + "_" + std::to_string(i)});
      fs::remove_all(fs::path{m_filename} / fs::path{name() + "." + m_suffixes["xml"] + "_" + std::to_string(i)});
    }
  }
  if (output) {
    fs::remove(fs::path{filename()} / fs::path{name() + "." + m_suffixes["out"]});
    fs::remove(fs::path{filename()} / fs::path{name() + "." + m_suffixes["xml"]});
  }
  if (unused)
    throw std::runtime_error("sjef::project::clean for unused files is not yet implemented");
}

void Project::kill() {
  if (property_get("backend").empty()) return;
  auto be = m_backends.at(property_get("backend"));
  auto pid = property_get("jobnumber");
  if (pid.empty()) return;
  if (be.host == "localhost") {
    auto spacepos = be.kill_command.find_first_of(" ");
    if (spacepos != std::string::npos)
      bp::spawn(executable(be.kill_command.substr(0, spacepos)),
                be.kill_command.substr(spacepos + 1, std::string::npos), pid);
    else
      bp::spawn(executable(be.kill_command), pid);
  } else {
//    std::cerr << "remote kill " << be.host << ":" << be.kill_command << ":" << pid << std::endl;
//    ensure_remote_server();
    m_remote_server.in << be.kill_command << " " << pid << std::endl;
    m_remote_server.in << "echo '@@@!!EOF'" << std::endl;
    std::string line;
    while (std::getline(m_remote_server.out, line) && line != "@@@!!EOF");
  }
}

bool Project::run_needed(int verbosity) const {
  auto start_time = std::chrono::steady_clock::now();
  if (verbosity > 0) std::cerr << "sjef::Project::run_needed, status=" << status() << std::endl;
  if (verbosity > 1)
    std::cerr << ", time " << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count() << std::endl;
  auto statuss = status();
  if (statuss == running or statuss == waiting) return false;
  auto inpfile = filename("inp");
  auto xmlfile = filename("xml");
  if (verbosity > 1)
    std::cerr << ", time " << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count() << std::endl;
  if (verbosity > 0)
    std::cerr << "sjef::Project::run_needed, input file exists ?=" << fs::exists(inpfile) << std::endl;
  if (verbosity > 1)
    std::cerr << ", time " << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count() << std::endl;
  if (not fs::exists(inpfile)) return false;
  if (verbosity > 0) std::cerr << "sjef::Project::run_needed, xml file exists ?=" << fs::exists(xmlfile) << std::endl;
  if (verbosity > 1)
    std::cerr << ", time " << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count() << std::endl;
  if (not fs::exists(xmlfile)) return true;
//  if (fs::last_write_time(xmlfile) < fs::last_write_time(inpfile)) return true;
  if (verbosity > 1)
    std::cerr << "sjef::Project::run_needed, time after initial checks "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - start_time).count() << std::endl;
//  if (verbosity > 3)
//    std::cerr << "sjef::Project::run_needed, input\n" << file_contents(m_suffixes.at("inp"))
//              << std::endl;
//  auto inputFromOutput = input_from_output();
//  if (verbosity > 1)
//    std::cerr << "after input_from_output"
//              << ", time " << std::chrono::duration_cast<std::chrono::milliseconds>(
//        std::chrono::steady_clock::now() - start_time).count()
//              << std::endl;
//  if (not inputFromOutput.empty()) {
//    if (verbosity > 1)
//      std::cerr << "sjef::Project::run_needed, input from output\n" << input_from_output() << std::endl;
//    if (std::regex_replace(file_contents("inp"), std::regex{" *\n\n*"}, "\n") != input_from_output()) return true;
//  }
  if (verbosity > 1)
    std::cerr << "before property_get, time " << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count() << std::endl;
  auto run_input_hash = property_get("run_input_hash");
  if (verbosity > 1)
    std::cerr << "after property_get, time " << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count() << std::endl;
  if (run_input_hash.empty()) { // if there's no input hash, look at the xml file instead
//      std::cerr << input_from_output() << std::endl;
//    std::cerr << file_contents("inp") << std::endl;
    if (verbosity > 1)
      std::cerr << "There's no run_input_hash, so compare output and input: " <<
                (std::regex_replace(file_contents("inp"), std::regex{" *\n\n*"}, "\n") != input_from_output())
                << std::endl;
    return (std::regex_replace(file_contents("inp"), std::regex{" *\n\n*"}, "\n") != input_from_output());
  }
  {
    if (verbosity > 1)
      std::cerr << "sjef::Project::run_needed, input_hash =" << input_hash() << std::endl;
    std::stringstream sstream(run_input_hash);
    size_t i_run_input_hash;
    sstream >> i_run_input_hash;
    if (verbosity > 1)
      std::cerr << "sjef::Project::run_needed, run_input_hash =" << i_run_input_hash << std::endl;
    if (verbosity > 1) {
      std::cerr << "sjef::Project::run_needed, input hash matches ?=" << (i_run_input_hash
          == input_hash()) << std::endl;
//      std::cerr << "ending"
//                << ", time " << std::chrono::duration_cast<std::chrono::milliseconds>(
//          std::chrono::steady_clock::now() - start_time).count()
//                << std::endl;
    }
    if (i_run_input_hash != input_hash()) {
      if (verbosity > 1) {
        std::cerr << "sjef::Project::run_needed returning true" << std::endl;
        std::cerr << "ending time " << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count()
                  << std::endl;
        std::cerr << "because i_run_input_hash != input_hash()" << std::endl;
      }
      return true;
    }
  }
  if (verbosity > 1) {
    std::cerr << "sjef::Project::run_needed returning false" << std::endl;
    std::cerr << "ending"
              << ", time " << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count()
              << std::endl;
  }
  return false;
}

std::string Project::xml() const {
  return xmlRepair(file_contents(m_suffixes.at("xml")));
}

std::string Project::file_contents(const std::string& suffix, const std::string& name, bool sync) const {

//  std::cerr << "file_contents " << suffix << ": " << property_get("_private_sjef_project_backend_inactive_synced")
//            << std::endl;
  auto be = property_get("backend");
  if (sync
      and not be.empty()
      and be != "local"
      and property_get("_private_sjef_project_backend_inactive_synced") != "1"
      )
    synchronize(be);
  std::ifstream s(filename(suffix, name));
  auto result = std::string(std::istreambuf_iterator<char>(s),
                            std::istreambuf_iterator<char>());
  while (result.back() == '\n') result.pop_back();
  return result;
}

status Project::status(int verbosity, bool cached) const {
//  if (cached and m_status != unevaluated)
//    std::cerr << "using cached status " << m_status << ", so returning immediately" << std::endl;
//  else if (cached)
//    std::cerr << "want to use cached status but cannot because not yet evaluated " << m_status << std::endl;
//  else
//    std::cerr << "do not want to use cached status " << m_status << std::endl;
  if (cached and m_status != unevaluated) return m_status;
  if (property_get("backend").empty()) return unknown;
  auto start_time = std::chrono::steady_clock::now();
  auto bes = property_get("backend");
  if (bes.empty()) bes = sjef::Backend::default_name;
  if (bes.empty() or m_backends.count(bes) == 0)
    throw std::runtime_error("Invalid backend: " + bes);
  auto be = m_backends.at(bes);
  if (verbosity > 1)
    std::cerr << "status() backend:\n====" << be.str() << "\n====" << std::endl;
  auto pid = property_get("jobnumber");
  if (verbosity > 1)
    std::cerr << "job number " << pid << std::endl;
  if (pid.empty() or std::stoi(pid) < 0) {
    auto ih = std::to_string(input_hash());
    auto rih = property_get("run_input_hash");
//    std::cerr << ih << " : " << rih<<std::endl;
    if (! rih.empty()) return (rih == ih) ? completed : unknown;
    return (std::regex_replace(file_contents("inp"), std::regex{" *\n\n*"}, "\n") != input_from_output())
    ? unknown : completed;
  }
//  std::cerr << "did not return unknown for empty pid "<<pid << std::endl;
  const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive", "0");
  if (property_get("_private_sjef_project_completed_job") == be.host + ":" + pid) {
//      std::cerr << "status return complete because _private_sjef_project_completed_job is valid"<<std::endl;
    const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive", "1");
    return completed;
  }
  auto result = pid == "" ? unknown : completed;
  if (be.host == "localhost") {
    auto spacepos = be.status_command.find_first_of(" ");
    bp::child c;
    bp::ipstream is;
    std::string line;
    auto cmd = splitString(be.status_command + " " + pid);
    c = bp::child(
        executable(cmd[0]), bp::args(std::vector<std::string>{cmd.begin() + 1, cmd.end()}),
        bp::std_out > is);
    while (std::getline(is, line)) {
      while (isspace(line.back())) line.pop_back();
//      std::cout << "line: @"<<line<<"@"<<std::endl;
      if ((" " + line).find(" " + pid + " ") != std::string::npos) {
        if (line.back() == '+') line.pop_back();
        if (line.back() == 'Z') { // zombie process
#if defined(__linux__) || defined(__APPLE__)
          waitpid(std::stoi(pid), NULL, WNOHANG);
#endif
          result = completed;
        }
        result = running;
      }
    }
  } else {
    if (verbosity > 1)
      std::cerr << "remote status " << be.host << ":" << be.status_command << ":" << pid << std::endl;
//    ensure_remote_server();
    m_remote_server.in << be.status_command << " " << pid << std::endl;
    m_remote_server.in << "echo '@@@!!EOF'" << std::endl;
    if (verbosity > 2)
      std::cerr << "sending " << be.status_command << " " << pid << std::endl;
    std::string line;
    while (std::getline(m_remote_server.out, line)
        && line != "@@@!!EOF"
        ) {
      if (verbosity > 0) std::cerr << "line received: " << line << std::endl;
      if ((" " + line).find(" " + pid + " ") != std::string::npos) {
        std::smatch match;
        if (verbosity > 2) std::cerr << "line" << line << std::endl;
        if (verbosity > 2) std::cerr << "status_running " << be.status_running << std::endl;
        if (verbosity > 2) std::cerr << "status_waiting " << be.status_waiting << std::endl;
        if (std::regex_search(line, match, std::regex{be.status_running})) {
          result = running;
        }
        if (std::regex_search(line, match, std::regex{be.status_waiting})) {
          result = waiting;
        }
      }
    }
  }
  if (verbosity > 2) std::cerr << "received status " << result << std::endl;
  if (result == completed) {
//    const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive_synced", property_get("_private_sjef_project_backend_inactive"));
    const_cast<Project*>(this)->property_set("_private_sjef_project_completed_job", be.host + ":" + pid);
  }
  if (result != unknown) {
//    std::cerr << "result is not unknown, but is " << result << std::endl;
    if (result == running)
      const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive_synced", "0");
    const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive", result == completed ? "1" : "0");
    goto return_point;
  }
  if (verbosity > 2) std::cerr << "fallen through loop" << std::endl;
  synchronize(be, verbosity, true);
  const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive",
                                           (result != completed or
                                               fs::exists(fs::path{filename()} / fs::path{name()
                                                                                              + "."
                                                                                              + m_suffixes.at("xml")}) // there may be a race, where the job status is completed, but the output file is not yet there. This is an imperfect test for that .. just whether the .xml exists at all. TODO: improve test for complete output file
                                           )
                                           ? "1" : "0");
  return_point:
  auto
      time_taken = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
  if (time_taken < m_status_lifetime) m_status_lifetime = time_taken;
  m_status_last = std::chrono::steady_clock::now();
  m_status = result;
//  std::cerr << "m_status set to " << m_status << std::endl;
  return result;
}

std::string sjef::Project::status_message(int verbosity) const {
  std::map<sjef::status, std::string> message;
  message[sjef::status::unknown] = "Not found";
  message[sjef::status::running] = "Running";
  message[sjef::status::waiting] = "Waiting";
  message[sjef::status::completed] = "Completed";
  message[sjef::status::unevaluated] = "Unevaluated";
  auto statu = this->status(verbosity);
  auto result = message[statu];
  if (statu != sjef::status::unknown && !property_get("jobnumber").empty())
    result +=", job number " + property_get("jobnumber") + " on backend "
              + property_get("backend");
  return result;
}

void Project::wait(unsigned int maximum_microseconds) const {
  unsigned int microseconds = 1;
  while (status() != completed) {
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
    if (microseconds < maximum_microseconds) microseconds *= 2;
  }
}

static int property_sequence;
void Project::property_rewind() {
  property_sequence = 0;
}

void Project::property_delete(const std::string& property, bool save) {
  check_property_file();
//  std::cerr << "property_delete " << property << " save=" << save << std::endl;
  if (!m_properties->child("plist")) m_properties->append_child("plist");
  if (!m_properties->child("plist").child("dict")) m_properties->child("plist").append_child("dict");
  auto dict = m_properties->child("plist").child("dict");
  std::string query{"//key[text()='" + property + "']"};
  auto nodes = dict.select_nodes(query.c_str());
  for (const auto& keynode : nodes) {
    auto valnode = keynode.node().select_node("following-sibling::string[1]");
    dict.remove_child(keynode.node());
    dict.remove_child(valnode.node());
  }
  if (save)
    save_property_file();
}

void Project::property_set(const std::string& property, const std::string& value, bool save) {
//  std::cerr << "property_set " << property << " = " << value << std::endl;
  property_delete(property, false);
  {
    std::lock_guard<std::mutex> guard(m_unmovables.m_property_set_mutex);
    if (!m_properties->child("plist")) m_properties->append_child("plist");
    if (!m_properties->child("plist").child("dict")) m_properties->child("plist").append_child("dict");
    auto keynode = m_properties->child("plist").child("dict").append_child("key");
    keynode.text() = property.c_str();
    auto stringnode = m_properties->child("plist").child("dict").append_child("string");
    stringnode.text() = value.c_str();
    if (save)
      save_property_file();
  }
}

std::string Project::property_get(const std::string& property) const {
  std::string query{"/plist/dict/key[text()='" + property + "']/following-sibling::string[1]"};
  check_property_file();
  return m_properties->select_node(query.c_str()).node().child_value();
}

std::string Project::property_next() {
  property_sequence++;
  std::string query{"/plist/dict/key[position()='" + std::to_string(property_sequence) + "']"};
  return m_properties->select_node(query.c_str()).node().child_value();
}

void Project::recent_edit(const std::string& add, const std::string& remove) {
  bool changed = false;
  {
    if (!fs::exists(m_recent_projects_file)) {
      fs::create_directories(fs::path(m_recent_projects_file).parent_path());
      fs::ofstream junk(m_recent_projects_file);
    }
    std::ifstream in(m_recent_projects_file);
    std::ofstream out(m_recent_projects_file + "-");
    size_t lines = 0;
    if (!add.empty()) {
      out << add << std::endl;
      changed = true;
      ++lines;
    }
    std::string line;
    while (in >> line && lines < recentMax) {
      if (line != remove && line != add && fs::exists(line)) {
        ++lines;
        out << line << std::endl;
      } else
        changed = true;
    }
    changed = changed or lines >= recentMax;
  }
  if (changed)
    fs::rename(m_recent_projects_file + "-", m_recent_projects_file);
  else
    fs::remove(m_recent_projects_file + "-");
}

std::string Project::filename(std::string suffix, const std::string& name) const {
  if (m_suffixes.count(suffix) > 0) suffix = m_suffixes.at(suffix);
  if (suffix != "" and name == "")
    return m_filename + "/" + fs::path(m_filename).stem().string() + "." + suffix;
  else if (suffix != "" and name != "")
    return m_filename + "/" + name + "." + suffix;
  else if (name != "")
    return m_filename + "/" + name;
  else
    return m_filename;
}
std::string Project::name() const { return fs::path(m_filename).stem().string(); }

int Project::recent_find(const std::string& filename) const {
  std::ifstream in(m_recent_projects_file);
  std::string line;
  for (int position = 1; in >> line; ++position) {
    if (fs::exists(line)) {
      if (line == filename) return position;
    } else
      --position;
  }
  return 0;
}

std::string Project::recent(int number) const {
  std::ifstream in(m_recent_projects_file);
  std::string line;
  for (int position = 0; in >> line;) {
    if (fs::exists(line)) ++position;
    if (position == number) return line;
  }
  return "";

}

///> @private
struct plist_writer : pugi::xml_writer {
  std::string file;
  virtual void write(const void* data, size_t size) {
    std::ofstream s(file);
    s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<!DOCTYPE plist SYSTEM '\"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"'>\n"
      << std::string{static_cast<const char*>(data), size};
  }
};

constexpr static bool use_writer = false;

std::mutex load_property_file_mutex;
void Project::load_property_file() const {
  const std::lock_guard<std::mutex> lock(load_property_file_mutex);
  if (!m_properties->load_file(propertyFile().c_str()))
    throw std::runtime_error("error in loading " + propertyFile());
  m_property_file_modification_time = fs::last_write_time(propertyFile());
}

static std::string writing_object_file = ".Info.plist.writing_object";

bool Project::properties_last_written_by_me(bool removeFile) const {
  auto path = fs::path{m_filename} / fs::path{writing_object_file};
  std::ifstream i{path.string(), std::ios_base::in};
  if (not i.is_open()) return false;
  std::hash<const Project*> hasher;
  auto me = hasher(this);
  std::hash<const Project*>::result_type writer;
  i >> writer;
  if (removeFile and writer == me)
    fs::remove(path);
  return writer == me;

}
void Project::check_property_file() const {
  auto lastwrite = fs::last_write_time(propertyFile());
  if (m_property_file_modification_time == lastwrite) { // tie-breaker
    if (not properties_last_written_by_me(false))
      --m_property_file_modification_time; // to mark this object's cached properties as out of date
  }
  if (m_property_file_modification_time < lastwrite) {
    {
      load_property_file();
    }
    m_property_file_modification_time = lastwrite;
  }
}

std::mutex save_property_file_mutex;
void Project::save_property_file() const {
  const std::lock_guard<std::mutex> lock(save_property_file_mutex);
//  std::cout << "save_property_file" << std::endl;
//  system((std::string{"cat "} + propertyFile()).c_str());
  struct plist_writer writer;
  writer.file = propertyFile();
  try {
    boost::interprocess::file_lock fileLock{writer.file.c_str()};
    fileLock.lock();
    if (use_writer)
      m_properties->save(writer, "\t", pugi::format_no_declaration);
    else
      m_properties->save_file(propertyFile().c_str());
  }
  catch (boost::interprocess::interprocess_exception& x) {
    if (use_writer)
      m_properties->save(writer, "\t", pugi::format_no_declaration);
    else
      m_properties->save_file(propertyFile().c_str());
  }
//  system((std::string{"cat "} + propertyFile()).c_str());
//  std::cout << "end save_property_file" << std::endl;
  m_property_file_modification_time = fs::last_write_time(propertyFile());
  {
    std::ofstream o{((fs::path{m_filename} / fs::path{writing_object_file}).string())};
    std::hash<const Project*> hasher;
    o << hasher(this);
  }
}

///> @private
inline std::string random_string(size_t length) {
  auto randchar = []() -> char {
    const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    return charset[rand() % max_index];
  };
  std::string str(length, 0);
  std::generate_n(str.begin(), length, randchar);
  return str;
}

size_t sjef::Project::project_hash() {
  auto p = this->property_get("project_hash");
  size_t result;
  if (p.empty()) {
    result = std::hash<std::string>{}(random_string(32));
    this->property_set("project_hash", std::to_string(result));
  } else {
    std::istringstream iss(p);
    iss >> result;
  }
  return result;
}

size_t sjef::Project::input_hash() const {
  constexpr bool debug = false;
  if (debug) {
    std::cerr << "input_hash" << filename("inp") << std::endl;
    std::ifstream ss(filename("inp"));
    std::cerr << "unmodified file\n" << std::string((std::istreambuf_iterator<char>(ss)),
                                                    std::istreambuf_iterator<char>()) << std::endl;
  }
  std::ifstream ss(filename("inp"));
  std::string line;
  std::string input;
  while (std::getline(ss, line))
    input += referenced_file_contents(line) + "\n";
  if (debug)
    std::cerr << "rewritten input " << input << std::endl;
  return std::hash<std::string>{}(input);
}

///> @private
inline std::string environment(std::string key) {
  char* s = std::getenv(key.c_str());
  if (s == nullptr) {
    if (key == "TMPDIR")
      return "/tmp";
    throw std::runtime_error("Unknown environment variable " + key);
  }
  return s;
}

///> @private
std::string expand_path(const std::string& path, const std::string& suffix) {
  auto text = path;
  // specials
#ifdef _WIN32
  text = std::regex_replace(text,std::regex{R"--(\~)--"},environment("USERPROFILE"));
  text = std::regex_replace(text,std::regex{R"--(\$\{HOME\})--"},environment("USERPROFILE"));
  text = std::regex_replace(text,std::regex{R"--(\$HOME/)--"},environment("USERPROFILE")+"/");
  text = std::regex_replace(text,std::regex{R"--(\$\{TMPDIR\})--"},environment("TEMP"));
  text = std::regex_replace(text,std::regex{R"--(\$TMPDIR/)--"},environment("TEMP")+"/");
#else
  text = std::regex_replace(text, std::regex{R"--(\~)--"}, environment("HOME"));
#endif
  // expand environment variables
  std::smatch match;
  while (std::regex_search(text, match, std::regex{R"--(\$([^{/]+)/)--"}))
    text.replace((match[0].first - text.cbegin()), (match[0].second - text.cbegin()), environment(match[1]) + "/");
  while (std::regex_search(text, match, std::regex{R"--(\$\{([^}]+)\})--"}))
    text.replace((match[0].first - text.cbegin()), (match[0].second - text.cbegin()), environment(match[1]));
  // replace separators by native form
  text = std::regex_replace(text, std::regex{R"--([/\\])--"}, std::string{fs::path::preferred_separator});
  // resolve relative path
  if (text[0] != fs::path::preferred_separator and text[1] != ':') {
    text = (fs::current_path() / text).string();
  }
  // remove any trailing slash
  if (text.back() == fs::path::preferred_separator) text.pop_back();
  // add suffix
//  std::cerr << "before suffix add: " << text << std::endl;
  if (fs::path{text}.extension().native() != std::string{"."} + suffix and !suffix.empty()) text += "." + suffix;
//  std::cerr << "after suffix add: " << text << std::endl;
  return text;
}

std::string xmlRepair(const std::string& source,
                      const std::map<std::string, std::string>& injections
) {
  if (source.empty()) { // empty source. Construct valid xml
    return std::string("<?xml version=\"1.0\"?><root/>");
  }
  std::vector<std::string> nodes;
  bool commentPending = false;
  auto s = source.begin();
  std::smatch match;
  while (std::regex_search(s, source.end(), match, std::regex("<[^>]*[^-]>|<!--|-->"))) {
    const auto& match0 = match[0];
    auto pattern = std::string{match0.first, match0.second};
    if (pattern.substr(pattern.length() - 2) == "/>") {
    } else if (pattern[1] == '/') { // no checking done that it's the right node
      if (!nodes.empty()) nodes.pop_back();
    } else if (pattern.substr(0, 4) == "<!--") {
      commentPending = true;
    } else if (pattern.find("-->") != std::string::npos) {
      commentPending = false;
    } else if (pattern.size() > 1 && pattern[0] == '<' && (pattern[1] != '?') && !commentPending) {
      std::smatch matchnode;
      if (not std::regex_search(pattern, matchnode, std::regex("<([^> /]*)")) or matchnode.size() != 2)
        throw std::logic_error("bad stuff in parsing xml node");
      nodes.push_back({matchnode[1].first, matchnode[1].second});
    }
    s = match0.second;
  }

  auto result = source;
  if (std::string{s, source.end()}.find('<')
      != std::string::npos) /* fix broken tags due to e.g. full disk such as: <parallel proces */
    result.erase(source.find_last_of('<'));
  if (commentPending) result += "-->";
  for (auto node = nodes.rbegin(); node != nodes.rend(); node++) {
    for (const auto& injection : injections)
      if (*node == injection.first)
        result += injection.second;
    result += "</" + *node + ">";
  }
  return result;
}

std::vector<std::string> sjef::Project::backend_names() const {
  std::vector<std::string> result;
  for (const auto& be : this->m_backends)
    result.push_back(be.first);
  return result;
}

void sjef::Project::ensure_remote_server() const {
//  std::cerr << "ensure_remote_server called"<<std::endl;
  if (m_remote_server.process.running()
      and m_remote_server.host == this->backend_get(property_get("backend"), "host")
      )
    return;
//  std::cerr << "ensure_remote_server fires"<<std::endl;
  fs::path control_path_directory = expand_path("$HOME/.sjef/.ssh/ctl");
  fs::create_directories(control_path_directory);
  auto control_path_option = "-o ControlPath=\"" + (control_path_directory / "%L-%r@%h:%p").string() + "\"";
  auto backend = property_get("backend");
  if (backend.empty()) backend = sjef::Backend::default_name;
  m_remote_server.host = this->backend_get(backend, "host");
  if (m_remote_server.host == "localhost") return;
//  std::cerr << "ssh " + control_path_option + " -O check " + m_remote_server.host << std::endl;
  auto c = boost::process::child("ssh " + control_path_option + " -O check " + m_remote_server.host,
                                 bp::std_out > bp::null,
                                 bp::std_err > bp::null);
  c.wait();
//  if (c.exit_code() != 0)
//    std::cerr << "Attempting to start ssh ControlMaster" << std::endl;
//  else
//    std::cerr << "ssh ControlMaster already running" << std::endl;
//  if (c.exit_code() != 0)
//    std::cerr
//        << "ssh -nNf " + control_path_option + " -o ControlMaster=yes -o ControlPersist=60 " + m_remote_server.host
//        << std::endl;
  if (c.exit_code() != 0)
    c = boost::process::child(
        "ssh -nNf " + control_path_option + " -o ControlMaster=yes -o ControlPersist=60 " + m_remote_server.host,
        bp::std_out > bp::null,
        bp::std_err > bp::null);
  c.wait();
  m_control_path_option = (c.exit_code() == 0) ? control_path_option : "";

//  std::cerr << "Start remote_server " << std::endl;
  m_remote_server.process.terminate();
  m_remote_server.process = bp::child(bp::search_path("ssh"),
                                      m_control_path_option,
                                      m_remote_server.host,
                                      bp::std_in < m_remote_server.in,
                                      bp::std_err > m_remote_server.err,
                                      bp::std_out > m_remote_server.out);
//  std::cerr << "started remote_server "<<std::endl;
}

void sjef::Project::shutdown_backend_watcher() {
  m_unmovables.shutdown_flag.test_and_set();
//  std::cerr << "shutdown_backend_watcher for project at " << this << " joinable=" << m_backend_watcher.joinable()
//            << std::endl;
  if (m_backend_watcher.joinable())
    m_backend_watcher.join();
//  std::cerr << "shutdown_backend_watcher for project at " << this << " is complete " << std::endl;
}

//TODO this function is just for debugging and should be tidied away when done
void sjef::Project::report_shutdown(const std::string& message) const {
  auto value = m_unmovables.shutdown_flag.test_and_set();
  if (!value) m_unmovables.shutdown_flag.clear();
  std::cerr << "report_shutdown " << message << ": " << value << std::endl;
}

void sjef::Project::change_backend(std::string backend) {
  if (backend.empty()) backend = sjef::Backend::default_name;
//  std::cerr << "change_backend " << backend << "for project " << name() <<" at address "<<this<< std::endl;
  property_set("backend", backend);
  shutdown_backend_watcher();
  m_unmovables.shutdown_flag.clear();
  m_backend_watcher = std::thread(backend_watcher, std::ref(*this), backend, 100, 1000);
//  std::cerr << "change_backend returns " << backend <<" and watcher joinable=" <<m_backend_watcher.joinable() << std::endl;
}

void sjef::Project::backend_watcher(sjef::Project& project,
                                    const std::string& backend,
                                    int min_wait_milliseconds,
                                    int max_wait_milliseconds) noexcept {
  if (max_wait_milliseconds <= 0) max_wait_milliseconds = min_wait_milliseconds;
  constexpr auto radix = 2;
  auto wait = std::max(min_wait_milliseconds, 1);
  try {
//    std::cerr << "sjef::Project::backend_watcher() start for project "<<project.name()<<" at address "<<&project<<", backend "<< backend
//    << ", " << project.property_get("backend")
//              << std::endl;
//    std::cerr << "ensure_remote_server call from server"<<std::endl;
    project.ensure_remote_server();
//    std::cerr << "ensure_remote_server returns to server"<<std::endl;
    for (auto iter = 0; !project.m_unmovables.shutdown_flag.test_and_set(); ++iter) {
      project.m_unmovables.shutdown_flag.clear();
//      std::cerr << "iter " << iter << std::endl;
//      std::cerr << "going to sleep for " << wait << "ms" << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(wait));
      wait = std::min(wait * radix, max_wait_milliseconds);
//      std::cerr << "... watcher for project " << &project << " waking up" << std::endl;
      auto abort = project.m_unmovables.shutdown_flag.test_and_set();
//      std::cerr << "abort="<<abort<<std::endl;
      if (not abort) {
        project.m_unmovables.shutdown_flag.clear();
        try {
          project.synchronize(backend,0);
        }
        catch (const std::exception& ex) {
          std::cerr << "sjef::Project::backend_watcher() synchronize() has thrown " << ex.what() << std::endl;
          project.m_status = unknown;
        }
//      std::cerr << "... watcher for project "<<&project<<" back from synchronize"<<std::endl;
        try {
          project.m_status = project.status(0, false);
        }
        catch (const std::exception& ex) {
          std::cerr << "sjef::Project::backend_watcher() status() has thrown " << ex.what() << std::endl;
          project.m_status = unknown;
        }
//      std::cerr << "... watcher for project "<<&project<<" back from status"<<std::endl;
//      std::cerr << "sjef::Project::backend_watcher() status " << project.m_status << std::endl;
      }
    }
//    std::cerr << "sjef::Project::backend_watcher() stop" << std::endl;
  }
  catch (...) {
  }
}

} // namespace sjef
