#include "sjef.h"
#include <string>
#include <iostream>
#include <boost/algorithm/string.hpp>
#include <map>
#include <array>
#include <regex>
#include "sjef-backend.h"
#include "FileLock.h"
#include <boost/process/search_path.hpp>
#include <boost/process/child.hpp>
#include <boost/process/spawn.hpp>
#include <boost/process/args.hpp>
#include <boost/process/io.hpp>
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

int backend_watcher_wait_milliseconds;

///> @private
struct sjef::pugi_xml_document : public pugi::xml_document {};

///> @private
const std::string sjef::Project::s_propertyFile = "Info.plist";

///> @private
inline fs::path executable(fs::path command) {
  if (command.is_absolute())
    return command;
  else
    return bp::search_path(command);
}

///> @private
bool copyDir(
    fs::path const& source,
    fs::path const& destination,
    bool delete_source = false
) {
  // Check whether the function call is valid
  if (!fs::exists(source) || !fs::is_directory(source))
    throw std::runtime_error("Source directory " + source.string() + " does not exist or is not a directory.");
  sjef::FileLock source_lock((source / ".lock").native(), false, true);
  if (fs::exists(destination))
    throw std::runtime_error("Destination directory " + destination.string() + " already exists.");
  // Create the destination directory
  if (!fs::create_directory(destination))
    throw std::runtime_error("Unable to create destination directory " + destination.string());
  sjef::FileLock destination_lock((destination / ".lock").native(), true, true);
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
      if (current.filename() != ".lock")
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
                 bool construct,
                 const std::string& default_suffix,
                 const std::map<std::string, std::string>& suffixes,
                 const Project* masterProject) :
    m_project_suffix(get_project_suffix(filename, default_suffix)),
    m_filename(expand_path(filename, m_project_suffix)),
    m_reserved_files(std::vector<std::string>{sjef::Project::s_propertyFile}),
    m_properties(std::make_unique<pugi_xml_document>()),
    m_suffixes(suffixes),
    m_backend_doc(std::make_unique<pugi_xml_document>()),
    m_status_lifetime(0),
    m_status_last(std::chrono::steady_clock::now()),
//    m_file_lock(nullptr),
    m_master_instance(masterProject),
    m_master_of_slave(masterProject == nullptr),
    m_property_file_modification_time(0),
    m_use_control_path(false) {
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
//  std::cerr << fs::system_complete(m_filename) << std::endl;
  if (!fs::exists(m_filename))
    throw std::runtime_error("project does not exist and could not be created: " + m_filename);
  if (!fs::is_directory(m_filename))
    throw std::runtime_error("project should be a directory: " + m_filename);

//  std::cerr << "constructor m_filename=" << m_filename
//            << " construct=" << construct << "<< address=" << this << ", master=" << m_master_instance << std::endl;
  if (!construct) return;
  if (!fs::exists(propertyFile())) {
    save_property_file();
    property_set("_private_sjef_project_backend_inactive", "1");
  }
  property_set("_private_sjef_project_backend_inactive_synced", "0");
  cached_status(unevaluated);
  custom_initialisation();

  auto nimport = property_get("IMPORTED").empty() ? 0 : std::stoi(property_get("IMPORTED"));
//    std::cerr << "nimport "<<nimport<<std::endl;
  for (int i = 0; i < nimport; i++) {
//      std::cerr << "key "<<std::string{"IMPORT"}+std::to_string(i)<<", value "<<property_get(std::string{"IMPORT"}+std::to_string(i))<<std::endl;
    m_reserved_files.push_back(property_get(std::string{"IMPORT"} + std::to_string(i)));
  }
  recent_edit(m_filename);

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
  if (m_backends.count(sjef::Backend::default_name) == 0) {
    m_backends[sjef::Backend::default_name] = default_backend();
    const auto config_file = expand_path("~/.sjef/" + m_project_suffix + "/backends.xml");
    if (not fs::exists(config_file))
      fs::ofstream(config_file) << "<?xml version=\"1.0\"?>\n<backends>\n</backends>" << std::endl;
    {
      FileLock l(config_file);
      fs::copy_file(config_file, config_file + "-");
      FileLock ll(config_file + "-");
      auto in = fs::ifstream(config_file + "-");
      auto out = fs::ofstream(config_file);
      std::string line;
      auto be_defaults = Backend();
      while (std::getline(in, line)) {
        out << line << std::endl;
        if (line.find("<backends>") != std::string::npos) {
          out << "  <backend name=\"" + sjef::Backend::default_name + "\" ";
          if (be_defaults.host != m_backends[sjef::Backend::default_name].host)
            out << "\n           host=\"" + m_backends[sjef::Backend::default_name].host + "\" ";
          if (be_defaults.run_command != m_backends[sjef::Backend::default_name].run_command)
            out << "\n           run_command=\"" + m_backends[sjef::Backend::default_name].run_command + "\" ";
          if (be_defaults.run_jobnumber != m_backends[sjef::Backend::default_name].run_jobnumber)
            out << "\n           run_jobnumber=\"" + m_backends[sjef::Backend::default_name].run_jobnumber + "\" ";
          if (be_defaults.status_command != m_backends[sjef::Backend::default_name].status_command)
            out << "\n           status_command=\"" + m_backends[sjef::Backend::default_name].status_command + "\" ";
          if (be_defaults.status_waiting != m_backends[sjef::Backend::default_name].status_waiting)
            out << "\n           status_waiting=\"" + m_backends[sjef::Backend::default_name].status_waiting + "\" ";
          if (be_defaults.status_running != m_backends[sjef::Backend::default_name].status_running)
            out << "\n           status_running=\"" + m_backends[sjef::Backend::default_name].status_running + "\" ";
          if (be_defaults.kill_command != m_backends[sjef::Backend::default_name].kill_command)
            out << "\n           kill_command=\"" + m_backends[sjef::Backend::default_name].kill_command + "\" ";
          out << "\n  />" << std::endl;
        }
      }
    }
    fs::remove(config_file + "-");
  }
//  for (const auto& be : m_backends) std::cerr << "m_backend "<<be.first<<std::endl;

//std::cerr << "project constructor name()="<<name()<<std::endl;
  if (not name().empty() and name().front() != '.')
    change_backend(property_get("backend"), true);
}

