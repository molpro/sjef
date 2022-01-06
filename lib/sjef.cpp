#include "sjef.h"
#include "Locker.h"
#include "sjef-backend.h"
#include <array>
#include <boost/process/args.hpp>
#include <boost/process/child.hpp>
#include <boost/process/io.hpp>
#include <boost/process/search_path.hpp>
#include <boost/process/spawn.hpp>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <pugixml.hpp>
#include <regex>
#include <string>
#include <thread>
#include <unistd.h>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/wait.h>
#endif

namespace bp = boost::process;
namespace fs = std::filesystem;

int backend_watcher_wait_milliseconds;

///> @private
struct sjef::pugi_xml_document : public pugi::xml_document {};

///> @private
const std::string sjef::Project::s_propertyFile = "Info.plist";
const std::string writing_object_file = ".Info.plist.writing_object";

///> @private
inline std::string executable(fs::path command) {
  if (command.is_absolute())
    return command.string();
  else {
    constexpr bool use_boost_search_path = true;
    if (use_boost_search_path) {
//      std::cout << "executable(" << command << ") returns " << bp::search_path(command.string()).string() << std::endl;
      return bp::search_path(command.string()).string();
    } else {
      std::stringstream path{std::string{getenv("PATH")}}; // TODO windows
      std::string elem;
      while (std::getline(path, elem, ':')) {
        auto resolved = elem / command;
        // TODO check that it's executable
        if (fs::is_regular_file(resolved))
          return resolved.string();
      }
      return "";
    }
  }
}

///> @private
bool copyDir(fs::path const& source, fs::path const& destination, bool delete_source = false, bool recursive = true) {
  // Check whether the function call is valid
  if (!fs::exists(source) || !fs::is_directory(source))
    throw std::runtime_error("Source directory " + source.string() + " does not exist or is not a directory.");
  //  sjef::Locker source_locker(source);
  //  auto source_lock = source_locker.bolt();
  if (fs::exists(destination))
    throw std::runtime_error("Destination directory " + destination.string() + " already exists.");
  // Create the destination directory
  if (!fs::create_directory(destination))
    throw std::runtime_error("Unable to create destination directory " + destination.string());
  sjef::Locker destination_locker(source);
  auto destination_lock = destination_locker.bolt();
  // Iterate through the source directory
  for (fs::directory_iterator file(source); file != fs::directory_iterator(); ++file) {
    fs::path current(file->path());
    if (fs::is_directory(current)) {
      // Found directory: Recursion
      if (recursive) {
        if (!copyDir(current, destination / current.filename(), delete_source)) {
          return false;
        }
      }
    } else {
      // Found file: Copy
      if (current.filename() != ".lock")
        fs::copy_file(current, destination / current.filename());
    }
  }
  return true;
}

namespace sjef {
struct remote_server {
  bp::child process;
  bp::opstream in;
  bp::ipstream out;
  bp::ipstream err;
  std::string host;
  std::string last_out;
  std::string last_err;
};

inline std::string getattribute(pugi::xpath_node node, std::string name) {
  return node.node().attribute(name.c_str()).value();
}
const std::vector<std::string> Project::suffix_keys{"inp", "out", "xml"};
Project::Project(const std::string& filename, bool construct, const std::string& default_suffix,
                 const std::map<std::string, std::string>& suffixes, const Project* masterProject)
    : m_project_suffix(get_project_suffix(filename, default_suffix)),
      m_filename(expand_path(filename, m_project_suffix)),
      m_reserved_files(std::vector<std::string>{sjef::Project::s_propertyFile}),
      m_properties(std::make_unique<pugi_xml_document>()), m_suffixes(suffixes),
      m_backend_doc(std::make_unique<pugi_xml_document>()), m_status_lifetime(0),
      m_status_last(std::chrono::steady_clock::now()), m_master_instance(masterProject),
      m_master_of_slave(masterProject == nullptr), m_backend(""),
      m_locker(masterProject == nullptr ? std::make_shared<Locker>(m_filename) : masterProject->m_locker),
      m_property_file_modification_time(), m_run_directory_ignore({writing_object_file, name() + "_[^./\\\\]+\\..+"}) {
  {
    if (fs::exists(propertyFile())) {
      //      std::cout << "old project " << m_filename << std::endl;
      //          if (system((std::string{"ls -ltra "} + m_filename).c_str())) {}
      //          if (system((std::string{"cat "} + propertyFile()).c_str())) {}
      auto lock = m_locker->bolt();
      load_property_file_locked();
    } else {
      //      std::cout << "new project " << m_filename << std::endl;
      if (not fs::exists(m_filename))
        fs::create_directories(m_filename);
      //      save_property_file();
      //      property_set("backend","local");
      std::ofstream(propertyFile()) << "<?xml version=\"1.0\"?>\n"
                                       "<plist> <dict/> </plist>"
                                    << std::endl;
    }
    //    std::cerr << "Project constructor filename=" << filename << "address " << this << " master " <<
    //    m_master_of_slave
    //              << std::endl;
    auto recent_projects_directory = expand_path(std::string{"~/.sjef/"} + m_project_suffix);
    fs::create_directories(recent_projects_directory);
    m_recent_projects_file = expand_path(recent_projects_directory + "/projects");
    for (const auto& key : suffix_keys)
      if (m_suffixes.count(key) < 1)
        m_suffixes[key] = key;
    if (suffixes.count("inp") > 0)
      m_reserved_files.push_back(this->filename("inp"));
    if (!fs::exists(m_filename))
      throw std::runtime_error("project does not exist and could not be created: " + m_filename);
    if (!fs::is_directory(m_filename))
      throw std::runtime_error("project should be a directory: " + m_filename);

    //    std::cerr << "constructor m_filename=" << m_filename << " construct=" << construct << "<< address=" << this
    //              << ", m_master_instance=" << m_master_instance << std::endl;
    if (!construct)
      return;
    //    std::cout << "constructor for" << m_filename << " master=" << m_master_of_slave << std::endl;
    const std::string& pf = propertyFile();
    if (!fs::exists(pf) and m_master_of_slave) {
      //      std::cerr << "propertyFile doesn't exist, so saving to it" << std::endl;
      save_property_file();
      m_property_file_modification_time = fs::last_write_time(propertyFile());
      property_set("_private_sjef_project_backend_inactive", "1");
      //    std::cerr << "@@ backend_inactive set to "<<property_get("_private_sjef_project_backend_inactive");
    } else {
      if (not fs::exists(pf))
        throw std::runtime_error("Unexpected absence of property file " + pf);
      //      std::cerr << "propertyFile already exists " << fs::exists(pf) << ", " << fs::file_size(pf) << std::endl;
      //      if (system((std::string{"cat "} + pf).c_str())) {
      //      }
      //            load_property_file_locked();
      //    m_property_file_modification_time = (m_master_instance ?
      //    m_master_instance->m_property_file_modification_time : fs::last_write_time(propertyFile()));
      m_property_file_modification_time = fs::last_write_time(propertyFile());
      check_property_file_locked();
    }
    //    std::cout << "property testkey=" << property_get("testkey") << std::endl;
    //    std::cerr << "before property_set " << property_get("run_directories") << std::endl;
    property_set("_private_sjef_project_backend_inactive_synced", "0");
    //    std::cerr << "after property_set " << property_get("run_directories") << std::endl;
    cached_status(unevaluated);
    custom_initialisation();

    auto nimport = property_get("IMPORTED").empty() ? 0 : std::stoi(property_get("IMPORTED"));
    //    std::cerr << "nimport "<<nimport<<std::endl;
    for (int i = 0; i < nimport; i++) {
      //      std::cerr << "key "<<std::string{"IMPORT"}+std::to_string(i)<<", value
      //      "<<property_get(std::string{"IMPORT"}+std::to_string(i))<<std::endl;
      m_reserved_files.push_back(property_get(std::string{"IMPORT"} + std::to_string(i)));
    }
    // If this is a run-directory project, do not add to recent list
    if (fs::path{m_filename}.parent_path().filename().string() != "run" and
        not fs::exists(fs::path{m_filename}.parent_path().parent_path() / "Info.plist"))
      recent_edit(m_filename);

    m_backends[sjef::Backend::dummy_name] = sjef::Backend(
        sjef::Backend::dummy_name, "localhost", "{$PWD}",
        "/bin/sh -c 'echo dummy > ${0%.*}.out; echo \"<?xml version=\\\"1.0\\\"?>\n<root/>\" > ${0%.*}.xml'");
    if (not sjef::check_backends(m_project_suffix)) {
      auto config_file = expand_path(std::string{"~/.sjef/"} + m_project_suffix + "/backends.xml");
      std::cerr << "contents of " << config_file << ":" << std::endl;
      std::cerr << std::ifstream(config_file).rdbuf() << std::endl;
      throw std::runtime_error("sjef backend files are invalid");
    }
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
          if ((kVal = getattribute(be, "host")) != "")
            m_backends[kName].host = kVal;
          if ((kVal = getattribute(be, "cache")) != "")
            m_backends[kName].cache = kVal;
          if ((kVal = getattribute(be, "run_command")) != "")
            m_backends[kName].run_command = kVal;
          if ((kVal = getattribute(be, "run_jobnumber")) != "")
            m_backends[kName].run_jobnumber = kVal;
          if ((kVal = getattribute(be, "status_command")) != "")
            m_backends[kName].status_command = kVal;
          if ((kVal = getattribute(be, "status_running")) != "")
            m_backends[kName].status_running = kVal;
          if ((kVal = getattribute(be, "status_waiting")) != "")
            m_backends[kName].status_waiting = kVal;
          if ((kVal = getattribute(be, "kill_command")) != "")
            m_backends[kName].kill_command = kVal;
          if (not check_backend(kName))
            throw std::runtime_error(std::string{"sjef backend "} + kName + " is invalid");
        }
      }
    }
    if (m_backends.count(sjef::Backend::default_name) == 0) {
      m_backends[sjef::Backend::default_name] = default_backend();
      const auto config_file = expand_path("~/.sjef/" + m_project_suffix + "/backends.xml");
      if (not fs::exists(config_file))
        std::ofstream(config_file) << "<?xml version=\"1.0\"?>\n<backends>\n</backends>" << std::endl;
      {
        Locker locker{fs::path{config_file}.parent_path()};
        auto lock = locker.bolt();
        fs::copy_file(config_file, config_file + "-");
        auto in = std::ifstream(config_file + "-");
        auto out = std::ofstream(config_file);
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

    // std::cerr << "project constructor name()="<<name()<<std::endl;
    //    std::cout << "near end of constructor (about to call change_backend) for" << m_filename
    //              << " master=" << m_master_of_slave << std::endl;
    //    std::cout << "property testkey=" << property_get("testkey") << std::endl;
  }
  if (not name().empty() and name().front() != '.') {
    auto be = property_get("backend");
    if (m_backends.count(be) == 0)
      be = sjef::Backend::default_name;
    change_backend(be, true);
  }
  //  std::cout << "end of constructor for" << m_filename << " master=" << m_master_of_slave << std::endl;
}

