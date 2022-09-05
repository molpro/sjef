#include "sjef.h"
#include "Locker.h"
#include "sjef-backend.h"
#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
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
#include "util/Job.h"
#include "util/Shell.h"
#include "util/util.h"
#include <random>

namespace fs = std::filesystem;

///> @private
struct sjef::pugi_xml_document : public pugi::xml_document {};

///> @private
const std::string sjef::Project::s_propertyFile = "Info.plist";
const std::string writing_object_file = ".Info.plist.writing_object";

///> @private
class internal_error : public sjef::runtime_error {
public:
  using runtime_error::runtime_error;
};

///> @private
inline bool localhost(const std::string_view& host) {
  return (host.empty() || host == "localhost" || host == "127.0.0.1");
}

///> @private
bool copyDir(fs::path const& source, fs::path const& destination, bool delete_source = false, bool recursive = true) {
  using sjef::runtime_error;
  if (!fs::exists(source) || !fs::is_directory(source))
    throw runtime_error("Source directory " + source.string() + " does not exist or is not a directory.");
  if (fs::exists(destination))
    throw runtime_error("Destination directory " + destination.string() + " already exists.");
  if (!fs::create_directory(destination))
    throw runtime_error("Unable to create destination directory " + destination.string());
  sjef::Locker destination_locker(destination);
  auto destination_lock = destination_locker.bolt();
  for (fs::directory_iterator file(source); file != fs::directory_iterator(); ++file) {
    fs::path current(file->path());
    if (fs::is_directory(current)) {
      if (recursive && !copyDir(current, destination / current.filename(), delete_source))
        return false;
    } else {
      if (current.filename() != ".lock" && current.extension() != ".lock")
        fs::copy_file(current, destination / current.filename());
    }
  }
  return true;
}