Project::~Project() {
//  std::cerr << "enter destructor for project " << name() << " address " << this << ", m_master_instance="
//            << m_master_instance << std::endl;
//  std::cerr << "thread joinable? " << m_backend_watcher.joinable() << std::endl;
//  if (m_master_of_slave)
//    std::cerr << "shutdown_backend_watcher() about to be called from destructor for " << this << std::endl;
  if (m_master_of_slave)
    shutdown_backend_watcher();
//  if (m_remote_server->process.running())
//    std::cerr << "~Project remote server process to be killed: " << m_remote_server->process.id() << ", master="
//              << m_master_of_slave << std::endl;
  if (m_remote_server != nullptr and m_remote_server->process.running())
    m_remote_server->process.terminate();
//  std::cerr << "shutdown_backend_watcher() returned to destructor for " << this << std::endl;
//  std::cerr << "leaving destructor for project " << name() << " address " << this << std::endl;
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

void Project::throw_if_backend_invalid(std::string backend) const {
  if (backend.empty()) backend = property_get("backend");
  if (backend.empty()) throw std::runtime_error("No backend specified");
  if (m_backends.count(backend) == 0)
    throw std::runtime_error("Backend " + backend + " is not registered");
}

bool Project::export_file(std::string file, bool overwrite) {
  throw_if_backend_invalid();
  if (!property_get("backend").empty())synchronize(0);
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

std::mutex synchronize_mutex;
bool Project::synchronize(int verbosity, bool nostatus) const {
  verbosity += 3;
//  const std::lock_guard lock(m_synchronize_mutex);
  const std::lock_guard lock(synchronize_mutex);
  auto backend_name = property_get("backend");
  std::cerr << "backend_name in synchronise() " << backend_name << std::endl;
//  for (const auto& keyval : m_backends)
//    std::cerr << "m_backends[" << keyval.first << "]=" << keyval.second.name << std::endl;
  auto& backend = m_backends.at(backend_name);
  if (verbosity > 1)
    std::cerr << "synchronize with " << backend.name << " (" << backend.host << ") master=" << m_master_of_slave
              << std::endl;
  if (backend.host == "localhost") return true;
  if (verbosity > 1)
    std::cerr << "synchronize backend_inactive=" << property_get("_private_sjef_project_backend_inactive")
              << " backend_inactive_synced="
              << property_get("_private_sjef_project_backend_inactive_synced") << std::endl;
//  std::cerr << "input exists ? " <<fs::exists(filename("inp")) << std::endl;
//  std::cerr << "compare write times "<<fs::last_write_time(filename("inp")) << " : " << m_property_file_modification_time << std::endl;
  bool locally_modified;
  {
    FileLock pl(propertyFile(), false, false);
    locally_modified = m_property_file_modification_time != fs::last_write_time(propertyFile());
//    std::cerr << "initial locally_modified=" << locally_modified << std::endl;
    for (const auto& suffix : {"inp", "xyz"}) {
      if (fs::exists(filename(suffix))) {
        auto lm = fs::last_write_time(filename(suffix));
        locally_modified = locally_modified
            or lm > m_input_file_modification_time[suffix];
        m_input_file_modification_time[suffix] = lm;
      }
    }
  }
//  std::cerr << "locally_modified=" << locally_modified << std::endl;
  if (not locally_modified
      and property_get("_private_sjef_project_backend_inactive_synced") == "1")
    return true;
//  std::cerr << "really syncing" << std::endl;
  //TODO: implement more robust error checking
//  system(("ssh " + backend.host + " mkdir -p " + cache(backend)).c_str());
  const_cast<Project*>(this)->change_backend(backend.name);
//  if (m_master_of_slave) {
  ensure_remote_server();
  // absolutely send reserved files
  std::string rsync = "rsync";
  std::string rsyncopt = "-vv --timeout=5";
  rsyncopt += " --exclude=backup";
  rsyncopt += " --exclude=*.out_*";
  rsyncopt += " --exclude=*.xml_*";
  rsyncopt += " --exclude=*.log_*";
  rsyncopt += " --exclude=*.d";
  if (not this->m_control_path_option.empty())
    rsyncopt += " -e 'ssh " + m_control_path_option + "'";
  if (verbosity > 2)
    std::cerr << "rsyncopt: " << rsyncopt << std::endl;
  auto cmd = std::string{bp::search_path(rsync).native()} + " " +
      rsyncopt + " " +
      "-L -a --update " + " " + (verbosity > 0 ? std::string{"-v "} : std::string{""}) + m_filename + "/ "
      + backend.host + ":"
      + cache(backend);
  if (verbosity > 1)
    std::cerr << "First rsync: " << cmd << std::endl;
  bp::child(cmd).wait();
  // fetch all newer files from backend
  if (property_get("_private_sjef_project_backend_inactive_synced") == "1")
    std::cerr << "second rsync not taken" << std::endl;
  if (property_get("_private_sjef_project_backend_inactive_synced") == "1") return true;
  {
    auto cmd = std::string{bp::search_path(rsync).native()} + " " +
        rsyncopt +
        " -a --update " + (verbosity > 0 ? std::string{"-v "} : std::string{""});
    for (const auto& rf : m_reserved_files) {
//    std::cerr << "reserved file pattern " << rf << std::endl;
      auto f = regex_replace(fs::path{rf}.filename().native(), std::regex(R"--(%)--"), name());
//    std::cerr << "reserved file resolved " << f << std::endl;
      if (fs::exists(f))
        cmd += "--exclude=" + f + " ";
    }
    cmd += backend.host + ":" + cache(backend) + "/ " + m_filename;
    if (verbosity > 1)
      std::cerr << "second rsync " << cmd << std::endl;
    bp::child(cmd).wait();
  }
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

  for (const auto& key : property_names()) {
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
//  std::cerr << "move to " << dest << std::endl;
  if (force)
    fs::remove_all(dest);
  if (!property_get("backend").empty()) synchronize();
  auto namesave = name();
  auto filenamesave = m_filename;
  shutdown_backend_watcher();
//  std::cerr << "move about to copyDir " << m_filename << " : " << dest << std::endl;
  bool success = false;
  if (copyDir(fs::path(m_filename), dest, true)) {
//    std::cerr << "move succeeded to copyDir " << m_filename << " : " << dest << std::endl;
    m_filename = dest.string();
    force_file_names(namesave);
    recent_edit(m_filename, filenamesave);
//    std::cerr << "move about to remove all " << filenamesave << std::endl;
    success = fs::remove_all(filenamesave) > 0;
  }
  if (not success) std::cerr << "move failed to copyDir " << m_filename << " : " << dest << std::endl;
//  std::cerr << "move about to call change_backend " << success << std::endl;
  change_backend(property_get("backend"));
//  std::cerr << "move returns " << success << std::endl;
  return success;
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

void Project::erase(const std::string& filename, const std::string& default_suffix) {
//    std::cerr << "sjef::project::erase "<<filename<<std::endl;
  auto filename_ = sjef::expand_path(filename, default_suffix);
  if (fs::remove_all(filename_)) {
    recent_edit("", filename_);
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
  throw_if_backend_invalid(backend);
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

std::string Project::backend_parameter_expand(const std::string& backend, std::string templ) const {
  if (templ.empty()) templ = backend_get(backend, "run_command");
//  std::cerr << "backend_parameter_expand backend="<<backend << ", templ="<<templ<<std::endl;
  std::string output_text;
  std::regex re("[^$]\\{([^}]*)\\}");
  auto callback = [&](std::string m) {
//    std::cerr << "callback "<<m<<std::endl;
    if (std::regex_match(m, re)) {
//      std::cerr << "callback, matching m=" << m << std::endl;
      auto first = m.front();
      m.pop_back();
      m.erase(0, 1);
      if (first != '{') m[0] = first;
//      std::cerr << "matched "<<m<<std::endl;
      auto bang = m.find_first_of("!");
      if (bang != std::string::npos)
        m = m.substr(0, bang);
//      std::cerr << "matched with comment removed=" << m << std::endl;
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
//      std::cerr << "parameter_name="<<parameter_name<<", value="<<value<<std::endl;
      if (value.empty()) {
//        std::cerr << "value empty; def="<<def<<std::endl;
        if (not def.empty())
          output_text += m.substr(0, percent) + def;
        else
          output_text += m.front();
      } else {
        output_text += m.substr(0, percent) + value;
      }
    } else {
//      std::cerr << "plain append of "<<m<<std::endl;
      output_text += m;
    }
//    std::cerr << "callback end has output_text="<<output_text<<std::endl;
  };

  auto templ_ = std::string{" "} + templ;
  std::sregex_token_iterator
      begin(templ_.begin(), templ_.end(), re, {-1, 0}),
      end;
  std::for_each(begin, end, callback);
//  std::cerr << "backend_parameter_expand returns output_text="<<output_text<<std::endl;
  return output_text.substr(1);
}

std::map<std::string, std::string> Project::backend_parameters(const std::string& backend, bool doc) const {
  std::map<std::string, std::string> result;

  throw_if_backend_invalid(backend);
  auto templ = std::string{" "} + m_backends.at(backend).run_command;
  std::string output_text;
  std::regex re("[^$]\\{([^}]*)\\}");
  auto callback = [&](std::string m) {
    if (std::regex_match(m, re)) {
//      std::cerr << "callback, matching m="<<m<<std::endl;
      auto first = m.front();
      m.pop_back();
      m.erase(0, 1);
      if (first != '{') m[0] = first;
//      std::cerr << "matched "<<m<<std::endl;
//      std::cerr << "callback, trimmed m="<<m<<std::endl;
      std::string docu;
      auto bang = m.find_first_of("!");
      if (bang != std::string::npos) {
        docu = m.substr(bang + 1);
        m = m.substr(0, bang);
      }
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
      result[parameter_name] = doc ? docu : def;
    }
  };

  std::sregex_token_iterator
      begin(templ.begin(), templ.end(), re, {-1, 0}),
      end;
  std::for_each(begin, end, callback);

  return result;
}

bool Project::run(int verbosity, bool force, bool wait) {
  auto& backend = m_backends.at(property_get("backend"));
  auto stat = status(verbosity);
  if (stat == running || stat == waiting) {
    return false;
  }
  if (verbosity > 0)
    std::cerr << "Project::run() run_needed()=" << run_needed(verbosity) << std::endl;
//  if (not force and not run_needed()) return false;
  cached_status(unevaluated);
  backend_watcher_wait_milliseconds = 0;
  std::string line;
  bp::child c;
  std::string optionstring;
//  std::cerr << "setting run_input_hash to input_hash()=" << input_hash() << std::endl;
  property_set("run_input_hash", std::to_string(input_hash()));
  if (verbosity > 0 and backend.name != sjef::Backend::dummy_name) optionstring += "-v ";
//  std::cerr << "backend.run_command before expand "<<backend.run_command<<std::endl;
  auto run_command = backend_parameter_expand(backend.name, backend.run_command);
//  std::cerr << "run_command after expand "<<run_command<<std::endl;
  custom_run_preface();
  if (backend.host == "localhost") {
    property_set("_private_sjef_project_backend_inactive", "0");
    property_set("_private_sjef_project_backend_inactive_synced", "0");
    if (verbosity > 0) std::cerr << "run local job, backend=" << backend.name << std::endl;
    auto spl = splitString(run_command);
    run_command = spl.front();
//    spl.erase(spl.begin());
//    for (const auto& sp : spl)
    for (auto sp = spl.rbegin(); sp < spl.rend() - 1; sp++)
      optionstring = "'" + *sp + "' " + optionstring;
    if (verbosity > 1)
      std::cerr << "run local job executable=" << executable(run_command) << " " << optionstring << " "
                << fs::path{m_filename} / fs::path(this->name() + ".inp")
                << std::endl;
    if (verbosity > 2)
      for (const auto& o : splitString(optionstring))
        std::cerr << "option " << o << std::endl;
    fs::path current_path_save;
    try {
      current_path_save = fs::current_path();
    }
    catch (...) {
      current_path_save = "";
    }
    fs::current_path(m_filename);

    if (optionstring.empty())
      c = bp::child(executable(run_command),
                    fs::path{m_filename} / fs::path(this->name() + ".inp"));
    else
      c = bp::child(executable(run_command),
                    bp::args(splitString(optionstring)),
                    fs::path{m_filename} / fs::path(this->name() + ".inp"));
    fs::current_path(current_path_save);
    if (not c.valid())
      throw std::runtime_error("Spawning run process has failed");
    c.detach();
    property_set("jobnumber", std::to_string(c.id()));
    if (verbosity > 1) std::cerr << "jobnumber " << c.id() << ", running=" << c.running() << std::endl;
    if (wait) this->wait();
    return true;
  } else { // remote host
    if (verbosity > 0) std::cerr << "run remote job on " << backend.name << std::endl;
    bp::ipstream c_err, c_out;
    synchronize(verbosity);
    property_set("_private_sjef_project_backend_inactive", "0");
    property_set("_private_sjef_project_backend_inactive_synced", "0");
    if (verbosity > 3) std::cerr << "cache(backend) " << cache(backend) << std::endl;
    auto jobstring =
        "cd " + cache(backend) + "; nohup " + run_command + " " + optionstring
            + this->name()
            + ".inp";
    if (backend.run_jobnumber == "([0-9]+)") jobstring += "& echo $! "; // go asynchronous if a straight launch
    if (verbosity > 3) std::cerr << "backend.run_jobnumber " << backend.run_jobnumber << std::endl;
    if (verbosity > 3) std::cerr << "jobstring " << jobstring << std::endl;
    auto run_output = remote_server_run(jobstring);
    if (verbosity > 3) std::cerr << "run_output " << run_output << std::endl;
    std::smatch match;
    if (std::regex_search(run_output, match, std::regex{backend.run_jobnumber})) {
      if (verbosity > 2)
        std::cerr << "... a match was found: " << match[1] << std::endl;
      if (verbosity > 1) status(verbosity - 2);
      property_set("jobnumber", match[1]);
      if (wait) this->wait();
      return true;
    }
  }
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
  throw_if_backend_invalid();
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
    ensure_remote_server();
    remote_server_run(be.kill_command + " " + pid, 0, false);
  }
  property_set("_private_sjef_project_killed_job", be.host + ":" + pid);
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

std::string Project::xml(bool sync) const {
  return xmlRepair(file_contents(m_suffixes.at("xml"), "", sync));
}

std::string Project::file_contents(const std::string& suffix, const std::string& name, bool sync) const {

//  std::cerr << "file_contents " << suffix << ": " << property_get("_private_sjef_project_backend_inactive_synced")
//            << std::endl;
  auto be = property_get("backend");
  if (sync
      and not be.empty()
      and be != "local"
      and property_get("_private_sjef_project_backend_inactive_synced") != "1"
      and suffix != m_suffixes.at("inp")
      )
    synchronize();
  std::ifstream s(filename(suffix, name));
  auto result = std::string(std::istreambuf_iterator<char>(s),
                            std::istreambuf_iterator<char>());
  while (result.back() == '\n') result.pop_back();
  return result;
}

status Project::status(int verbosity, bool cached) const {
//  if (cached and cached_status() != unevaluated)
//    std::cerr << "using cached status " << cached_status() << ", so returning immediately" << std::endl;
//  else if (cached)
//    std::cerr << "want to use cached status but cannot because not yet evaluated " << cached_status() << std::endl;
//  else
//    std::cerr << "do not want to use cached status " << cached_status() << std::endl;
  if (cached and cached_status() != unevaluated and m_master_of_slave) return cached_status();
  if (property_get("backend").empty()) return unknown;
  auto start_time = std::chrono::steady_clock::now();
  auto bes = property_get("backend");
  if (bes.empty()) bes = sjef::Backend::default_name;
  throw_if_backend_invalid(bes);
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
    if (!rih.empty())
      return (rih == ih) ? ((be.host + ":" + pid) == property_get("_private_sjef_project_killed_job") ? killed
                                                                                                      : completed)
                         : unknown;
    return (std::regex_replace(file_contents("inp"), std::regex{" *\n\n*"}, "\n") != input_from_output(false))
           ? unknown : completed;
  }
//  std::cerr << "did not return unknown for empty pid "<<pid << std::endl;
  const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive", "0");
  if (property_get("_private_sjef_project_completed_job") == be.host + ":" + pid
      and cached_status() != unevaluated) {
//    std::cerr << "status return completed/killed because _private_sjef_project_completed_job is valid" << std::endl;
    const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive", "1");
    return ((be.host + ":" + pid) == property_get("_private_sjef_project_killed_job") ? killed : completed);
  }
  auto result =
      pid == "" ? unknown : ((be.host + ":" + pid) == property_get("_private_sjef_project_killed_job") ? killed
                                                                                                       : completed);
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
          result = ((be.host + ":" + pid) == property_get("_private_sjef_project_killed_job") ? killed : completed);
        }
        result = running;
      }
    }
  } else {
    if (verbosity > 1)
      std::cerr << "remote status " << be.host << ":" << be.status_command << ":" << pid << std::endl;
    ensure_remote_server();
//    remote_server_run(be.status_command + " " + pid);
    {
      const std::lock_guard lock(m_remote_server_mutex);
      m_remote_server->in << be.status_command << " " << pid << std::endl;
      m_remote_server->in << "echo '@@@!!EOF'" << std::endl;
      if (verbosity > 2)
        std::cerr << "sending " << be.status_command << " " << pid << std::endl;
      std::string line;
      while (std::getline(m_remote_server->out, line)
          && line != "@@@!!EOF"
          ) {
        if (verbosity > 0) std::cerr << "line received: " << line << std::endl;
        if ((" " + line).find(" " + pid + " ") != std::string::npos) {
          std::smatch match;
          if (verbosity > 2) std::cerr << "line" << line << std::endl;
          if (verbosity > 2) std::cerr << "status_running " << be.status_running << std::endl;
          if (verbosity > 2) std::cerr << "status_waiting " << be.status_waiting << std::endl;
          if (std::regex_search(line, match, std::regex{be.status_waiting})) {
            result = waiting;
          }
          if (std::regex_search(line, match, std::regex{be.status_running})) {
            result = running;
          }
        }
      }
    }
  }
  if (verbosity > 2) std::cerr << "received status " << result << std::endl;
  if (result == completed || result == killed) {
//    const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive_synced", property_get("_private_sjef_project_backend_inactive"));
    const_cast<Project*>(this)->property_set("_private_sjef_project_completed_job", be.host + ":" + pid);
  }
  if (result != unknown) {
//    std::cerr << "result is not unknown, but is " << result << std::endl;
    if (result == running)
      const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive_synced", "0");
    const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive",
                                             (result == completed || result == killed) ? "1" : "0");
    goto return_point;
  }
  if (verbosity > 2) std::cerr << "fallen through loop" << std::endl;
  synchronize(verbosity, true);
  const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive",
                                           ((result != completed && result != killed) or
                                               fs::exists(fs::path{filename()} / fs::path{name()
                                                                                              + "."
                                                                                              + m_suffixes.at("xml")}) // there may be a race, where the job status is completed, but the output file is not yet there. This is an imperfect test for that .. just whether the .xml exists at all. TODO: improve test for complete output file
                                           )
                                           ? "1" : "0");
  return_point:
  auto time_taken =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
  if (time_taken < m_status_lifetime) m_status_lifetime = time_taken;
  m_status_last = std::chrono::steady_clock::now();
  cached_status(result);