Project::~Project() {
  //  std::cerr << "enter destructor for project " << name() << " address " << this
  //            << ", m_master_instance=" << m_master_instance << std::endl;
  //  std::cout << "in destructor:\n" << std::ifstream(fs::path{m_filename} / "Info.plist").rdbuf() << "" << std::endl;
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
    throw std::runtime_error("Cannot deduce project suffix for \"" + filename + "\" with default suffix \"" +
                             default_suffix + "\"");
  return suffix.substr(1);
}

bool Project::import_file(std::string file, bool overwrite) {
  auto to = fs::path{m_filename} / fs::path{file}.filename();
  for (const auto& key : suffix_keys)
    if (fs::path{file}.extension() == m_suffixes[key])
      to = fs::path{m_filename} / fs::path{name()} / m_suffixes[key];
  // TODO: implement generation of .inp from .xml
  std::error_code ec;
  //  std::cerr << "Import copies from "<<file<<" to "<<to<<std::endl;
  if (overwrite and exists(to))
    remove(to);
  fs::copy_file(file, to, ec);
  m_reserved_files.push_back(to.string());
  auto nimport = property_get("IMPORTED").empty() ? 0 : std::stoi(property_get("IMPORTED"));
  std::string key = "IMPORT" + std::to_string(nimport);
  property_set(key, to.filename().string());
  property_set("IMPORTED", std::to_string(nimport + 1));
  if (ec)
    throw std::runtime_error(ec.message());
  return true;
}

void Project::throw_if_backend_invalid(std::string backend) const {
  if (backend.empty())
    backend = property_get("backend");
  if (backend.empty())
    throw std::runtime_error("No backend specified");
  if (m_backends.count(backend) > 0)
    return;
  const std::string& path = expand_path(std::string{"~/.sjef/"} + m_project_suffix + "/backends.xml");
  std::cerr << "Contents of " << path << ":\n" << std::ifstream(path).rdbuf() << std::endl;
  throw std::runtime_error("Backend " + backend + " is not registered");
}

bool Project::export_file(std::string file, bool overwrite) {
  throw_if_backend_invalid();
  if (!property_get("backend").empty())
    synchronize(0);
  auto from = fs::path{m_filename};
  from /= fs::path{file}.filename();
  std::error_code ec;
  if (overwrite and exists(fs::path{file}))
    remove(fs::path{file});
  fs::copy_file(from, file, ec);
  if (ec)
    throw std::runtime_error(ec.message());
  return true;
}

std::mutex synchronize_mutex;
bool Project::synchronize(int verbosity, bool nostatus, bool force) const {
  //      verbosity += 2;
  if (verbosity > 0)
    std::cerr << "Project::synchronize(" << verbosity << ", " << nostatus << ", " << force << ") "
              << (m_master_of_slave ? "master" : "slave") << std::endl;
  //  const std::lock_guard lock(m_synchronize_mutex);
  auto backend_name = property_get("backend");
  auto backend_changed = m_backend != backend_name;
  if (backend_changed)
    const_cast<Project*>(this)->change_backend(backend_name);
  const std::lock_guard lock(synchronize_mutex);
  auto& backend = m_backends.at(backend_name);
  if (verbosity > 2)
    std::cerr << "synchronize with " << backend.name << " (" << backend.host << ") master=" << m_master_of_slave
              << std::endl;
  if (backend.host == "localhost")
    return true;
  if (verbosity > 2)
    std::cerr << "synchronize backend_inactive=" << property_get("_private_sjef_project_backend_inactive")
              << " backend_inactive_synced=" << property_get("_private_sjef_project_backend_inactive_synced")
              << std::endl;
  //  std::cerr << "input exists ? " <<fs::exists(filename("inp")) << std::endl;
  //  std::cerr << "compare write times "<<fs::last_write_time(filename("inp")) << " : " <<
  //  m_property_file_modification_time << std::endl;
  bool locally_modified;
  {
    //    Lock pl(propertyFile()+".lock");
    auto lock = m_locker->bolt();
    locally_modified = m_property_file_modification_time != fs::last_write_time(propertyFile());
    //    std::cerr << "initial locally_modified=" << locally_modified << std::endl;
    for (const auto& suffix : {"inp", "xyz"}) {
      if (fs::exists(filename(suffix))) {
        auto lm = fs::last_write_time(filename(suffix));
        locally_modified = locally_modified or lm > m_input_file_modification_time[suffix];
        m_input_file_modification_time[suffix] = lm;
      }
    }
  }
  //  std::cerr << "locally_modified=" << locally_modified << std::endl;
  if (not force and not locally_modified and not backend_changed and
      std::stoi(property_get("_private_sjef_project_backend_inactive_synced")) > 2)
    return true;
  //  std::cerr << "really syncing" << std::endl;
  // TODO: implement more robust error checking
  // std::cerr << "synchronize calls change_backend"<<std::endl;
  //   const_cast<Project*>(this)->change_backend(backend.name);
  //   if (m_master_of_slave) {
  //   ensure_remote_server();
  //  absolutely send reserved files
  std::string rsync = "rsync";
  auto rsync_command = bp::search_path("rsync");
  std::vector<std::string> rsync_options{
      "--timeout=5",       "--exclude=backup", "--exclude=*.out_*",    "--exclude=*.xml_*",
      "--exclude=*.log_*", "--exclude=*.d",    "--exclude=Info.plist", "--exclude=.Info.plist.writing_object",
      "--inplace"};
  if (true) {
    rsync_options.push_back("-e");
    rsync_options.push_back(
        "ssh -o ControlPath=~/.ssh/sjef-control-%h-%p-%r -o ControlMaster=auto -o ControlPersist=yes");
  }
  if (verbosity > 0)
    rsync_options.push_back("-v");
  auto rsync_options_first = rsync_options;
  auto rsync_options_second = rsync_options;
  rsync_options_first.push_back("--inplace");
  rsync_options_first.push_back("--exclude=*.out");
  rsync_options_first.push_back("--exclude=*.xml");
  rsync_options_first.push_back("--exclude=*.log");
  rsync_options_first.push_back("-L");
  rsync_options_first.push_back("-a");
  rsync_options_first.push_back("--update");
  //  rsync_options_first.push_back("--mkpath"); // needs rsync >= 3.2.3
  rsync_options_first.push_back(m_filename + "/");
  rsync_options_first.push_back(backend.host + ":" + (fs::path{backend.cache} / m_filename).string());
  if (verbosity > 0) {
    std::cerr << "Push rsync: " << rsync_command;
    for (const auto& o : rsync_options_first)
      std::cerr << " '" << o << "'";
    std::cerr << std::endl;
  }
  auto start_time = std::chrono::steady_clock::now();
  //  bp::child(cmd).wait();
  bp::child(bp::search_path(rsync), rsync_options_first).wait();
  if (verbosity > 1)
    std::cerr
        << "time for first rsync "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << "ms" << std::endl;
  // fetch all newer files from backend
  if (std::stoi(property_get("_private_sjef_project_backend_inactive_synced")) > 2) {
    // std::cerr << "second rsync not taken" << std::endl;
    return true;
  }
  const auto backend_inactive = property_get("_private_sjef_project_backend_inactive");
  //  std::cerr << "backend_inactive="<<backend_inactive<<status_message()<<std::endl;
  {
    rsync_options_second.push_back("-a");
    //    rsync_options_second.push_back("--update");
    for (const auto& rf : m_reserved_files) {
      //    std::cerr << "reserved file pattern " << rf << std::endl;
      auto f = regex_replace(fs::path{rf}.filename().string(), std::regex(R"--(%)--"), name());
      //    std::cerr << "reserved file resolved " << f << std::endl;
      //      if (fs::exists(f))
      //        cmd += "--exclude=" + f + " ";
      if (fs::exists(f))
        rsync_options_second.push_back("--exclude=" + f);
    }
    rsync_options_second.push_back(backend.host + ":" + (fs::path{backend.cache} / m_filename).string() + "/");
    rsync_options_second.push_back(m_filename);
    if (verbosity > 0) {
      std::cerr << "Pull rsync: " << rsync_command;
      for (const auto& o : rsync_options_second)
        std::cerr << " '" << o << "'";
      std::cerr << std::endl;
    }
    auto start_time = std::chrono::steady_clock::now();
    bp::child(rsync_command, rsync_options_second).wait();
    if (verbosity > 1)
      std::cerr << "time for second rsync "
                << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time)
                       .count()
                << "ms" << std::endl;
  }
  if (not nostatus) // to avoid infinite loop with call from status()
    status(0);      // to get backend_inactive
  // std::cerr << "synchronize backend_inactive=" << property_get("_private_sjef_project_backend_inactive") <<
  // std::endl;
  if (backend_inactive != "0") {
    const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive", backend_inactive);
    auto n = std::stoi(property_get("_private_sjef_project_backend_inactive_synced"));
    const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive_synced", std::to_string(n + 1));
    //        std::cerr << "advancing count to " << n + 1 << std::endl;
  } else {
    const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive", "0");
    const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive_synced", "0");
  }
  //  std::cerr << "synchronize backend_inactive_synced=" <<
  //  property_get("_private_sjef_project_backend_inactive_synced") << std::endl;
  return true;
}