namespace sjef {

inline std::string getattribute(pugi::xpath_node node, const std::string& name) {
  return node.node().attribute(name.c_str()).value();
}

std::mutex s_make_locker_mutex;
std::map<fs::path, std::shared_ptr<Locker>> lockers;
inline std::shared_ptr<Locker> make_locker(const fs::path& filename) {
  std::lock_guard lock(s_make_locker_mutex);
  auto name = fs::absolute(filename);
  if (lockers.count(name) == 0) {
    lockers[name] = std::make_shared<Locker>(fs::path{name} / ".lock");
  }
  return lockers[name];
}
inline void prune_lockers(const fs::path& filename) {
  std::lock_guard lock(s_make_locker_mutex);
  auto name = fs::absolute(filename);
  if (lockers.count(name) > 0 && lockers[name].use_count() == 0)
    lockers.erase(name);
}
inline fs::path sjef_config_directory() {
  return fs::path(expand_path(getenv("SJEF_CONFIG") == nullptr ? "~/.sjef" : getenv("SJEF_CONFIG")));
}

const std::vector<std::string> Project::suffix_keys{"inp", "out", "xml"};
Project::Project(const std::filesystem::path& filename, bool construct, const std::string& default_suffix,
                 const mapstringstring_t& suffixes)
    : m_project_suffix(get_project_suffix(filename, default_suffix)),
      m_filename(expand_path(filename, m_project_suffix)), m_properties(std::make_unique<pugi_xml_document>()),
      m_suffixes(suffixes), m_backend_doc(std::make_unique<pugi_xml_document>()), m_locker(make_locker(m_filename)),
      m_run_directory_ignore({writing_object_file, name() + "_[^./\\\\]+\\..+"}) {
  {
    auto lock = m_locker->bolt();
    if (fs::exists(propertyFile())) {
      load_property_file_locked();
    } else {
      if (!fs::exists(m_filename))
        fs::create_directories(m_filename);
      std::ofstream(propertyFile().string()) << "<?xml version=\"1.0\"?>\n"
                                                "<plist> <dict/> </plist>"
                                             << std::endl;
    }
    auto recent_projects_directory = expand_path(sjef_config_directory() / m_project_suffix);
    fs::create_directories(recent_projects_directory);
    for (const auto& key : suffix_keys)
      if (m_suffixes.count(key) < 1)
        m_suffixes[key] = key;
    if (suffixes.count("inp") > 0)
      m_reserved_files.push_back(this->filename("inp"));
    if (!fs::exists(m_filename))
      throw runtime_error("project does not exist and could not be created: " + m_filename.string());
    if (!fs::is_directory(m_filename))
      throw runtime_error("project should be a directory: " + m_filename.string());

    if (!construct)
      return;
    if (const auto pf = propertyFile(); !fs::exists(pf)) {
      save_property_file();
      m_property_file_modification_time = fs::last_write_time(propertyFile());
      property_set("_status", "4");
    } else {
      if (!fs::exists(pf))
        throw runtime_error("Unexpected absence of property file " + pf.string());
      m_property_file_modification_time = fs::last_write_time(propertyFile());
      check_property_file_locked();
    }
    custom_initialisation();

    auto nimport = property_get("IMPORTED").empty() ? 0 : std::stoi(property_get("IMPORTED"));
    for (int i = 0; i < nimport; i++) {
      m_reserved_files.push_back(property_get(std::string{"IMPORT"} + std::to_string(i)));
    }
    if (fs::path{m_filename}.parent_path().filename().string() != "run" &&
        !fs::exists(fs::path{m_filename}.parent_path().parent_path() /
                    "Info.plist")) // If this is a run-directory project, do not add to recent list
      recent_edit(m_filename);

    m_backends.try_emplace(Backend::dummy_name, Backend::local());
    if (!check_backends(m_project_suffix)) {
      auto config_file = expand_path(sjef_config_directory() / m_project_suffix / "backends.xml");
      m_warn.error() << "contents of " << config_file << ":" << std::endl;
      m_warn.error() << std::ifstream(config_file).rdbuf() << std::endl;
      throw runtime_error("sjef backend files are invalid");
    }
    for (const auto& config_dir : std::vector<std::filesystem::path>{"/usr/local/etc/sjef", sjef_config_directory()}) {
      const auto config_file = expand_path(config_dir / m_project_suffix / "backends.xml");
      if (fs::exists(config_file)) {
        m_backend_doc->load_file(config_file.c_str());
        auto backends = m_backend_doc->select_nodes("/backends/backend");
        for (const auto& be : backends) {
          auto kName = getattribute(be, "name");
          if (auto kHost = getattribute(be, "host"); localhost(kHost))
            m_backends.try_emplace(kName, Backend::local(), kName);
          else
            m_backends.try_emplace(kName, Backend::Linux(), kName);
          if (const auto kVal = getattribute(be, "template"); kVal != "") {
            m_backends[kName].host = m_backends[kVal].host;
            m_backends[kName].cache = m_backends[kVal].cache;
            m_backends[kName].run_command = m_backends[kVal].run_command;
            m_backends[kName].run_jobnumber = m_backends[kVal].run_jobnumber;
            m_backends[kName].status_command = m_backends[kVal].status_command;
            m_backends[kName].status_running = m_backends[kVal].status_running;
            m_backends[kName].status_waiting = m_backends[kVal].status_waiting;
            m_backends[kName].kill_command = m_backends[kVal].kill_command;
          }
          if (const auto kVal = getattribute(be, "host"); kVal != "")
            m_backends[kName].host = kVal;
          if (const auto kVal = getattribute(be, "cache"); kVal != "")
            m_backends[kName].cache = kVal;
          if (const auto kVal = getattribute(be, "run_command"); kVal != "")
            m_backends[kName].run_command = kVal;
          if (const auto kVal = getattribute(be, "run_jobnumber"); kVal != "")
            m_backends[kName].run_jobnumber = kVal;
          if (const auto kVal = getattribute(be, "status_command"); kVal != "")
            m_backends[kName].status_command = kVal;
          if (const auto kVal = getattribute(be, "status_running"); kVal != "")
            m_backends[kName].status_running = kVal;
          if (const auto kVal = getattribute(be, "status_waiting"); kVal != "")
            m_backends[kName].status_waiting = kVal;
          if (const auto kVal = getattribute(be, "kill_command"); kVal != "")
            m_backends[kName].kill_command = kVal;
          if (!check_backend(kName))
            throw runtime_error(std::string{"sjef backend "} + kName + " is invalid");
        }
      }
    }
    if (m_backends.count(sjef::Backend::default_name) == 0) {
      m_backends[sjef::Backend::default_name] = default_backend();
      const auto config_file = expand_path(sjef_config_directory() / m_project_suffix / "backends.xml");
      auto config_file_ = config_file;
      config_file_ += "-";
      if (!fs::exists(config_file))
        std::ofstream(config_file) << "<?xml version=\"1.0\"?>\n<backends>\n</backends>" << std::endl;
      {
        Locker locker{fs::path{config_file}.parent_path()};
        auto locki = locker.bolt();
        fs::copy_file(config_file, config_file_);
        auto in = std::ifstream(config_file_.string());
        auto out = std::ofstream(config_file);
        std::string line;
        auto be_defaults = Backend(Backend::local());
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
      fs::remove(config_file_);
    }
  }
  if (!name().empty() && name().front() != '.') {
    auto be = property_get("backend");
    if (m_backends.count(be) == 0)
      be = sjef::Backend::default_name;
    change_backend(be, true);
    // we may be loading an old project with an active job when it was last closed, so need to poll its status
    {
      auto initial_status = static_cast<sjef::status>(std::stoi("0" + property_get("_status")));
      //      std::cout << "initial_status " << initial_status << std::endl;
      if (initial_status == running or initial_status == waiting) {
        auto new_status = util::Job(*this).get_status();
        if (new_status == unknown) {
          //          std::cout << "setting status from " << initial_status <<" to completed"<< std::endl;
          property_set("_status", std::to_string(static_cast<int>(completed)));
        }
      }
    }
  }
}

Project::~Project() {}

std::string Project::get_project_suffix(const std::filesystem::path& filename,
                                        const std::string& default_suffix) const {
  auto suffix = expand_path(filename, default_suffix).extension().string();
  if (suffix.empty())
    throw runtime_error("Cannot deduce project suffix for \"" + filename.string() + "\" with default suffix \"" +
                        default_suffix + "\"");
  return suffix.substr(1);
}

bool Project::import_file(const std::filesystem::path& file, bool overwrite) {
  auto to = m_filename / file.filename();
  for (const auto& key : suffix_keys)
    if (fs::path{file}.extension() == m_suffixes[key])
      to = fs::path{m_filename} / fs::path{name()} / m_suffixes[key];
  // TODO: implement generation of .inp from .xml
  std::error_code ec;
  if (overwrite && exists(to))
    remove(to);
  fs::copy_file(file, to, ec);
  m_reserved_files.emplace_back(to.string());
  auto nimport = property_get("IMPORTED").empty() ? 0 : std::stoi(property_get("IMPORTED"));
  std::string key = "IMPORT" + std::to_string(nimport);
  property_set(key, to.filename().string());
  property_set("IMPORTED", std::to_string(nimport + 1));
  if (ec)
    throw runtime_error(ec.message());
  return true;
}

void Project::throw_if_backend_invalid(std::string backend) const {
  if (backend.empty())
    backend = property_get("backend");
  if (backend.empty())
    throw runtime_error("No backend specified");
  if (m_backends.count(backend) > 0)
    return;
  const auto path = expand_path(sjef_config_directory() / m_project_suffix / "backends.xml");
  m_warn.error() << "Contents of " << path << ":\n" << std::ifstream(path).rdbuf() << std::endl;
  throw runtime_error("Backend " + backend + " is not registered");
}

bool Project::export_file(const fs::path& file, bool overwrite) {
  throw_if_backend_invalid();
  auto from = fs::path{m_filename};
  from /= file.filename();
  std::error_code ec;
  if (overwrite && exists(fs::path{file}))
    remove(file);
  fs::copy_file(from, file, ec);
  if (ec)
    throw runtime_error(ec.message());
  return true;
}

void Project::force_file_names(const std::string& oldname) {
  m_locker->bolt();
  fs::directory_iterator end_iter;
  for (fs::directory_iterator dir_itr(m_filename); dir_itr != end_iter; ++dir_itr) {
    auto path = dir_itr->path();
    try {
      auto ext = path.extension().string();
      if (path.stem() == oldname && !ext.empty() && m_suffixes.count(ext.substr(1)) > 0) {
        auto newpath = path.parent_path();
        newpath /= name();
        newpath.replace_extension(dir_itr->path().extension());
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
      throw runtime_error(dir_itr->path().string() + " " + ex.what());
    }
  }
}

fs::path Project::propertyFile() const { return (fs::path{m_filename} / fs::path{s_propertyFile}).string(); }

bool Project::move(const std::filesystem::path& destination_filename, bool force) {
  if (auto stat = status(); stat == running || stat == waiting)
    return false;
  auto dest = fs::absolute(expand_path(destination_filename, fs::path{m_filename}.extension().string().substr(1)));
  if (force)
    fs::remove_all(dest);
  auto namesave = name();
  auto filenamesave = m_filename;
  try {
    if (!copyDir(fs::path(m_filename), dest, true))
      throw runtime_error("Failed to copy current project directory");
    m_filename = dest.string();
    force_file_names(namesave);
    recent_edit(m_filename, filenamesave);
    if (!fs::remove_all(filenamesave))
      throw runtime_error("failed to delete current project directory");
    return true;
  } catch (...) {
    m_warn.warn() << "move failed to copy " << m_filename << " : " << dest << std::endl;
  }
  change_backend(property_get("backend"));
  return false;
}

bool Project::copy(const std::filesystem::path& destination_filename, bool force, bool keep_hash, bool slave,
                   int keep_run_directories) {
  auto dest = fs::absolute(expand_path(destination_filename, fs::path{m_filename}.extension().string().substr(1)));
  if (slave)
    keep_run_directories = 0;
  {
    if (force)
      fs::remove_all(dest);
    if (fs::exists(dest))
      throw runtime_error("Copy to " + dest.string() + " cannot be done because the destination already exists");
    auto bolt = m_locker->bolt();
    if (!copyDir(fs::path(m_filename), dest, false, !slave))
      return false;
  }
  Project dp(dest.string());
  dp.force_file_names(name());
  if (!slave)
    recent_edit(dp.m_filename);
  dp.property_delete("jobnumber");
  if (slave)
    dp.property_delete("run_directories");
  dp.clean(true, true, false, keep_run_directories);
  if (!keep_hash)
    dp.property_delete("project_hash");
  return true;
}

void Project::erase(const std::filesystem::path& filename, const std::string& default_suffix) {
  auto filename_ = sjef::expand_path(filename, default_suffix);
  Backend backend;
  {
    auto project = Project(filename_);
    backend = project.backends().at(project.m_backend);
  }
  if (fs::remove_all(filename_))
    recent_edit("", filename_);
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
  std::string output_text;
  std::regex re("[^$]\\{([^}]*)\\}");
  auto callback = [&](std::string m) {
    if (std::regex_match(m, re)) {
      auto first = m.front();
      m.pop_back();
      m.erase(0, 1);
      if (first != '{')
        m[0] = first;
      if (auto bang = m.find_first_of("!"); bang != std::string::npos)
        m = m.substr(0, bang);
      auto percent = m.find_first_of("%");
      if (percent == std::string::npos)
        throw runtime_error("Invalid template: " + templ + "\nMissing % in expression {" + m + "}");
      auto parameter_name = m.substr(percent + 1);
      auto defpos = parameter_name.find_first_of(":");
      std::string def;
      if (defpos != std::string::npos) {
        def = parameter_name.substr(defpos + 1);
        parameter_name.erase(defpos);
      }
      auto value = backend_parameter_get(backend, parameter_name);
      if (value.empty()) {
        if (!def.empty())
          output_text += m.substr(0, percent) + def;
        else
          output_text += m.front();
      } else {
        output_text += m.substr(0, percent) + value;
      }
    } else {
      output_text += m;
    }
  };

  auto templ_ = std::string{" "} + templ;
  std::sregex_token_iterator begin(templ_.begin(), templ_.end(), re, {-1, 0});
  std::sregex_token_iterator end;
  std::for_each(begin, end, callback);
  return output_text.substr(1);
}

mapstringstring_t Project::backend_parameters(const std::string& backend, bool doc) const {
  mapstringstring_t result;

  throw_if_backend_invalid(backend);
  auto templ = std::string{" "} + m_backends.at(backend).run_command;
  std::string output_text;
  std::regex re("[^$]\\{([^}]*)\\}");
  auto callback = [&](std::string m) {
    if (std::regex_match(m, re)) {
      auto first = m.front();
      m.pop_back();
      m.erase(0, 1);
      if (first != '{')
        m[0] = first;
      std::string docu;
      if (auto bang = m.find_first_of("!"); bang != std::string::npos) {
        docu = m.substr(bang + 1);
        m = m.substr(0, bang);
      }
      auto percent = m.find_first_of("%");
      if (percent == std::string::npos)
        throw runtime_error("Invalid template: " + templ + "\nMissing % in expression {" + m + "}");
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

  std::sregex_token_iterator begin(templ.begin(), templ.end(), re, {-1, 0});
  std::sregex_token_iterator end;
  std::for_each(begin, end, callback);

  return result;
}

bool Project::run(int verbosity, bool force, bool wait) {

  using util::splitString;

  if (auto stat = status(); stat == running || stat == waiting)
    return false;

  const auto& backend = m_backends.at(property_get("backend"));
  m_trace(2 - verbosity) << "Project::run() run_needed()=" << run_needed(verbosity) << std::endl;
  if (!force && !run_needed())
    return false;
  //  status(unevaluated);
  std::string line;
  std::string optionstring;
  property_set("run_input_hash", std::to_string(input_hash()));
  if (verbosity > 0 && backend.name != sjef::Backend::dummy_name)
    optionstring += "-v ";
  auto run_command = backend_parameter_expand(backend.name, backend.run_command);
  custom_run_preface();
  m_job.reset(nullptr);
  auto rundir = run_directory_new();
  m_trace(3 - verbosity) << "new run directory " << rundir << std::endl;
  m_xml_cached = "";
  m_trace(2 - verbosity) << "run job, backend=" << backend.name << std::endl;
  m_trace(2 - verbosity) << "initial run_command " << run_command << std::endl;
  auto spl = splitString(run_command);
  run_command = spl.front();
  for (auto sp = spl.rbegin(); sp < spl.rend() - 1; sp++)
    optionstring = "'" + *sp + "' " + optionstring;
  m_trace(3 - verbosity) << "run job " << run_command + " " + optionstring + std::to_string(rundir) + ".inp"
                         << std::endl;
  m_job.reset(new util::Job(*this));
  m_job->run(run_command + " " + optionstring + std::to_string(rundir) + ".inp", verbosity, false);
  property_set("jobnumber", std::to_string(m_job->job_number()));
  //    p_status_mutex.reset(); // TODO probably not necessary
  m_trace(3 - verbosity) << "jobnumber " << m_job->job_number() << std::endl;
  if (wait)
    this->wait();
  return true;
}

void Project::clean(bool oldOutput, bool output, bool unused, int keep_run_directories) {
  if (oldOutput || output) {
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
    throw std::invalid_argument("sjef::project::clean for unused files is not yet implemented");
  if (auto statuss = status(); statuss == running || statuss == waiting)
    keep_run_directories = std::max(keep_run_directories, 1);
  while (run_list().size() > size_t(keep_run_directories))
    run_delete(*run_list().rbegin());
}

void Project::kill(int verbosity) {
  if (status() == running or status() == waiting) {
    if (m_job == nullptr)
      m_job.reset(new util::Job(*this));
    m_job->kill(verbosity);
  }
}

bool Project::run_needed(int verbosity) const {
  auto start_time = std::chrono::steady_clock::now();
  m_trace(3 - verbosity)
      << ", time "
      << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
      << std::endl;
  if (auto statuss = status(); statuss == running || statuss == waiting)
    return false;
  auto inpfile = filename("inp");
  auto xmlfile = filename("xml", "", 0);
  m_trace(3 - verbosity) << "inpfile " << inpfile << std::endl;
  m_trace(3 - verbosity) << "xmlfile " << xmlfile << std::endl;
  m_trace(3 - verbosity)
      << ", time "
      << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
      << std::endl;
  m_trace(2 - verbosity) << "sjef::Project::run_needed, input file exists ?=" << fs::exists(inpfile) << std::endl;
  m_trace(3 - verbosity)
      << ", time "
      << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
      << std::endl;
  if (!fs::exists(inpfile))
    return false;
  m_trace(2 - verbosity) << "sjef::Project::run_needed, xml file exists ?=" << fs::exists(xmlfile) << std::endl;
  m_trace(3 - verbosity)
      << ", time "
      << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
      << std::endl;
  if (!fs::exists(xmlfile))
    return true;
  m_trace(3 - verbosity)
      << "sjef::Project::run_needed, time after initial checks "
      << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
      << std::endl;
  m_trace(3 - verbosity)
      << "before property_get, time "
      << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
      << std::endl;
  auto run_input_hash = property_get("run_input_hash");
  m_trace(3 - verbosity)
      << "after property_get, time "
      << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
      << std::endl;
  if (run_input_hash.empty()) { // if there's no input hash, look at the xml file instead
    m_trace(3 - verbosity) << "There's no run_input_hash, so compare output and input: "
                           << (std::regex_replace(std::regex_replace(file_contents("inp"), std::regex{"\r"}, ""),
                                                  std::regex{" *\n\n*"}, "\n") != input_from_output())
                           << std::endl;
    return (std::regex_replace(std::regex_replace(std::regex_replace(file_contents("inp"), std::regex{"\r"}, ""),
                                                  std::regex{" *\n\n*"}, "\n"),
                               std::regex{"\n$"}, "") != input_from_output());
  }
  {
    m_trace(3 - verbosity) << "sjef::Project::run_needed, input_hash =" << input_hash() << std::endl;
    std::stringstream sstream(run_input_hash);
    size_t i_run_input_hash;
    sstream >> i_run_input_hash;
    m_trace(3 - verbosity) << "sjef::Project::run_needed, run_input_hash =" << i_run_input_hash << std::endl;
    m_trace(3 - verbosity) << "sjef::Project::run_needed, input hash matches ?=" << (i_run_input_hash == input_hash())
                           << std::endl;
    if (i_run_input_hash != input_hash()) {
      m_trace(3 - verbosity) << "sjef::Project::run_needed returning true" << std::endl;
      m_trace(3 - verbosity) << "ending time "
                             << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                                      start_time)
                                    .count()
                             << std::endl;
      m_trace(3 - verbosity) << "because i_run_input_hash != input_hash()" << std::endl;
      return true;
    }
  }
  m_trace(3 - verbosity) << "sjef::Project::run_needed returning false" << std::endl;
  m_trace(3 - verbosity)
      << "ending"
      << ", time "
      << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count()
      << std::endl;
  return false;
}

std::string Project::xml(int run, bool sync) const {
  //  std::cout << "Project::xml() status="<<status()<<std::endl;
  if (status() != completed)
    return xmlRepair(file_contents(m_suffixes.at("xml"), "", run, sync));
  if (m_xml_cached.empty()) {
    m_xml_cached = xmlRepair(file_contents(m_suffixes.at("xml"), "", run, sync));
    //    std::cout << "m_xml_cached set to " << m_xml_cached << std::endl;
  }
  return m_xml_cached;
}

std::string Project::file_contents(const std::string& suffix, const std::string& name, int run, bool sync) const {

  std::ifstream s(filename(suffix, name, run));
  auto result = std::string(std::istreambuf_iterator<char>(s), std::istreambuf_iterator<char>());
  while (result.back() == '\n')
    result.pop_back();
  return result;
}

sjef::status Project::status() const {
  auto current_status = property_get("_status");
  return current_status.empty() ? unevaluated : static_cast<sjef::status>(std::stoi(current_status));
}

std::string sjef::Project::status_message(int verbosity) const {
  std::map<sjef::status, std::string> message;
  message[sjef::status::unknown] = "Not found";
  message[sjef::status::running] = "Running";
  message[sjef::status::waiting] = "Waiting";
  message[sjef::status::completed] = "Completed";
  message[sjef::status::unevaluated] = "Unevaluated";
  message[sjef::status::killed] = "Killed";
  auto statu = this->status();
  auto result = message[statu];
  if (statu != sjef::status::unknown && !property_get("jobnumber").empty())
    result += ", job number " + property_get("jobnumber") + " on backend " + property_get("backend");
  return result;
}

void Project::wait(unsigned int maximum_microseconds) const {
  unsigned int microseconds = 1;
  //  std::cout << "wait status="<<status()<<std::endl;
  while (status() == unknown or status() == running or status() == waiting) {
    //    std::cout << "in sjef::Project::wait() status "<<status()<<std::endl;
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
    if (microseconds < maximum_microseconds)
      microseconds *= 2;
  }
  //  std::cout << "wait status="<<status()<<std::endl;
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

void Project::property_set(const mapstringstring_t& properties) {
  auto lock = m_locker->bolt();
  check_property_file_locked();
  for (const auto& [property, value] : properties) {
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
mapstringstring_t Project::property_get(const std::vector<std::string>& properties) const {
  check_property_file();
  mapstringstring_t results;
  for (const std::string& property : properties) {
    std::string query{"/plist/dict/key[text()='" + property + "']/following-sibling::string[1]"};
    auto xpath_node = m_properties->select_node(query.c_str());
    auto node = xpath_node.node();
    std::string value = node.child_value();
    if (!value.empty())
      results[property] = value;
  }
  return results;
}

std::vector<std::string> Project::property_names() const {
  check_property_file();
  std::vector<std::string> result;
  for (const auto& node : m_properties->select_nodes(((std::string){"/plist/dict/key"}).c_str()))
    result.push_back(node.node().child_value());
  return result;
}

inline std::string slurp(const std::filesystem::path& path) {
  std::ostringstream buf;
  std::ifstream input(path);
  buf << input.rdbuf();
  return buf.str();
}

std::mutex s_recent_edit_mutex;
void Project::recent_edit(const std::filesystem::path& add, const std::filesystem::path& remove) {
  auto project_suffix =
      add.empty() ? fs::path(remove).extension().string().substr(1) : fs::path(add).extension().string().substr(1);
  const auto recent_projects_file = sjef_config_directory() / project_suffix / "projects";
  auto recent_projects_file_ = recent_projects_file;
  recent_projects_file_ += "-";
  bool changed = false;
  auto lock_threads = std::lock_guard(s_recent_edit_mutex);
  Locker locker{fs::path{recent_projects_file}.parent_path()};
  {
    auto lock = locker.bolt();
    if (!fs::exists(recent_projects_file)) {
      fs::create_directories(fs::path(recent_projects_file).parent_path());
      std::ofstream junk(recent_projects_file);
    }
    {
      std::ifstream in(recent_projects_file);
      std::ofstream out(recent_projects_file_);
      size_t lines = 0;
      if (!add.empty()) {
        out << add.string() << std::endl;
        changed = true;
        ++lines;
      }
      std::string line;
      while (getline(in, line) && lines < recentMax) {
        if (line != remove.string() && line != add && fs::exists(line)) {
          ++lines;
          out << line << std::endl;
        } else
          changed = true;
      }
      changed = changed || lines >= recentMax;
    }
    if (changed) {
      fs::remove(recent_projects_file);
      fs::rename(recent_projects_file_, recent_projects_file);
    } else
      fs::remove(recent_projects_file_);
  }
}

std::filesystem::path Project::filename(std::string suffix, const std::string& name, int run) const {
  fs::path result{m_filename};
  if (run > -1)
    result = run_directory(run);
  std::string basename = result.stem().string();
  if (m_suffixes.count(suffix) > 0)
    suffix = m_suffixes.at(suffix);
  if (suffix != "" && name == "")
    result /= basename + "." + suffix;
  else if (suffix != "" && name != "")
    result /= name + "." + suffix;
  else if (name != "")
    result /= name;
  return result;
}
std::string Project::name() const { return fs::path(m_filename).stem().string(); }

std::filesystem::path Project::run_directory(int run) const {
  if (run < 0)
    return filename();
  auto sequence = run_verify(run);
  if (sequence < 1)
    return filename(); // covers the case of old projects without run directories
  auto dir = fs::path{filename()} / "run" / (std::to_string(sequence) + "." + m_project_suffix);
  if (!fs::is_directory(dir))
    throw runtime_error("Cannot find directory " + dir.string());
  return dir.string();
}
int Project::run_directory_new() {
  auto dirlist = run_list();
  auto sequence = run_directory_next();
  dirlist.insert(sequence);
  std::stringstream ss;
  for (const auto& dir : dirlist)
    ss << dir << " ";
  property_set("run_directories", ss.str());
  auto rundir = fs::path{filename()} / "run";
  auto dir = rundir / (std::to_string(sequence) + "." + m_project_suffix);
  if (!fs::exists(rundir) && !fs::create_directories(rundir)) {
    throw runtime_error("Cannot create directory " + rundir.string());
  }
  property_delete("jobnumber");
  set_current_run(0);
  copy(dir.string(), false, false, true);
  return sequence;
}

void Project::run_delete(int run) {
  if (status() == running or status() == waiting)
    throw runtime_error("Cannot delete run directory when job is running or waiting");
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
}

int Project::recent_find(const std::string& suffix, const std::filesystem::path& filename) {
  auto recent_projects_directory = expand_path(sjef_config_directory() / suffix);
  fs::create_directories(recent_projects_directory);
  std::ifstream in(expand_path(recent_projects_directory / "projects"));
  std::string line;
  for (int position = 1; std::getline(in, line); ++position) {
    if (fs::exists(line)) {
      if (line == filename.string())
        return position;
    } else
      --position;
  }
  return 0;
}

int Project::recent_find(const std::filesystem::path& filename) const {
  return recent_find(m_project_suffix, filename);
}

std::string Project::recent(const std::string& suffix, int number) {
  auto recent_projects_directory = expand_path(sjef_config_directory() / suffix);
  fs::create_directories(recent_projects_directory);
  //  std::cout << "recent_projects_directory " << recent_projects_directory << std::endl;
  std::ifstream in(expand_path(recent_projects_directory / "projects"));
  std::string line;
  for (int position = 0; in >> line;) {
    //    std::cout << "line " << line << std::endl;
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
  void write(const void* data, size_t size) override {
    std::ofstream s(file);
    s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<!DOCTYPE plist SYSTEM '\"-//Apple//DTD PLIST 1.0//EN\" "
         "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"'>\n"
      << std::string{static_cast<const char*>(data), size};
  }
};

constexpr static bool use_writer = false;
void Project::load_property_file_locked() const {
  if (auto result = m_properties->load_file(propertyFile().c_str()); !result)
    throw runtime_error("error in loading " + propertyFile().string() + "\n" + result.description() + "\n" +
                        slurp(propertyFile()));
  m_property_file_modification_time = fs::last_write_time(propertyFile());
}

bool Project::properties_last_written_by_me(bool removeFile) const {
  auto path = fs::path{m_filename} / fs::path{writing_object_file};
  auto lock = m_locker->bolt();
  std::ifstream i{path.string(), std::ios_base::in};
  if (!i.is_open())
    return false;
  std::hash<const Project*> hasher;
  auto me = hasher(this);
  decltype(me) writer;
  i >> writer;
  if (removeFile && writer == me)
    fs::remove(path);
  return writer == me;
}
void Project::check_property_file() const {
  auto lock = m_locker->bolt();

  check_property_file_locked();
}
void Project::check_property_file_locked() const {
  auto lastwrite = fs::last_write_time(propertyFile());
  if (m_property_file_modification_time == lastwrite && !properties_last_written_by_me(false))
    m_property_file_modification_time -= std::chrono::milliseconds(1);
  if (m_property_file_modification_time < lastwrite) {
    load_property_file_locked();
    m_property_file_modification_time = lastwrite;
  }
}

void Project::save_property_file() const {
  auto lock = m_locker->bolt();
  save_property_file_locked();
}
void Project::save_property_file_locked() const {
  struct plist_writer writer;
  writer.file = propertyFile().string();
  if (!fs::exists(propertyFile())) {
    fs::create_directories(m_filename);
    { std::ofstream x(propertyFile()); }
  }
  if (use_writer)
    m_properties->save(writer, "\t", pugi::format_no_declaration);
  else
    m_properties->save_file(propertyFile().c_str());
  auto path = (fs::path{m_filename} / fs::path{writing_object_file});
  std::ofstream o{path.string()};
  std::hash<const Project*> hasher;
  o << hasher(this);
}

///> @private
inline std::string random_string(size_t length) {
  const char charset[] = "0123456789"
                         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                         "abcdefghijklmnopqrstuvwxyz";
  std::random_device dev;
  std::mt19937 rng(dev());
  std::uniform_int_distribution<std::mt19937::result_type> rn(0, sizeof(charset) - 1);
  auto randchar = [&rn, &rng, &charset]() { return charset[rn(rng)]; };
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
  std::ifstream ss(filename("inp"));
  std::string line;
  std::string input;
  while (std::getline(ss, line))
    input += referenced_file_contents(line) + "\n";
  return std::hash<std::string>{}(input);
}

///> @private
inline std::string environment(const std::string& key) {
  const char* s = std::getenv(key.c_str());
  if (s == nullptr) {
    if (key == "TMPDIR")
      return "/tmp";
    throw internal_error("Unknown environment variable " + key);
  }
  return s;
}

///> @private
std::filesystem::path expand_path(const std::filesystem::path& path, const std::string& suffix) {
  // TODO use more of std::filesystem in this parsing
  auto text = path.string();
#ifdef _WIN32
  text = std::regex_replace(text, std::regex{R"--(^\~)--"}, environment("USERPROFILE"));
  text = std::regex_replace(text, std::regex{R"--(^\$\{HOME\})--"}, environment("USERPROFILE"));
  text = std::regex_replace(text, std::regex{R"--(^\$HOME/)--"}, environment("USERPROFILE") + "/");
  text = std::regex_replace(text, std::regex{R"--(^\$\{TMPDIR\})--"}, environment("TEMP"));
  text = std::regex_replace(text, std::regex{R"--(^\$TMPDIR/)--"}, environment("TEMP") + "/");
#else
  text = std::regex_replace(text, std::regex{R"--(^\~)--"}, environment("HOME"));
#endif
  std::smatch match;
  while (std::regex_search(text, match, std::regex{R"--(\$([^{/]+)/)--"}))
    text.replace((match[0].first - text.cbegin()), (match[0].second - text.cbegin()), environment(match[1]) + "/");
  while (std::regex_search(text, match, std::regex{R"--(\$\{([^}]+)\})--"}))
    text.replace((match[0].first - text.cbegin()), (match[0].second - text.cbegin()), environment(match[1]));
  text = std::regex_replace(text, std::regex{R"--([/\\])--"}, std::string{fs::path::preferred_separator});
  if (text[0] != fs::path::preferred_separator && text[1] != ':') {
    text = (fs::current_path() / text).string();
  }
  if (text.back() == fs::path::preferred_separator)
    text.pop_back();
  if (fs::path{text}.extension().string() != std::string{"."} + suffix && !suffix.empty())
    text += "." + suffix;
  return text;
}

std::string xmlRepair(const std::string& source, const mapstringstring_t& injections) {
  if (source.empty()) { // empty source. Construct valid xml
    return std::string("<?xml version=\"1.0\"?><root/>");
  }
  std::vector<std::string> nodes;
  bool commentPending = false;
  auto s = source.begin();
  std::smatch match;
  while (std::regex_search(s, source.end(), match, std::regex("<[^>]*[^-]>|<!--|-->"))) {
    const auto& match0 = match[0];
    if (auto pattern = std::string{match0.first, match0.second}; pattern.substr(pattern.length() - 2) == "/>") {
      // do nothing if end xml tag
    } else if (pattern[1] == '/') { // no checking done that it's the right node
      if (!nodes.empty())
        nodes.pop_back();
    } else if (pattern.substr(0, 4) == "<!--") {
      commentPending = true;
    } else if (pattern.find("-->") != std::string::npos) {
      commentPending = false;
    } else if (pattern.size() > 1 && pattern[0] == '<' && (pattern[1] != '?') && !commentPending) {
      std::smatch matchnode;
      if (!std::regex_search(pattern, matchnode, std::regex("<([^> /]*)")) || matchnode.size() != 2)
        throw std::invalid_argument("bad stuff in parsing xml node");
      nodes.emplace_back(matchnode[1].first, matchnode[1].second);
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
    for (const auto& [key, value] : injections)
      if (*node == key)
        result += value;
    result += "</" + *node + ">";
  }
  return result;
}

std::vector<std::string> sjef::Project::backend_names() const {
  std::vector<std::string> result;
  for (const auto& [key, value] : this->m_backends)
    if (key != sjef::Backend::dummy_name)
      result.push_back(key);
  return result;
}

void sjef::Project::change_backend(std::string backend, bool force) {
  if (backend.empty())
    backend = sjef::Backend::default_name;
  bool unchanged = property_get("backend") == backend && m_backend == backend;
  if (!force && unchanged)
    return;
  if (!unchanged && !m_backend.empty()) {
    if (status() == running or status() == waiting)
      throw runtime_error("Cannot change backend when job is running or waiting");
    property_delete("jobnumber");
  }
  m_backend = backend;
  if (!unchanged) {
    throw_if_backend_invalid(backend);
    property_set("backend", backend);
  }
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
  for (const auto& [key, value] : m_backends)
    result = result && check_backend(key);
  return result;
}

bool sjef::Project::check_backends(const std::string& suffix) const {
  for (const auto& config_dir : std::vector<std::filesystem::path>{"/usr/local/etc/sjef", sjef_config_directory()}) {
    const auto config_file = expand_path(config_dir / suffix / "backends.xml");
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

void Project::add_backend(const std::string& name, const mapstringstring_t& fields) {
  m_backends[name] = localhost((fields.count("host") > 0 ? fields.at("host") : "localhost"))
                         ? Backend(Backend::local(), name)
                         : Backend(Backend::Linux(), name);
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

std::string version() noexcept { return SJEF_VERSION; }

pugi::xpath_node_set Project::select_nodes(const std::string& xpath_query, int run) const {
  auto xml = pugi::xml_document();
  xml.load_string(this->xml(run).c_str());
  return xml.select_nodes(xpath_query.c_str());
}

std::vector<std::string> Project::xpath_search(const std::string& xpath_query, const std::string& attribute,
                                               int run) const {
  auto node_set = select_nodes(xpath_query, run);
  std::vector<std::string> result;
  result.reserve(node_set.size());
  for (const auto& node : node_set) {
    if (attribute.empty())
      result.emplace_back(node.node().child_value());
    else
      result.emplace_back(node.node().attribute(attribute.c_str()).value());
  }
  return result;
}

const std::string Project::backend_cache() const {
  auto& backend = m_backends.at(m_backend);
  return backend.host + ":" + backend.cache + "/" + m_filename.string();
}

} // namespace sjef