//  std::cerr << "cached_status() set to " << cached_status() << std::endl;
  return result;
}

sjef::status Project::cached_status() const {
  auto current_status = property_get("_status");
  return current_status.empty() ? unevaluated : static_cast<sjef::status>(std::stoi(current_status));
}

void Project::cached_status(sjef::status status) const {
  auto current_status = property_get("_status");
  if (current_status.empty() or status != std::stoi(current_status))
    const_cast<Project*>(this)->property_set("_status", std::to_string(static_cast<int>(status)));
}

std::string sjef::Project::status_message(int verbosity) const {
  std::map<sjef::status, std::string> message;
  message[sjef::status::unknown] = "Not found";
  message[sjef::status::running] = "Running";
  message[sjef::status::waiting] = "Waiting";
  message[sjef::status::completed] = "Completed";
  message[sjef::status::unevaluated] = "Unevaluated";
  message[sjef::status::killed] = "Killed";
  auto statu = this->status(verbosity);
  auto result = message[statu];
  if (statu != sjef::status::unknown && !property_get("jobnumber").empty())
    result += ", job number " + property_get("jobnumber") + " on backend "
        + property_get("backend");
  return result;
}

void Project::wait(unsigned int maximum_microseconds) const {
  unsigned int microseconds = 1;
//  std::cout << "wait enters with status " << status() << std::endl;
  sjef::status stat;
  while (true) {
    auto stat = status();
    if (stat == completed or stat == killed) break;
//    std::cout << "stat=" << stat << std::endl;
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
    if (microseconds < maximum_microseconds) microseconds *= 2;
  }
//  std::cout << "wait returns with status " << status() << std::endl;
}