void Project::force_file_names(const std::string& oldname) {

  m_locker = std::make_unique<Locker>(m_filename);
  fs::directory_iterator end_iter;
  for (fs::directory_iterator dir_itr(m_filename); dir_itr != end_iter; ++dir_itr) {
    auto path = dir_itr->path();
    try {
      auto ext = path.extension().string();
      if (path.stem() == oldname and not ext.empty() and m_suffixes.count(ext.substr(1)) > 0) {
        auto newpath = path.parent_path();
        newpath /= name();
        newpath.replace_extension(dir_itr->path().extension());
        //        std::cerr << "rename(" << path.filename() << "," << newpath.filename() << ")" << std::endl;
        rename(path, newpath);

        if (newpath.extension() == ".inp")
          rewrite_input_file(newpath.string(), oldname);
        for (const auto& key : property_names()) {
          auto value = property_get(key);
          if (value == path.filename().string())
            property_set(key, newpath.filename().string());
        }
      }
    } catch (const std::exception& ex) {
      throw std::runtime_error(dir_itr->path().string() + " " + ex.what());
    }
  }
}

std::string Project::propertyFile() const { return (fs::path{m_filename} / fs::path{s_propertyFile}).string(); }

bool Project::move(const std::string& destination_filename, bool force) {
  auto stat = status(-1);
  if (stat == running or stat == waiting)
    return false;
  auto dest = fs::absolute(expand_path(destination_filename, fs::path{m_filename}.extension().string().substr(1)));
  //  std::cerr << "move to " << dest << std::endl;
  if (force)
    fs::remove_all(dest);
  if (!property_get("backend").empty())
    synchronize();
  auto namesave = name();
  auto filenamesave = m_filename;
  shutdown_backend_watcher();
  try {
    //    fs::copy(fs::path(m_filename), dest, fs::copy_options::recursive);
    if (not copyDir(fs::path(m_filename), dest, true))
      throw std::runtime_error("Failed to copy current project directory");
    m_filename = dest.string();
    force_file_names(namesave);
    recent_edit(m_filename, filenamesave);
    if (not fs::remove_all(filenamesave))
      throw std::runtime_error("failed to delete current project directory");
    return true;
  } catch (...) {
    std::cerr << "move failed to copy " << m_filename << " : " << dest << std::endl;
  }
  change_backend(property_get("backend"));
  return false;
}

bool Project::copy(const std::string& destination_filename, bool force, bool keep_hash, bool slave) {
  auto dest = fs::absolute(expand_path(destination_filename, fs::path{m_filename}.extension().string().substr(1)));
  try { // try to synchronize if we can, but still do the copy if not
    if (!property_get("backend").empty())
      synchronize();
  } catch (...) {
  }
  {
    if (force)
      fs::remove_all(dest);
    if (fs::exists(dest))
      throw std::runtime_error("Copy to " + dest.string() + " cannot be done because the destination already exists");
    //    fs::copy(fs::path(m_filename), dest, (slave ? fs::copy_options::none : fs::copy_options::recursive));
    if (not copyDir(fs::path(m_filename), dest, false, not slave))
      //      throw std::runtime_error("Failed to copy current project directory");
      return false;
  }
  Project dp(dest.string());
  //  std::cout << "copy() source properties:";
  //  for (const auto& n : property_names())
  //    std::cout << " " << n;
  //  std::cout << std::endl;
  //  if (system((std::string{"cat "}+propertyFile()).c_str()));
  //  std::cout << "copy() dest properties:";
  //  for (const auto& n : dp.property_names())
  //    std::cout << " " << n;
  //  std::cout << std::endl;
  //  if (system((std::string{"cat "}+dp.propertyFile()).c_str()));
  dp.force_file_names(name());
  if (not slave)
    recent_edit(dp.m_filename);
  dp.property_delete("jobnumber");
  if (slave)
    dp.property_delete("run_directories");
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
  do {
    while (*str == c && *str)
      ++str;
    const char* begin = str;
    //    if (*begin == quote) std::cerr << "opening quote found: " << begin << std::endl;
    while (*str && (*str != c || (*begin == quote && str > begin && *(str - 1) != quote)))
      ++str;
    //    std::cerr << "rejecting " <<  *str << std::endl;
    //    std::cerr << "taking " << std::string(begin, str) << std::endl;
    if (*begin == quote and str > begin + 1 and *(str - 1) == quote)
      result.push_back(std::string(begin + 1, str - 1));
    else
      result.push_back(std::string(begin, str));
    if (result.back().empty())
      result.pop_back();
  } while (0 != *str++);
  free((void*)str0);
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
  if (templ.empty())
    templ = backend_get(backend, "run_command");
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
      if (first != '{')
        m[0] = first;
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
  std::sregex_token_iterator begin(templ_.begin(), templ_.end(), re, {-1, 0}), end;
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
      if (first != '{')
        m[0] = first;
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

  std::sregex_token_iterator begin(templ.begin(), templ.end(), re, {-1, 0}), end;
  std::for_each(begin, end, callback);

  return result;
}