void Project::property_delete(const std::vector<std::string>& properties) {
  FileLock pl(propertyFile(), true, false);
  check_property_file_locked();
  for (const auto& property : properties)
    property_delete_locked(property);
  save_property_file_locked();
}

void Project::property_delete(const std::string& property) { //TODO make atomic
  property_delete(std::vector<std::string>{property});
}

void Project::property_delete_locked(const std::string& property) {
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
}

void Project::property_set(const std::map<std::string, std::string>& properties) {
  FileLock pl(propertyFile(), true, false);
  check_property_file_locked();
  for (const auto& keyval : properties) {
    const auto& property = keyval.first;
    const auto& value = keyval.second;
//    std::cout << "property_set " << property << " = " << value << " on thread " << m_master_of_slave << std::endl;

    property_delete_locked(property);
    std::lock_guard guard(m_unmovables.m_property_set_mutex);
    if (!m_properties->child("plist")) m_properties->append_child("plist");
    if (!m_properties->child("plist").child("dict")) m_properties->child("plist").append_child("dict");
    auto keynode = m_properties->child("plist").child("dict").append_child("key");
    keynode.text() = property.c_str();
    auto stringnode = m_properties->child("plist").child("dict").append_child("string");
    stringnode.text() = value.c_str();
  }
  save_property_file_locked();
}

void Project::property_set(const std::string& property, const std::string& value) {
  property_set({{property, value}});
}

std::string Project::property_get(const std::string& property) const {
  return property_get(std::vector<std::string>{property})[property];
}
std::map<std::string, std::string> Project::property_get(const std::vector<std::string>& properties) const {
  check_property_file();
  std::map<std::string, std::string> results;
  for (const std::string& property : properties) {
    std::string query{"/plist/dict/key[text()='" + property + "']/following-sibling::string[1]"};
    std::string result = m_properties->select_node(query.c_str()).node().child_value();
    result = m_properties->select_node(query.c_str()).node().child_value();
//  std::cout << "property_get " << property << "=" << result << " on thread " << m_master_of_slave << std::endl;
    std::string value = m_properties->select_node(query.c_str()).node().child_value();
    if (not value.empty())
      results[property] = value;
  }
  return results;
}

std::vector<std::string> Project::property_names() const {
  check_property_file();
  std::vector<std::string> result;
  for (const auto& node : m_properties->select_nodes(((std::string) {"/plist/dict/key"}).c_str()))
    result.push_back(node.node().child_value());
  return result;
}

void Project::recent_edit(const std::string& add, const std::string& remove) {
  auto project_suffix =
      add.empty() ? fs::path(remove).extension().native().substr(1) : fs::path(add).extension().native().substr(1);
  auto recent_projects_file = expand_path(std::string{"~/.sjef/"} + project_suffix + "/projects");
  bool changed = false;
  {
    if (!fs::exists(recent_projects_file)) {
      fs::create_directories(fs::path(recent_projects_file).parent_path());
      fs::ofstream junk(recent_projects_file);
    }
    FileLock lock{recent_projects_file + ".lock"};
    std::ifstream in(recent_projects_file);
    std::ofstream out(recent_projects_file + "-");
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
    if (changed)
      fs::rename(recent_projects_file + "-", recent_projects_file);
    else
      fs::remove(recent_projects_file + "-");
  }
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

inline std::string slurp(const std::string& path) {
  std::ostringstream buf;
  std::ifstream input(path.c_str());
  buf << input.rdbuf();
  return buf.str();
}
void Project::load_property_file_locked() const {
//  std::cout << "load_property_file() from " << this << std::endl;
//  std::cout << slurp(propertyFile())<<std::endl;
  auto result = m_properties->load_file(propertyFile().c_str());
  if (!result)
    throw std::runtime_error(
        "error in loading " + propertyFile() + "\n" + result.description() + "\n" + slurp(propertyFile()));
  m_property_file_modification_time = fs::last_write_time(propertyFile());
}

static std::string writing_object_file = ".Info.plist.writing_object";

bool Project::properties_last_written_by_me(bool removeFile) const {
  auto path = fs::path{m_filename} / fs::path{writing_object_file};
//  std::cout << "enter properties_last_written_by_me on thread " << m_master_of_slave << std::endl;
  FileLock lock(path.string(), false);
  std::ifstream i{path.string(), std::ios_base::in};
  if (not i.is_open()) return false;
  std::hash<const Project*> hasher;
  auto me = hasher(this);
  std::hash<const Project*>::result_type writer;
  i >> writer;
  if (removeFile and writer == me)
    fs::remove(path);
//  std::cout << "properties_last_written_by_me writer=" << writer << " me=" << me << " : " << (writer == me)
//            << std::endl;
  return writer == me;

}
void Project::check_property_file() const {
  FileLock fileLock(propertyFile(), false, false);
  check_property_file_locked();
}
void Project::check_property_file_locked() const {
  auto lastwrite = fs::last_write_time(propertyFile());
  if (m_property_file_modification_time == lastwrite) { // tie-breaker
    if (not properties_last_written_by_me(false))
      --m_property_file_modification_time; // to mark this object's cached properties as out of date
  }
  if (m_property_file_modification_time < lastwrite) {
    {
      load_property_file_locked();
    }
    m_property_file_modification_time = lastwrite;
  }
}

void Project::save_property_file() const {
  FileLock fileLock(propertyFile(), true, false);
  save_property_file_locked();
}
void Project::save_property_file_locked() const {
  struct plist_writer writer;
  writer.file = propertyFile();
  if (not fs::exists(propertyFile())) {
    fs::create_directories(m_filename);
    { fs::ofstream x(propertyFile()); }
  }
//    std::cerr << "1 exists(propertyFile()) ? " << fs::exists(propertyFile()) << std::endl;
  if (use_writer)
    m_properties->save(writer, "\t", pugi::format_no_declaration);
  else
    m_properties->save_file(propertyFile().c_str());
//  std::cerr << "2 exists(propertyFile()) ? " << fs::exists(propertyFile()) << std::endl;
//  std::cout << "save_property_file master=" << m_master_of_slave << std::endl;
//  system((std::string{"cat "} + propertyFile()).c_str());
//  std::cout << "end save_property_file master=" << m_master_of_slave << std::endl;
  m_property_file_modification_time = fs::last_write_time(propertyFile());
  auto path = (fs::path{m_filename} / fs::path{writing_object_file});
  FileLock lock(path.string(), true, false);
  std::ofstream o{path.string()};
  std::hash<const Project*> hasher;
  o << hasher(this);
//    std::cout << " writing hash " << hasher(this) << " on thread " << m_master_of_slave << std::endl;
//  std::cout << "written propertyFile:\n";
//  std::cout << slurp(propertyFile())<<std::endl;
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
    if (be.first != sjef::Backend::dummy_name)
      result.push_back(be.first);
  return result;
}

std::string sjef::Project::remote_server_run(const std::string& command, int verbosity, bool wait) const {
  const std::lock_guard lock(m_remote_server_mutex);
  if (verbosity > 0)
    std::cerr << command << std::endl;
  const std::string terminator{"@@@!!EOF"};
  if (not wait) {
    m_remote_server->in << command << " >/dev/null 2>/dev/null &" << std::endl;
    return "";
  }
//  std::cerr << "remote_server_run m_remote_server->process.running" << m_remote_server->process.running() << std::endl;
  m_remote_server->in << "pwd" << std::endl;
  m_remote_server->in << command << std::endl;
  m_remote_server->in << ">&2 echo '" << terminator << "' $?" << std::endl;
  m_remote_server->in << "echo '" << terminator << "'" << std::endl;
  std::string line;
  m_remote_server->last_out.clear();
  while (std::getline(m_remote_server->out, line) && line != terminator)
    m_remote_server->last_out += line + '\n';
  if (verbosity > 1)
    std::cerr << "out from remote command " << command << ": " << m_remote_server->last_out << std::endl;
  m_remote_server->last_err.clear();
  while (std::getline(m_remote_server->err, line) && line.substr(0, terminator.size()) != terminator)
    m_remote_server->last_err += line + '\n';
  if (verbosity > 1)
    std::cerr << "err from remote command " << command << ": " << m_remote_server->last_err << std::endl;
  if (verbosity > 1)
    std::cerr << "last line=" << line << std::endl;
  auto rc = std::stoi(line.substr(terminator.size() + 1));
  if (verbosity > 1)
    std::cerr << "rc=" << rc << std::endl;
  if (rc != 0)
    throw std::runtime_error("Failed remote command: " + command + "\nStandard output:\n" + m_remote_server->last_out
                                 + "\nStandard error:\n" + m_remote_server->last_err);
  return m_remote_server->last_out;
}
void sjef::Project::ensure_remote_server() const {
  if (m_master_instance != nullptr) {
    m_remote_server = m_master_instance->m_remote_server;
    return;
  }
  const std::lock_guard lock(m_remote_server_mutex);
  if (m_remote_server == nullptr)
    m_remote_server.reset(new remote_server());
//  std::cerr << "ensure_remote_server called on thread " << std::this_thread::get_id() << ", master_of_slave="
//            << m_master_of_slave << ", host=" << m_remote_server->host << ", m_remote_server->process.running "
//            << m_remote_server->process.running() << std::endl;
//  std::cerr << "m_remote_server->host "<<m_remote_server->host << std::endl;
  fs::path control_path_directory = expand_path("$HOME/.sjef/.ssh/ctl");
  if (m_use_control_path) {
    fs::create_directories(control_path_directory);
    m_control_path_option = "-o ControlPath=\"" + (control_path_directory / "%r@%h:%p").string() + "\"";
  }
  auto backend = property_get("backend");
//  std::cerr << "master=" << m_master_of_slave << ", backend = " << backend << std::endl;
  if (backend.empty()) backend = sjef::Backend::default_name;
  m_remote_server->host = this->backend_get(backend, "host");
  if (m_remote_server->host == "localhost")
//    std::cerr << "ensure_remote_server() returning for locahost master=" << m_master_of_slave << std::endl;
    if (m_remote_server->host == "localhost") return;
//  std::cerr << "ssh " + m_control_path_option + " -O check " + m_remote_server->host << std::endl;
  if (m_use_control_path) {
    auto c = boost::process::child(bp::search_path("ssh"), m_control_path_option, "-O check", m_remote_server->host,
                                   bp::std_out > bp::null,
                                   bp::std_err > bp::null);
//    std::cerr << "control_path check pid=" << c.id() << std::endl;
    c.wait();
//  if (c.exit_code() != 0)
//    std::cerr << "Attempting to start ssh ControlMaster" << std::endl;
//  else
//    std::cerr << "ssh ControlMaster already running" << std::endl;
//  if (c.exit_code() != 0)
//    std::cerr
//        << "ssh -nNf " + m_control_path_option + " -o ControlMaster=yes -o ControlPersist=60 " + m_remote_server->host
//        << std::endl;
    if (c.exit_code() != 0)
      c = boost::process::child(
          bp::search_path("ssh").native() +
              " -nNf " +
              m_control_path_option +
              " -o ControlMaster=yes -o ControlPersist=60 " +
              m_remote_server->host,
          bp::std_out > bp::null,
          bp::std_err > bp::null);
//    std::cerr << "control_path establish pid=" << c.id() << std::endl;
    c.wait();
//  std::cerr << "exit code " << c.exit_code() << std::endl;
    m_control_path_option = (c.exit_code() == 0) ? m_control_path_option : "";
    c = boost::process::child(bp::search_path("ssh"), m_control_path_option, "-O check", m_remote_server->host,
                              bp::std_out > bp::null,
                              bp::std_err > bp::null);
//    std::cerr << "control_path check2 pid=" << c.id() << std::endl;
    c.wait();
    m_control_path_option = (c.exit_code() == 0) ? m_control_path_option : "";
  }
  if (m_remote_server->process.running()
      and m_remote_server->host == this->backend_get(property_get("backend"), "host")
      )
    return;
//  std::cerr << "ensure_remote_server fires" << std::endl;
//  std::cerr << "Start remote_server " << std::endl;
//  std::cerr << "m_control_path_option=" << m_control_path_option << std::endl;
//  std::cerr << "ensure_remote_server() remote server process to be killed: " << m_remote_server->process.id()
//            << ", master=" << m_master_of_slave << std::endl;
  m_remote_server->process.terminate();
  if (!m_control_path_option.empty())
    m_remote_server->process = bp::child(bp::search_path("ssh"),
                                         m_control_path_option,
                                         m_remote_server->host,
                                         bp::std_in < m_remote_server->in,
                                         bp::std_err > m_remote_server->err,
                                         bp::std_out > m_remote_server->out);
  else
    m_remote_server->process = bp::child(bp::search_path("ssh"),
                                         m_remote_server->host,
                                         bp::std_in < m_remote_server->in,
                                         bp::std_err > m_remote_server->err,
                                         bp::std_out > m_remote_server->out);
//  std::cerr << "started remote_server " << std::endl;
//  std::cerr << "ensure_remote_server() remote server process created : " << m_remote_server->process.id() << ", master="
//            << m_master_of_slave << std::endl;
//  std::cerr << "mkdir -p " << cache(this->m_backends.at(backend)) << std::endl;
//  std::cerr << "ensure_remote_server finishing on thread " << std::this_thread::get_id() << ", master_of_slave="
//            << m_master_of_slave << std::endl;
//  m_remote_server->in << "mkdir -p " << cache(this->m_backends.at(backend)) << std::endl;
}

void sjef::Project::shutdown_backend_watcher() {
  if (not m_master_of_slave) return;
//  std::cerr << "enter shutdown_backend_watcher for project at " << this << " joinable=" << m_backend_watcher.joinable()
//            << " on thread " << std::this_thread::get_id()
//            << std::endl;
  m_unmovables.shutdown_flag.test_and_set();
//  std::cerr << "shutdown_backend_watcher for project at " << this << " joinable=" << m_backend_watcher.joinable()
//            << std::endl;
//  std::cerr << "m_backend_watcher.get_id() " << m_backend_watcher.get_id() << std::endl;
  if (m_backend_watcher.joinable()) {
//    std::cerr << "shutdown_backend_watcher for project at " << this << " joining" << std::endl;
    m_backend_watcher.join();
//    std::cerr << "shutdown_backend_watcher for project at " << this << " joined" << std::endl;
  }
//  else std::cerr << "shutdown_backend_watcher for project at " << this << " not needed " << std::endl;

}

//TODO this function is just for debugging and should be tidied away when done
void sjef::Project::report_shutdown(const std::string& message) const {
  auto value = m_unmovables.shutdown_flag.test_and_set();
  if (!value) m_unmovables.shutdown_flag.clear();
  std::cerr << "report_shutdown " << message << ": " << value << std::endl;
}

void sjef::Project::change_backend(std::string backend, bool force) {
  if (backend.empty()) backend = sjef::Backend::default_name;
  bool unchanged = property_get("backend") == backend;
//  std::cerr << "ENTER change_backend() joinable=" << m_backend_watcher.joinable() <<", master="<<m_master_of_slave << std::endl;
  if (not force and unchanged and m_backend_watcher.joinable()) return;
//  std::cerr << "change_backend to " << backend << " for project " << name() << " at address " << this << std::endl;
//  std::cerr << "current backend " << be << ", unchanged=" << unchanged << std::endl;
  if (not unchanged) {
    throw_if_backend_invalid(backend);
    property_set("backend", backend);
    property_delete("jobnumber");
//    std::cerr << "deleted property jobnumber; its value now is " << property_get("jobnumber") << std::endl;
    ensure_remote_server();
//    if (this->m_backends.at(backend).host != "localhost")
//      std::cerr << "change_backend() about to call remote_server_run() for mkdir, master=" << m_master_of_slave
//      << "cache: " <<cache(this->m_backends.at(backend))
//                << std::endl;
    if (this->m_backends.at(backend).host != "localhost")
      remote_server_run(std::string{"mkdir -p "} + cache(this->m_backends.at(backend)));
  }
  ensure_remote_server();
  if (m_master_of_slave) {
    shutdown_backend_watcher();
    m_unmovables.shutdown_flag.clear();
//    std::cerr << "About to start backend_watcher in a thread" << " on thread " << std::this_thread::get_id() << std::endl;
    m_backend_watcher = std::thread(backend_watcher, std::ref(*this), backend, 100, 1000);
  }
//  std::cerr << "change_backend returns " << backend << " and watcher joinable=" << m_backend_watcher.joinable() << std::endl;
//  std::cerr << "current backend " << property_get("backend") << ", unchanged=" << unchanged << std::endl;
//  std::cerr << "change_backend " << backend << "for project " << name() << " finished " << property_get("backend") << std::endl;
//  std::cerr << "really finished" << std::endl;
}

void sjef::Project::backend_watcher(sjef::Project& project_,
                                    const std::string& backend,
                                    int min_wait_milliseconds,
                                    int max_wait_milliseconds) noexcept {
//  std::cerr << "Project::backend_watcher starting for " << project_.m_filename << " on thread "
//            << std::this_thread::get_id() << std::endl;
  project_.m_backend_watcher_instance.reset(new sjef::Project(project_.m_filename,
                                                              true,
                                                              "",
                                                              {{}},
                                                              &project_));
  auto& project = *project_.m_backend_watcher_instance.get();
  if (max_wait_milliseconds <= 0) max_wait_milliseconds = min_wait_milliseconds;
  constexpr auto radix = 2;
  backend_watcher_wait_milliseconds = std::max(min_wait_milliseconds, 1);
  try {
//    std::cerr << "sjef::Project::backend_watcher() START for project " << project.name() << " at address " << &project
//              << ", backend " << backend
//              << ", " << project.property_get("backend")
//              << std::endl;
//    std::cerr << "ensure_remote_server call from server"<<std::endl;
    project.ensure_remote_server();
//    std::cerr << "ensure_remote_server returns to server"<<std::endl;
    auto& shutdown_flag = const_cast<sjef::Project*>(project.m_master_instance)->m_unmovables.shutdown_flag;
    for (auto iter = 0;
         true;
         ++iter) {
//      std::cerr << "iter " << iter ;
//      std::cerr << "; going to sleep for " << backend_watcher_wait_milliseconds << "ms" << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(backend_watcher_wait_milliseconds));
      backend_watcher_wait_milliseconds = std::max(std::min(backend_watcher_wait_milliseconds * radix,
                                                            max_wait_milliseconds),
                                                   min_wait_milliseconds <= 0 ? 1 : min_wait_milliseconds);
//      std::cerr << "... watcher for project " << &project << " waking up" << std::endl;
      if (shutdown_flag.test_and_set()) break;
      shutdown_flag.clear();
      try {
        project.synchronize(0);
      }
      catch (const std::exception& ex) {
        std::cerr << "sjef::Project::backend_watcher() synchronize() has thrown " << ex.what() << std::endl;
        project.cached_status(unknown);
      }
//      std::cerr << "... watcher for project "<<&project<<" back from synchronize"<<std::endl;
      try {
        project.cached_status(project.status(0, false));
      }
      catch (const std::exception& ex) {
        std::cerr << "sjef::Project::backend_watcher() status() has thrown " << ex.what() << std::endl;
        project.cached_status(unknown);
      }
//      std::cerr << "... watcher for project "<<&project<<" back from status"<<std::endl;
//      std::cerr << "sjef::Project::backend_watcher() status " << project.cached_status() << std::endl;
    }
  }
  catch (...) {
  }
//  std::cerr << "Project::backend_watcher stopping for " << project_.m_filename << " on thread "
//            << std::this_thread::get_id() << std::endl;
}

} // namespace sjef