bool Project::run(int verbosity, bool force, bool wait) {
  auto& backend = m_backends.at(property_get("backend"));
  auto stat = status(verbosity);
  if (stat == running || stat == waiting) {
    return false;
  }

  //  std::cout <<"@@ before locking"<<std::endl;
  //  auto p_status_mutex=std::make_unique<std::lock_guard<std::mutex>>(m_status_mutex);
  //  std::cout <<"@@ after locking"<<std::endl;
  if (verbosity > 0)
    std::cerr << "Project::run() run_needed()=" << run_needed(verbosity) << std::endl;
  if (not force and not run_needed())
    return false;
  //  std::cout <<"@@ after run_needed"<<std::endl;
  cached_status(unevaluated);
  backend_watcher_wait_milliseconds = 0;
  std::string line;
  bp::child c;
  std::string optionstring;
  //  std::cerr << "setting run_input_hash to input_hash()=" << input_hash() << std::endl;
  property_set("run_input_hash", std::to_string(input_hash()));
  if (verbosity > 0 and backend.name != sjef::Backend::dummy_name)
    optionstring += "-v ";
  //  std::cerr << "backend.run_command before expand "<<backend.run_command<<std::endl;
  auto run_command = backend_parameter_expand(backend.name, backend.run_command);
  //  std::cerr << "run_command after expand "<<run_command<<std::endl;
  custom_run_preface();
  //  std::cout <<"@@ before creating new rundir"<<std::endl;
  //  p_status_mutex.reset();
  auto rundir = run_directory_new();
  auto p_status_mutex = std::make_unique<std::lock_guard<std::mutex>>(m_status_mutex);
  //  std::cout <<"@@ created new rundir "<<rundir<<std::endl;
  if (backend.host == "localhost") {
    property_set("_private_sjef_project_backend_inactive", "0");
    property_set("_private_sjef_project_backend_inactive_synced", "0");
    if (verbosity > 0)
      std::cerr << "run local job, backend=" << backend.name << std::endl;
    auto spl = splitString(run_command);
    run_command = spl.front();
    //    spl.erase(spl.begin());
    //    for (const auto& sp : spl)
    for (auto sp = spl.rbegin(); sp < spl.rend() - 1; sp++)
      optionstring = "'" + *sp + "' " + optionstring;
    if (executable(run_command).empty()) {
      for (const auto& p : ::boost::this_process::path())
        std::cerr << "path " << p << std::endl;
      throw std::runtime_error("Cannot find run command " + run_command);
    }
    if (verbosity > 1)
      std::cerr << "run local job executable=" << executable(run_command) << " " << optionstring << " "
                << filename("inp", "", rundir) << std::endl;
    if (verbosity > 2)
      for (const auto& o : splitString(optionstring))
        std::cerr << "option " << o << std::endl;
    fs::path current_path_save;
    try {
      current_path_save = fs::current_path();
    } catch (...) {
      current_path_save = "";
    }
    fs::current_path(filename("", "", 0));

    if (optionstring.empty())
      c = bp::child(executable(run_command),
                    //                    fs::path{m_filename} / fs::path(this->name() + ".inp")
                    fs::relative(filename("inp", "", rundir)).string());
    else
      c = bp::child(executable(run_command), bp::args(splitString(optionstring)),
                    //                    fs::path{m_filename} / fs::path(this->name() + ".inp")
                    fs::relative(filename("inp", "", rundir)).string());
    fs::current_path(current_path_save);
    if (not c.valid())
      throw std::runtime_error("Spawning run process has failed");
    c.detach();
    property_set("jobnumber", std::to_string(c.id()));
    p_status_mutex.reset();
    status(0, false); // to force status cache
    if (verbosity > 1)
      std::cerr << "jobnumber " << c.id() << ", running=" << c.running() << std::endl;
    if (wait)
      this->wait();
    return true;
  } else { // remote host
    if (verbosity > 0)
      std::cerr << "run remote job on " << backend.name << std::endl;
    bp::ipstream c_err, c_out;
    //    property_set("_private_sjef_project_backend_inactive_synced", 0);
    //    std::cout << "!! synchronize before submission" << std::endl;
    synchronize(verbosity, false, true);
    //    std::cout << "!! end synchronize before submission" << std::endl;
    cached_status(unknown);
    property_set("_private_sjef_project_backend_inactive", "0");
    property_set("_private_sjef_project_backend_inactive_synced", "0");
    if (verbosity > 3)
      std::cerr << "fs::path{backend.cache} / filename("
                   ","
                   ",rundir) "
                << fs::path{backend.cache} / filename("", "", rundir) << std::endl;
    auto jobstring = std::string{"cd "} + (fs::path{backend.cache} / filename("", "", rundir)).string() + "; nohup " +
                     run_command + " " + optionstring + fs::path{filename("inp", "", rundir)}.filename().string();
    if (backend.run_jobnumber == "([0-9]+)")
      jobstring += "& echo $! "; // go asynchronous if a straight launch
    if (verbosity > 3)
      std::cerr << "backend.run_jobnumber " << backend.run_jobnumber << std::endl;
    if (verbosity > 3)
      std::cerr << "jobstring " << jobstring << std::endl;
    cached_status(unevaluated);
    std::string run_output;
    {
      run_output = remote_server_run(jobstring);
      cached_status(unevaluated);
      //      std::cout << "cached status after setting to unevaluated: "<<cached_status()<<std::endl;
    }
    //    std::cout << "just before releasing status mutex, status="<<cached_status()<<std::endl;
    property_set("_private_job_submission_time", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                                    std::chrono::system_clock::now().time_since_epoch())
                                                                    .count()));
    p_status_mutex.reset();
    //    std::cout << "status after setting to unevaluated: "<<status()<<std::endl;
    //    std::cout << "run_output "<<run_output<<std::endl;
    synchronize(verbosity, false, true);
    status(0, false);
    //    std::cout << "status after forcing evaluation: "<<status()<<std::endl;
    if (verbosity > 3)
      std::cerr << "run_output " << run_output << std::endl;
    std::smatch match;
    if (std::regex_search(run_output, match, std::regex{backend.run_jobnumber})) {
      if (verbosity > 2)
        std::cerr << "... a match was found: " << match[1] << std::endl;
      if (verbosity > 1)
        status(verbosity - 2);
      property_set("jobnumber", match[1]);
      //      std::cout << " set property jobnumber = "<<property_get("jobnumber")<<std::endl;
      if (wait)
        this->wait();
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
  if (property_get("backend").empty())
    return;
  throw_if_backend_invalid();
  auto be = m_backends.at(property_get("backend"));
  auto pid = property_get("jobnumber");
  if (pid.empty())
    return;
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
  if (verbosity > 0)
    //    std::cerr << "sjef::Project::run_needed, status=" << cached_status() << std::endl;
    if (verbosity > 1)
      std::cerr << ", time "
                << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time)
                       .count()
                << std::endl;
  auto statuss = cached_status();
  if (statuss == running or statuss == waiting)
    return false;
  auto inpfile = filename("inp");
  auto xmlfile = filename("xml", "", 0);
  if (verbosity > 1) {
    std::cout << "inpfile " << inpfile << std::endl;
    std::cout << "xmlfile " << xmlfile << std::endl;
    std::cerr
        << ", time "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << std::endl;
  }
  if (verbosity > 0)
    std::cerr << "sjef::Project::run_needed, input file exists ?=" << fs::exists(inpfile) << std::endl;
  if (verbosity > 1)
    std::cerr
        << ", time "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << std::endl;
  if (not fs::exists(inpfile))
    return false;
  if (verbosity > 0)
    std::cerr << "sjef::Project::run_needed, xml file exists ?=" << fs::exists(xmlfile) << std::endl;
  if (verbosity > 1)
    std::cerr
        << ", time "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << std::endl;
  if (not fs::exists(xmlfile))
    return true;
  //  if (fs::last_write_time(xmlfile) < fs::last_write_time(inpfile)) return true;
  if (verbosity > 1)
    std::cerr
        << "sjef::Project::run_needed, time after initial checks "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << std::endl;
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
    std::cerr
        << "before property_get, time "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << std::endl;
  auto run_input_hash = property_get("run_input_hash");
  if (verbosity > 1)
    std::cerr
        << "after property_get, time "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << std::endl;
  if (run_input_hash.empty()) { // if there's no input hash, look at the xml file instead
                                //      std::cerr << input_from_output() << std::endl;
                                //    std::cerr << file_contents("inp") << std::endl;
                                //      std::cerr << input_from_output() << std::endl;
    //    std::cerr << std::regex_replace(std::regex_replace(file_contents("inp"), std::regex{"\r"}, ""), std::regex{"
    //    *\n\n*"}, "\n") << std::endl;
    if (verbosity > 1)
      std::cerr << "There's no run_input_hash, so compare output and input: "
                << (std::regex_replace(std::regex_replace(file_contents("inp"), std::regex{"\r"}, ""),
                                       std::regex{" *\n\n*"}, "\n") != input_from_output())
                << std::endl;
    return (std::regex_replace(std::regex_replace(std::regex_replace(file_contents("inp"), std::regex{"\r"}, ""),
                                                  std::regex{" *\n\n*"}, "\n"),
                               std::regex{"\n$"}, "") != input_from_output());
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
      std::cerr << "sjef::Project::run_needed, input hash matches ?=" << (i_run_input_hash == input_hash())
                << std::endl;
      //      std::cerr << "ending"
      //                << ", time " << std::chrono::duration_cast<std::chrono::milliseconds>(
      //          std::chrono::steady_clock::now() - start_time).count()
      //                << std::endl;
    }
    if (i_run_input_hash != input_hash()) {
      if (verbosity > 1) {
        std::cerr << "sjef::Project::run_needed returning true" << std::endl;
        std::cerr << "ending time "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                           start_time)
                         .count()
                  << std::endl;
        std::cerr << "because i_run_input_hash != input_hash()" << std::endl;
      }
      return true;
    }
  }
  if (verbosity > 1) {
    std::cerr << "sjef::Project::run_needed returning false" << std::endl;
    std::cerr
        << "ending"
        << ", time "
        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
        << std::endl;
  }
  return false;
}

std::string m_xml_cached;
std::string Project::xml(int run, bool sync) const {
  constexpr bool use_cache = true;
  const bool localhost = m_backends.at(property_get("backend")).host == "localhost";
  if ((not use_cache) or (localhost and cached_status() != completed) or
      ((not localhost) and std::stoi(property_get("_private_sjef_project_backend_inactive_synced")) <= 2)) {
    //    std::cout << "not creating xml cache, status="<<cached_status() << std::endl;
    return xmlRepair(file_contents(m_suffixes.at("xml"), "", run, sync));
  }
  if (m_xml_cached.empty())
    //    std::cout << "creating xml cache" << std::endl;
    if (m_xml_cached.empty())
      m_xml_cached = xmlRepair(file_contents(m_suffixes.at("xml"), "", run, sync));
  //  std::cout << "using xml cache" << std::endl;
  return m_xml_cached;
}

std::string Project::file_contents(const std::string& suffix, const std::string& name, int run, bool sync) const {

  //  std::cerr << "file_contents " << suffix << ": " << property_get("_private_sjef_project_backend_inactive_synced")
  //            << std::endl;
  auto be = property_get("backend");
  if (sync and not be.empty() and be != "local" and
      std::stoi(property_get("_private_sjef_project_backend_inactive_synced")) > 2 and suffix != m_suffixes.at("inp"))
    synchronize();
  //  std::cout << "file_contents " << filename(suffix, name, run) << std::endl;
  //  system((std::string{"ls -lR "}+filename(suffix,name,run)).c_str());
  std::ifstream s(filename(suffix, name, run));
  auto result = std::string(std::istreambuf_iterator<char>(s), std::istreambuf_iterator<char>());
  while (result.back() == '\n')
    result.pop_back();
  return result;
}

status Project::status(int verbosity, bool cached) const {
  //  if (cached and cached_status() != unevaluated)
  //    std::cerr << "using cached status " << cached_status() << ", so returning immediately" << std::endl;
  //  else if (cached)
  //    std::cerr << "want to use cached status but cannot because not yet evaluated " << cached_status() << std::endl;
  //  else
  //    std::cerr << "do not want to use cached status " << cached_status() << std::endl;
  // if (not m_master_of_slave) std::cerr << "slave enters status() "<<std::endl;
  if (cached and cached_status() != unevaluated and m_master_of_slave)
    return cached_status();
  const std::lock_guard lock(m_status_mutex);
  if (property_get("backend").empty())
    return unknown;
  //  if (not m_master_of_slave) std::cerr << "slave still in status() "<<std::endl;
  auto start_time = std::chrono::steady_clock::now();
  auto bes = property_get("backend");
  if (bes.empty())
    bes = sjef::Backend::default_name;
  throw_if_backend_invalid(bes);
  auto be = m_backends.at(bes);
  if (verbosity > 1)
    std::cerr << "status() backend:\n====" << be.str() << "\n====" << std::endl;

  {
    const std::string& job_submission_time = property_get("_private_job_submission_time");
    if (not job_submission_time.empty()) {
      std::stringstream ss;
      ss << job_submission_time;
      std::chrono::milliseconds::rep iob_submission_time;
      ss >> iob_submission_time;
      //      std::cout << "job_submission_time: " << iob_submission_time << std::endl;
      auto now =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
              .count();
      //      std::cout << "now: " << now <<" -> "<<now-iob_submission_time<< std::endl;
      if (now - iob_submission_time < 1000)
        return unknown; // don't do any status parsing until this much time has elapsed since job submission started
    }
  }

  auto pid = property_get("jobnumber");
  if (verbosity > 1)
    std::cerr << "job number " << pid << std::endl;
  if (pid.empty() or std::stoi(pid) < 0) {
    auto ih = std::to_string(input_hash());
    auto rih = property_get("run_input_hash");
    //    std::cerr << ih << " : " << rih<<std::endl;
    if (!rih.empty())
      return (rih == ih)
                 ? ((be.host + ":" + pid) == property_get("_private_sjef_project_killed_job") ? killed : completed)
                 : unknown;
    return (std::regex_replace(file_contents("inp"), std::regex{" *\n\n*"}, "\n") != input_from_output(false))
               ? unknown
               : completed;
  }
  //  std::cerr << "did not return unknown for empty pid "<<pid << std::endl;
  const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive", "0");
  //  std::cerr << "test completed_job "<<property_get("_private_sjef_project_completed_job")<< ",
  //  pid="<<pid<<std::endl;
  if (property_get("_private_sjef_project_completed_job") == be.host + ":" + pid and cached_status() != unevaluated) {
    //            std::cerr << "status return completed/killed because _private_sjef_project_completed_job is valid" <<
    //            "; changing backend_inactive from "<<property_get("_private_sjef_project_backend_inactive")<<" to 1"<<
    //            std::endl;
    const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive", "1");
    return ((be.host + ":" + pid) == property_get("_private_sjef_project_killed_job") ? killed : completed);
  }
  this->m_xml_cached.clear();
  auto result = pid == ""
                    ? unknown
                    : ((be.host + ":" + pid) == property_get("_private_sjef_project_killed_job") ? killed : completed);
  if (be.host == "localhost") {
    bp::child c;
    bp::ipstream is;
    std::string line;
    auto cmd = splitString(be.status_command + " " + pid);
    c = bp::child(executable(cmd[0]), bp::args(std::vector<std::string>{cmd.begin() + 1, cmd.end()}), bp::std_out > is);
    while (std::getline(is, line)) {
      while (isspace(line.back()))
        line.pop_back();
      //      std::cout << "line: @"<<line<<"@"<<std::endl;
      if ((" " + line).find(" " + pid + " ") != std::string::npos) {
        if (line.back() == '+')
          line.pop_back();
        if (line.back() == 'Z') { // zombie process
#if defined(__linux__) || defined(__APPLE__)
          waitpid(std::stoi(pid), NULL, WNOHANG);
#endif
          result = ((be.host + ":" + pid) == property_get("_private_sjef_project_killed_job") ? killed : completed);
        }
        result = running;
      }
    }
    c.wait();
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
      while (std::getline(m_remote_server->out, line) && line != "@@@!!EOF") {
        if (verbosity > 0)
          std::cerr << "line received: " << line << std::endl;
        if ((" " + line).find(" " + pid + " ") != std::string::npos) {
          std::smatch match;
          if (verbosity > 2)
            std::cerr << "line" << line << std::endl;
          if (verbosity > 2)
            std::cerr << "status_running " << be.status_running << std::endl;
          if (verbosity > 2)
            std::cerr << "status_waiting " << be.status_waiting << std::endl;
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
  if (verbosity > 2)
    std::cerr << "received status " << result << std::endl;
  if (result == completed || result == killed) {
    //    const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive_synced",
    //    property_get("_private_sjef_project_backend_inactive"));
    const_cast<Project*>(this)->property_set("_private_sjef_project_completed_job", be.host + ":" + pid);
    //    std::cerr << "completed_job set to "<<property_get("_private_sjef_project_completed_job");
  }
  if (result != unknown) {
    //    std::cerr << "result is not unknown, but is " << result << std::endl;
    if (result == running)
      const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive_synced", "0");
    if (result == running)
      //    std::cerr << "@@@ backend_inactive_synced set to
      //    "<<property_get("_private_sjef_project_backend_inactive_synced");
      const_cast<Project*>(this)->property_set("_private_sjef_project_backend_inactive",
                                               (result == completed || result == killed) ? "1" : "0");
    //    std::cerr << "@@@ backend_inactive set to "<<property_get("_private_sjef_project_backend_inactive");
    goto return_point;
  }
  if (verbosity > 2)
    std::cerr << "fallen through loop" << std::endl;
  synchronize(verbosity, true);
  const_cast<Project*>(this)->property_set(
      "_private_sjef_project_backend_inactive",
      ((result != completed && result != killed) or
       fs::exists(filename("xml", "", 0)) // there may be a race, where the job status is completed, but the output
                                          // file is not yet there. This is an imperfect test for that .. just whether
                                          // the .xml exists at all. TODO: improve test for complete output file
       )
          ? "1"
          : "0");
  //  std::cerr << "@@@@ backend_inactive set to "<<property_get("_private_sjef_project_backend_inactive");
return_point:
  auto time_taken =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
  if (time_taken < m_status_lifetime)
    m_status_lifetime = time_taken;
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
  if (current_status.empty() or status != std::stoi(current_status)) {
    //    std::cout << "force-setting _status to "<<status<<std::endl;
    const_cast<Project*>(this)->property_set("_status", std::to_string(static_cast<int>(status)));
    //    std::cout << "after force-setting _status its value is "<<property_get("_status")<<std::endl;
  }
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
    result += ", job number " + property_get("jobnumber") + " on backend " + property_get("backend");
  return result;
}

void Project::wait(unsigned int maximum_microseconds) const {
  unsigned int microseconds = 1;
  //  std::cout << "wait enters with status " << status() << std::endl;
  while (true) {
    auto stat = status();
    //    std::cout << "stat=" << stat << std::endl;
    if (stat == completed or stat == killed)
      break;
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
    if (microseconds < maximum_microseconds)
      microseconds *= 2;
  }
  //  std::cout << "wait returns with status " << status() << std::endl;
}

void Project::property_delete(const std::vector<std::string>& properties) {
  auto lock = m_locker->bolt();
  check_property_file_locked();
  for (const auto& property : properties)
    property_delete_locked(property);
  save_property_file_locked();
}

void Project::property_delete(const std::string& property) { // TODO make atomic
  property_delete(std::vector<std::string>{property});
}

void Project::property_delete_locked(const std::string& property) {
  //  std::cerr << "property_delete " << property << " save=" << save << std::endl;
  if (!m_properties->child("plist"))
    m_properties->append_child("plist");
  if (!m_properties->child("plist").child("dict"))
    m_properties->child("plist").append_child("dict");
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
  auto lock = m_locker->bolt();
  //  std::cout << "property_set " << properties.begin()->first << " about to call check_property_file_locked "
  //            << m_master_of_slave << std::endl;
  check_property_file_locked();
  for (const auto& keyval : properties) {
    const auto& property = keyval.first;
    const auto& value = keyval.second;
    //    std::cout << "property_set " << property << " = " << value << " on thread " << m_master_of_slave << std::endl;

    property_delete_locked(property);
    std::lock_guard guard(m_unmovables.m_property_set_mutex);
    if (!m_properties->child("plist"))
      m_properties->append_child("plist");
    if (!m_properties->child("plist").child("dict"))
      m_properties->child("plist").append_child("dict");
    auto keynode = m_properties->child("plist").child("dict").append_child("key");
    keynode.text() = property.c_str();
    auto stringnode = m_properties->child("plist").child("dict").append_child("string");
    stringnode.text() = value.c_str();
  }
  save_property_file_locked();
}

void Project::property_set(const std::string& property, const std::string& value) { property_set({{property, value}}); }

std::string Project::property_get(const std::string& property) const {
  return property_get(std::vector<std::string>{property})[property];
}
std::map<std::string, std::string> Project::property_get(const std::vector<std::string>& properties) const {
  //  std::cout << "property_get "<<properties.front()<<" about to call check_property_file_locked
  //  "<<m_master_of_slave<<std::endl;
  check_property_file();
  std::map<std::string, std::string> results;
  for (const std::string& property : properties) {
    std::string query{"/plist/dict/key[text()='" + property + "']/following-sibling::string[1]"};
    //    std::string result = m_properties->select_node(query.c_str()).node().child_value();
    //    result = m_properties->select_node(query.c_str()).node().child_value();
    //  std::cout << "property_get " << property << "=" << result << " on thread " << m_master_of_slave << std::endl;
    auto xpath_node = m_properties->select_node(query.c_str());
    auto node = xpath_node.node();
    std::string value = node.child_value();
    if (not value.empty())
      results[property] = value;
  }
  return results;
}

std::vector<std::string> Project::property_names() const {
  //  std::cout << "property_names "<<" about to call check_property_file_locked "<<m_master_of_slave<<std::endl;
  check_property_file();
  std::vector<std::string> result;
  for (const auto& node : m_properties->select_nodes(((std::string){"/plist/dict/key"}).c_str()))
    result.push_back(node.node().child_value());
  return result;
}

void Project::recent_edit(const std::string& add, const std::string& remove) {
  auto project_suffix =
      add.empty() ? fs::path(remove).extension().string().substr(1) : fs::path(add).extension().string().substr(1);
  auto recent_projects_file = expand_path(std::string{"~/.sjef/"} + project_suffix + "/projects");
  bool changed = false;
  {
    Locker locker{fs::path{recent_projects_file}.parent_path()};
    auto lock = locker.bolt();
    if (!fs::exists(recent_projects_file)) {
      fs::create_directories(fs::path(recent_projects_file).parent_path());
      std::ofstream junk(recent_projects_file);
    }
    {

      std::ifstream in(recent_projects_file);
      std::ofstream out(recent_projects_file + "-");
      size_t lines = 0;
      if (!add.empty()) {
        out << add << std::endl;
        changed = true;
        ++lines;
      }
      std::string line;
      while (getline(in, line) && lines < recentMax) {
        if (line != remove && line != add && fs::exists(line)) {
          ++lines;
          out << line << std::endl;
        } else
          changed = true;
      }
      changed = changed or lines >= recentMax;
    }
    if (changed) {
      fs::remove(recent_projects_file);
      fs::rename(recent_projects_file + "-", recent_projects_file);
    } else
      fs::remove(recent_projects_file + "-");
  }
}

std::string Project::filename(std::string suffix, const std::string& name, int run) const {
  fs::path result{m_filename};
  if (run > -1)
    result = run_directory(run);
  std::string basename = result.stem().string();
  if (m_suffixes.count(suffix) > 0)
    suffix = m_suffixes.at(suffix);
  if (suffix != "" and name == "")
    result /= basename + "." + suffix;
  else if (suffix != "" and name != "")
    result /= name + "." + suffix;
  else if (name != "")
    result /= name;
  return result.string();
}
std::string Project::name() const { return fs::path(m_filename).stem().string(); }

inline std::string slurp(const std::string& path) {
  std::ostringstream buf;
  std::ifstream input(path.c_str());
  buf << input.rdbuf();
  return buf.str();
}

std::string Project::run_directory(int run) const {
  if (run < 0)
    return filename();
  auto sequence = run_verify(run);
  if (sequence < 1)
    //    throw std::runtime_error("Invalid run directory");
    return filename(); // covers the case of old projects without run directories
  auto dir = fs::path{filename()} / "run" / (std::to_string(sequence) + "." + m_project_suffix);
  if (not fs::is_directory(dir))
    throw std::runtime_error("Cannot find directory " + dir.string());
  return dir.string();
}
int Project::run_directory_new() {
  //  auto lock = std::make_unique<Lock>(m_filename);
  //  std::cout << "enter run_directory_new() " << m_master_of_slave << std::endl;
  //  std::cout << "run_directories: " << property_get("run_directories") << std::endl;
  //  if (system((std::string{"ls -ltraR "} + filename()).c_str())) {
  //  }
  //  if (system((std::string{"cat "} + propertyFile()).c_str())) {
  //  }
  auto dirlist = run_list();
  auto sequence = run_directory_next();
  dirlist.insert(sequence);
  std::stringstream ss;
  for (const auto& dir : dirlist)
    ss << dir << " ";
  property_set("run_directories", ss.str());
  auto rundir = fs::path{filename()} / "run";
  auto dir = rundir / (std::to_string(sequence) + "." + m_project_suffix);
  //  std::cout <<"run_directory_new() makes "<<dir<<std::endl;
  if (not fs::exists(rundir) and not fs::create_directories(rundir)) {
    throw std::runtime_error("Cannot create directory " + rundir.string());
  }
  //  std::cerr << "deleted jobnumber property on starting new run directory "<<property_get("jobnumber")<<std::endl;
  property_delete("jobnumber");
  property_delete("_private_job_submission_time");
  set_current_run(0);
  //  fs::directory_iterator end;
  //  for (fs::directory_iterator iter(filename("")); iter != end; iter++) {
  //    auto file = iter->path().filename().string();
  //    bool include = true;
  //    for (const auto& exclude : m_run_directory_ignore)
  //      include = include and not std::regex_search(file, std::regex{exclude});
  //    if (include and fs::is_regular(iter->path())) {
  //      std::cout << "copy " << file << std::endl;
  //      fs::copy(filename("", file), filename("", file, sequence));
  //    }
  //  }
  //  Project newproj{dir.string()};
  //  for (const auto& key : newproj.property_names()) {
  //    auto value = newproj.property_get(key);
  //    boost::replace_first(value, name() + ".", newproj.name() + ".");
  //    newproj.property_set(key, value);
  //  }
  //  if (fs::exists(filename("inp"))) {
  //  std::cout << "copy from "<<filename("inp") << " to " <<newproj.filename("inp")<<std::endl;
  //    fs::copy(filename("inp"), newproj.filename("inp"));
  //  }
  //  newproj.property_delete("jobnumber");
  //  newproj.property_delete("run_directories");
  //  newproj.property_delete("private_project_sjef_project_completed_job");
  //  std::cout << "run_directory_new() before copy" << std::endl;
  //  std::cout << "run_directories: " << property_get("run_directories") << std::endl;
  //  if (system((std::string{"ls -ltraR "} + filename()).c_str())) {
  //  }
  //  if (system((std::string{"cat "} + propertyFile()).c_str())) {
  //  }
  //  lock.reset(nullptr);
  copy(dir.string(), false, false, true);
  //  std::cout << "exit  run_directory_new() " << m_master_of_slave << std::endl;
  return sequence;
}

void Project::run_delete(int run) {
  run = run_verify(run);
  if (run == 0)
    return;
  fs::remove_all(run_directory(run));
  auto dirlist = run_list();
  dirlist.erase(run);
  std::stringstream ss;
  for (const auto& dir : dirlist)
    ss << dir << " ";
  property_set("run_directories", ss.str());
}

int Project::run_verify(int run) const {
  auto runlist = run_list();
  if (run > 0)
    return (runlist.count(run) > 0) ? run : 0;
  const auto currentRun = current_run();
  if (currentRun > 0)
    return currentRun;
  else if (runlist.empty())
    return 0;
  else
    return *(runlist.begin());
}

Project::run_list_t Project::run_list() const {
  constexpr bool old_algorithm = false;
  run_list_t rundirs;
  if (old_algorithm) {
    auto ss = std::stringstream(property_get("run_directories"));
    //    std::cout << "Project::run_list() gets run_directories=" << property_get("run_directories") << std::endl;
    int value;
    while (ss >> value && !ss.eof())
      if (fs::exists(fs::path{m_filename} / "run" / (std::to_string(value) + "." + m_project_suffix)))
        rundirs.insert(value);
  } else {
    const std::filesystem::path& rundir = fs::path{m_filename} / "run";
    if (fs::exists(rundir))
      for (auto& f : fs::directory_iterator(rundir))
        if (f.path().extension() == std::string{"."} + m_project_suffix)
          rundirs.insert(std::stoi(f.path().stem()));
  }
  return rundirs;
}

int Project::run_directory_next() const {
  auto dirlist = run_list();
  return dirlist.empty() ? 1 : *(dirlist.begin()) + 1;
  ;
}

int Project::recent_find(const std::string& suffix, const std::string& filename) {
  auto recent_projects_directory = expand_path(std::string{"~/.sjef/"} + suffix);
  fs::create_directories(recent_projects_directory);
  std::ifstream in(expand_path(recent_projects_directory + "/projects"));
  std::string line;
  for (int position = 1; std::getline(in, line); ++position) {
    if (fs::exists(line)) {
      if (line == filename)
        return position;
    } else
      --position;
  }
  return 0;
}

int Project::recent_find(const std::string& filename) const { return recent_find(m_project_suffix, filename); }

std::string Project::recent(const std::string& suffix, int number) {
  auto recent_projects_directory = expand_path(std::string{"~/.sjef/"} + suffix);
  fs::create_directories(recent_projects_directory);
  std::ifstream in(expand_path(recent_projects_directory + "/projects"));
  std::string line;
  for (int position = 0; in >> line;) {
    if (fs::exists(line))
      ++position;
    if (position == number)
      return line;
  }
  return "";
}

std::string Project::recent(int number) const { return recent(m_project_suffix, number); }

///> @private
struct plist_writer : pugi::xml_writer {
  std::string file;
  virtual void write(const void* data, size_t size) {
    std::ofstream s(file);
    s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<!DOCTYPE plist SYSTEM '\"-//Apple//DTD PLIST 1.0//EN\" "
         "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"'>\n"
      << std::string{static_cast<const char*>(data), size};
  }
};

constexpr static bool use_writer = false;
void Project::load_property_file_locked() const {
  //  std::cout << "load_property_file() from " << this << std::endl;
  //  std::cout << slurp(propertyFile())<<std::endl;
  auto result = m_properties->load_file(propertyFile().c_str());
  if (!result)
    throw std::runtime_error("error in loading " + propertyFile() + "\n" + result.description() + "\n" +
                             slurp(propertyFile()));
  m_property_file_modification_time = fs::last_write_time(propertyFile());
}

bool Project::properties_last_written_by_me(bool removeFile, bool already_locked) const {
  auto path = fs::path{m_filename} / fs::path{writing_object_file};
  //  std::cout << "enter properties_last_written_by_me on thread " << m_master_of_slave << std::endl;
  //  std::unique_ptr<Interprocess_lock> lock_;
  //  if (not already_locked)
  //    lock_.reset(new Interprocess_lock(path.string()));
  //  FileLock lock(path.string(), false);
  auto lock = m_locker->bolt();
  std::ifstream i{path.string(), std::ios_base::in};
  if (not i.is_open())
    return false;
  std::hash<const Project*> hasher;
  auto me = hasher(this);
  decltype(me) writer;
  i >> writer;
  if (removeFile and writer == me)
    fs::remove(path);
  //  std::cout << "properties_last_written_by_me writer=" << writer << " me=" << me << " : " << (writer == me)
  //            << std::endl;
  return writer == me;
}
void Project::check_property_file() const {
  //  Lock fileLock(propertyFile()+".lock");
  auto lock = m_locker->bolt();
  //  std::cerr << "check_property_file acquired lock " << this << " : " << m_master_of_slave << std::endl;
  //  std::cout << "" << std::ifstream(fs::path{m_filename} / "Info.plist").rdbuf() << "" << std::endl;

  check_property_file_locked();
  //  std::cerr << "check_property_file releasing lock " << this << " : " << m_master_of_slave << std::endl;
  //  std::cout << "" << std::ifstream(fs::path{m_filename} / "Info.plist").rdbuf() << "" << std::endl;
}
void Project::check_property_file_locked() const {
  //  std::cout << "check_property_file_locked(), master=" << m_master_of_slave << std::endl;
  auto lastwrite = fs::last_write_time(propertyFile());
  //    std::cout << "lastwrite="<lastwrite.time_since_epoch().count()<<std::endl;
  //    std::cout <<
  //    "m_property_file_modification_time="<<m_property_file_modification_time.time_since_epoch().count()<<std::endl;
  if (m_property_file_modification_time == lastwrite) { // tie-breaker
    //    std::cout << "tie breaker " << properties_last_written_by_me(false, true) << std::endl;
    if (not properties_last_written_by_me(false, true))
      m_property_file_modification_time -=
          std::chrono::milliseconds(1); // to mark this object's cached properties as out of date
  }
  if (m_property_file_modification_time < lastwrite) {
    //    std::cout << "my modification time < lastwrite" << std::endl;
    { load_property_file_locked(); }
    m_property_file_modification_time = lastwrite;
  }
}

void Project::save_property_file() const {
  auto lock = m_locker->bolt();
  save_property_file_locked();
}
void Project::save_property_file_locked() const {
  struct plist_writer writer;
  writer.file = propertyFile();
  if (not fs::exists(propertyFile())) {
    fs::create_directories(m_filename);
    { std::ofstream x(propertyFile()); }
  }
  //    std::cerr << "1 exists(propertyFile()) ? " << fs::exists(propertyFile()) << std::endl;
  if (use_writer)
    m_properties->save(writer, "\t", pugi::format_no_declaration);
  else
    m_properties->save_file(propertyFile().c_str());
  //  std::cerr << "2 exists(propertyFile()) ? " << fs::exists(propertyFile()) << std::endl;
  //  std::cout << "save_property_file master=" << m_master_of_slave << std::endl;
  //  if (system((std::string{"cat "} + propertyFile()).c_str()));
  //  std::cout << "end save_property_file master=" << m_master_of_slave << std::endl;
  //  m_property_file_modification_time = fs::last_write_time(propertyFile());
  auto path = (fs::path{m_filename} / fs::path{writing_object_file});
  //  FileLock lock(path.string(), true, false);
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
    const char charset[] = "0123456789"
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
  //  system((std::string{"cat "}+filename("plist", "Info")).c_str());
  //  std::cout << "project_hash @"<<p<<"@"<<std::endl;
  size_t result;
  if (p.empty()) {
    //    FileLock pl(propertyFile()+".hashlock", true, true);
    result = std::hash<std::string>{}(random_string(32));
    this->property_set("project_hash", std::to_string(result));
    //    std::cout << "set project_hash "<<std::to_string(result)<<std::endl;
  } else {
    std::istringstream iss(p);
    iss >> result;
  }
  //  std::cout << "result " << result << std::endl;
  return result;
}

size_t sjef::Project::input_hash() const {
  constexpr bool debug = false;
  if (debug) {
    std::cerr << "input_hash" << filename("inp") << std::endl;
    std::ifstream ss(filename("inp"));
    std::cerr << "unmodified file\n"
              << std::string((std::istreambuf_iterator<char>(ss)), std::istreambuf_iterator<char>()) << std::endl;
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
  text = std::regex_replace(text, std::regex{R"--(^\~)--"}, environment("USERPROFILE"));
  text = std::regex_replace(text, std::regex{R"--(^\$\{HOME\})--"}, environment("USERPROFILE"));
  text = std::regex_replace(text, std::regex{R"--(^\$HOME/)--"}, environment("USERPROFILE") + "/");
  text = std::regex_replace(text, std::regex{R"--(^\$\{TMPDIR\})--"}, environment("TEMP"));
  text = std::regex_replace(text, std::regex{R"--(^\$TMPDIR/)--"}, environment("TEMP") + "/");
#else
  text = std::regex_replace(text, std::regex{R"--(^\~)--"}, environment("HOME"));
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
  if (text.back() == fs::path::preferred_separator)
    text.pop_back();
  // add suffix
  //  std::cerr << "before suffix add: " << text << std::endl;
  if (fs::path{text}.extension().string() != std::string{"."} + suffix and !suffix.empty())
    text += "." + suffix;
  //  std::cerr << "after suffix add: " << text << std::endl;
  return text;
}

std::string xmlRepair(const std::string& source, const std::map<std::string, std::string>& injections) {
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
      if (!nodes.empty())
        nodes.pop_back();
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
  if (std::string{s, source.end()}.find('<') !=
      std::string::npos) /* fix broken tags due to e.g. full disk such as: <parallel proces */
    result.erase(source.find_last_of('<'));
  if (commentPending)
    result += "-->";
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

std::mutex s_remote_server_mutex;
std::string sjef::Project::remote_server_run(const std::string& command, int verbosity, bool wait) const {
  const std::lock_guard lock(s_remote_server_mutex);
  if (verbosity > 0)
    std::cerr << command << std::endl;
  const std::string terminator{"@@@!!EOF"};
  if (not wait) {
    //    std::cerr << "remote_server_run wait=false host=" << m_remote_server->host << ", command=" << command <<
    //    std::endl;
    m_remote_server->in << command << " >/dev/null 2>/dev/null &" << std::endl;
    return "";
  }
  //  std::cerr << "remote_server_run m_remote_server->process.running" << m_remote_server->process.running() <<
  //  std::endl;
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
  auto rc = line.empty() ? -1 : std::stoi(line.substr(terminator.size() + 1));
  if (verbosity > 1)
    std::cerr << "rc=" << rc << std::endl;
  if (rc != 0)
    throw std::runtime_error("Host " + m_remote_server->host + "; failed remote command: " + command +
                             "\nStandard output:\n" + m_remote_server->last_out + "\nStandard error:\n" +
                             m_remote_server->last_err);
  return m_remote_server->last_out;
}
void sjef::Project::ensure_remote_server() const {
  if (m_master_instance != nullptr) {
    m_remote_server = m_master_instance->m_remote_server;
    //    std::cerr << "ensure_remote_server for slave resets to " << m_remote_server->host << std::endl;
    return;
  }
  const std::lock_guard lock(m_remote_server_mutex);
  if (m_remote_server == nullptr)
    m_remote_server.reset(new remote_server());
  //  std::cerr << "ensure_remote_server called on thread " << std::this_thread::get_id() << ", master_of_slave="
  //            << m_master_of_slave << ", host=" << m_remote_server->host << ", m_remote_server->process.running "
  //            << m_remote_server->process.running() << std::endl;
  //  std::cerr << "m_remote_server->host "<<m_remote_server->host << std::endl;
  auto backend = property_get("backend");
  //  std::cerr << "master=" << m_master_of_slave << ", property backend = " << backend << std::endl;
  if (backend.empty())
    backend = sjef::Backend::default_name;
  auto oldhost = m_remote_server->host;
  auto newhost = this->backend_get(backend, "host");
  if (newhost == oldhost)
    return;
  if (oldhost != "localhost" and m_remote_server->process.running()) {
    //    std::cerr << "ensure_remote_server() remote server process to be killed: " << m_remote_server->process.id()
    //              << ", master=" << m_master_of_slave << std::endl;
    m_remote_server->process.terminate();
  }
  m_remote_server.reset(new remote_server());
  m_remote_server->host = newhost;
  if (newhost == "localhost") {
    //    std::cerr << "ensure_remote_server() returning for localhost master=" << m_master_of_slave << std::endl;
    return;
  }
  //  std::cerr << "Start remote_server "
  //            << " ssh command = " << bp::search_path("ssh")
  //            << " host = " << m_remote_server->host
  //            << std::endl;
  m_remote_server->process =
      bp::child(bp::search_path("ssh"), m_remote_server->host, "/bin/sh",
                bp::std_in<m_remote_server->in, bp::std_err> m_remote_server->err, bp::std_out > m_remote_server->out);
  // TODO error checking
  //  std::cerr << "ensure_remote_server has started server on " << m_remote_server->host << std::endl;
  //  std::cerr << "started remote_server " << std::endl;
  //  std::cerr << "ensure_remote_server() remote server process created : " << m_remote_server->process.id() << ",
  //  master="
  //            << m_master_of_slave << std::endl;
  //  std::cerr << "ensure_remote_server finishing on thread " << std::this_thread::get_id() << ", master_of_slave="
  //            << m_master_of_slave << std::endl;
}

void sjef::Project::shutdown_backend_watcher() {
  if (not m_master_of_slave)
    return;
  //  std::cerr << "enter shutdown_backend_watcher for project at " << this << " joinable=" <<
  //  m_backend_watcher.joinable()
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

void sjef::Project::change_backend(std::string backend, bool force) {
  if (backend.empty())
    backend = sjef::Backend::default_name;
  bool unchanged = property_get("backend") == backend && m_backend == backend;
  if (not force and unchanged
      //  and m_backend_watcher.joinable()
  )
    return;
  //  std::cerr << "ENTER change_backend() joinable=" << m_backend_watcher.joinable() << ", master=" <<
  //  m_master_of_slave
  //            << std::endl;
  //  std::cerr << "change_backend to " << backend << " for project " << name() << " at address " << this << std::endl;
  //  std::cerr << "current backend " << property_get("backend") << " : " << m_backend << ", unchanged=" << unchanged
  //            << std::endl;
  if (not unchanged and not m_backend.empty())
    property_delete("jobnumber");
  m_backend = backend;
  if (not unchanged) {
    throw_if_backend_invalid(backend);
    if (m_master_of_slave)
      shutdown_backend_watcher();
    property_set("backend", backend);
    cached_status(unevaluated);
    ensure_remote_server();
    //    std::cerr << "after ensure_remote_server backend " << m_master_of_slave << std::endl;
    if (m_master_of_slave) {
      if (this->m_backends.at(backend).host != "localhost") {
        //        std::cout << "change_backend remote_server_run " << std::endl;
        try {
          remote_server_run(
              std::string{"mkdir -p "} + (fs::path{this->m_backends.at(backend).cache} / m_filename).string(), 0);
          //          std::cout << "change_backend remote_server_run has returned " << std::endl;
        } catch (...) {
        }
      }
      m_unmovables.shutdown_flag.clear();
      m_backend_watcher = std::thread(backend_watcher, std::ref(*this), backend, 100, 1000, 10);
    }
  }
}

void sjef::Project::backend_watcher(sjef::Project& project_, const std::string& backend, int min_wait_milliseconds,
                                    int max_wait_milliseconds, int poll_milliseconds) noexcept {
  //  std::cerr << "Project::backend_watcher starting for " << project_.m_filename << " on thread "
  //            << std::this_thread::get_id() << std::endl;
  project_.m_backend_watcher_instance.reset(new sjef::Project(project_.m_filename, true, "", {{}}, &project_));
  auto& project = *project_.m_backend_watcher_instance.get();
  if (max_wait_milliseconds <= 0)
    max_wait_milliseconds = min_wait_milliseconds;
  constexpr auto radix = 2;
  backend_watcher_wait_milliseconds = std::max(min_wait_milliseconds, poll_milliseconds);
  try {
    //    std::cerr << "sjef::Project::backend_watcher() START for project " << project.name() << " at address " <<
    //    &project
    //              << ", backend " << backend
    //              << ", " << project.property_get("backend")
    //              << std::endl;
    //    std::cerr << "ensure_remote_server call from server"<<std::endl;
    project.ensure_remote_server();
    //    std::cerr << "ensure_remote_server returns to server"<<std::endl;
    auto& shutdown_flag = const_cast<sjef::Project*>(project.m_master_instance)->m_unmovables.shutdown_flag;
    for (auto iter = 0; true; ++iter) {
      //      std::cerr << "iter " << iter ;
      //      std::cerr << "; going to sleep for " << backend_watcher_wait_milliseconds << "ms" << std::endl;
      //      std::this_thread::sleep_for(std::chrono::milliseconds(backend_watcher_wait_milliseconds));
      //      std::cout << "repeats: "<<backend_watcher_wait_milliseconds / poll_milliseconds<<std::endl;
      for (int repeat = 0; repeat < backend_watcher_wait_milliseconds / poll_milliseconds; ++repeat) {
        if (shutdown_flag.test_and_set())
          goto finished;
        shutdown_flag.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_milliseconds));
      }
      backend_watcher_wait_milliseconds =
          std::max(std::min(backend_watcher_wait_milliseconds * radix, max_wait_milliseconds),
                   min_wait_milliseconds <= 0 ? 1 : min_wait_milliseconds);
      //      std::cerr << "... watcher for project " << &project << " waking up" << std::endl;
      try {
        project.synchronize(0);
      } catch (const std::exception& ex) {
        std::cerr << "sjef::Project::backend_watcher() synchronize() has thrown " << ex.what() << std::endl;
        project.cached_status(unknown);
      }
      //      std::cerr << "... watcher for project "<<&project<<" back from synchronize"<<std::endl;
      try {
        project.cached_status(project.status(0, false));
      } catch (const std::exception& ex) {
        std::cerr << "sjef::Project::backend_watcher() status() has thrown " << ex.what() << std::endl;
        project.cached_status(unknown);
      }
      //      std::cerr << "... watcher for project "<<&project<<" back from status"<<std::endl;
      //      std::cerr << "sjef::Project::backend_watcher() status " << project.cached_status() << std::endl;
    }
  finished:;
  } catch (...) {
  }
  //  std::cerr << "Project::backend_watcher stopping for " << project_.m_filename << " on thread "
  //            << std::this_thread::get_id() << std::endl;
}

bool sjef::Project::check_backend(const std::string& name) const {
  auto be = m_backends.at(name);
  if (be.host.empty())
    return false;
  if (be.run_command.empty())
    return false;
  if (be.run_jobnumber.empty())
    return false;
  if (be.status_command.empty())
    return false;
  if (be.status_running.empty())
    return false;
  if (be.status_waiting.empty())
    return false;
  return true;
}

bool sjef::Project::check_all_backends() const {
  bool result = true;
  for (const auto& backend : m_backends)
    result = result and check_backend(backend.first);
  return result;
}

bool check_backends(const std::string& suffix) {
  for (const auto& config_dir : std::vector<std::string>{"/usr/local/etc/sjef", "~/.sjef"}) {
    const auto config_file = expand_path(config_dir + "/" + suffix + "/backends.xml");
    if (fs::exists(config_file)) {
      std::unique_ptr<pugi_xml_document> backend_doc;
      pugi::xml_document doc;
      pugi::xml_parse_result result = doc.load_file(config_file.c_str());
      if (result.status != pugi::status_ok) {
        return false;
      }
    }
  }
  return true;
}
void Project::take_run_files(int run, const std::string& fromname, const std::string& toname) const {
  auto toname_ = toname.empty() ? fromname : toname;
  fs::copy(filename("", fromname, run), fs::path{m_filename} / toname_);
}

void Project::set_current_run(unsigned int run) { property_set("current_run", std::to_string(run)); }

unsigned int Project::current_run() const {
  auto s = property_get("current_run");
  return s.empty() ? 0 : std::stoi(s);
}

void Project::add_backend(const std::string& name, const std::map<std::string, std::string>& fields) {
  m_backends[name] = Backend(name);
  if (fields.count("host") > 0)
    m_backends[name].host = fields.at("host");
  if (fields.count("cache") > 0)
    m_backends[name].cache = fields.at("cache");
  if (fields.count("run_command") > 0)
    m_backends[name].run_command = fields.at("run_command");
  if (fields.count("run_jobnumber") > 0)
    m_backends[name].run_jobnumber = fields.at("run_jobnumber");
  if (fields.count("status_command") > 0)
    m_backends[name].status_command = fields.at("status_command");
  if (fields.count("status_running") > 0)
    m_backends[name].status_running = fields.at("status_running");
  if (fields.count("status_waiting") > 0)
    m_backends[name].status_waiting = fields.at("status_waiting");
  if (fields.count("kill_command") > 0)
    m_backends[name].kill_command = fields.at("kill_command");
}

const std::string version() noexcept { return SJEF_VERSION; }

pugi::xpath_node_set Project::select_nodes(const std::string& xpath_query, int run) const {
  auto xml = pugi::xml_document();
  xml.load_string(this->xml(run).c_str());
  //  xml.load_file(filename("xml", "", run).c_str());
  return xml.select_nodes(xpath_query.c_str());
}

std::vector<std::string> Project::xpath_search(const std::string& xpath_query, const std::string& attribute,
                                               int run) const {
  auto node_set = select_nodes(xpath_query, run);
  std::vector<std::string> result;
  for (const auto& node : node_set) {
    if (attribute.empty())
      result.push_back(node.node().child_value());
    else
      result.push_back(node.node().attribute(attribute.c_str()).value());
  }
  return result;
}

} // namespace sjef
