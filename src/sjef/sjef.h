#ifndef SJEF_SJEF_H
#define SJEF_SJEF_H
#include <sjef/util/Logger.h>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace pugi {
class xpath_node_set; ///< @private
}
namespace sjef {
namespace util {
class Job;
class Locker;  ///< @private
}
class Backend; ///< @private
using util::Logger;
using util::Locker;
struct pugi_xml_document; ///< @private
static constexpr int recentMax = 128;
enum status : int { unknown = 0, running = 1, waiting = 2, completed = 3, unevaluated = 4, killed = 5, failed=6 };
using mapstringstring_t = std::map<std::string, std::string>;

class Project {
private:
  std::string m_project_suffix;
  std::filesystem::path m_filename; ///< the name of the file bundle, expressed as an absolute
                                    ///< pathname for the directory holding the bundle
  std::vector<std::filesystem::path> m_reserved_files = std::vector<std::filesystem::path>{
      sjef::Project::s_propertyFile}; ///< Files which should never be copied back from backend
  std::unique_ptr<pugi_xml_document> m_properties;
  mapstringstring_t m_suffixes; ///< File suffixes for the standard files
  std::map<std::string, Backend> m_backends;

  std::unique_ptr<pugi_xml_document> m_backend_doc;
  // put the flag into a container to deal conveniently with std:atomic_flag's
  // lack of move constructor
  struct unmovables_container {
    std::mutex m_property_set_mutex;
  };
  mutable unmovables_container m_unmovables;
  mutable std::string m_backend; ///< The current backend
  mutable std::string m_xml_cached;
  ///> @private
  static const std::string s_propertyFile;
  ///> @private
  std::shared_ptr<Locker> m_locker;
  mutable Logger m_warn{std::cerr, Logger::Levels::WARNING, {"sjef:: Error: ", "sjef:: Warning: ", "sjef:: Note:"}};
  mutable Logger m_trace{std::cout, Logger::Levels::QUIET};
  friend class util::Job;
  mutable std::unique_ptr<util::Job> m_job;

public:
  /*!
   * @brief Construct, or attach to, a Molpro project bundle
   * @param filename The file name of the bundle. If it does not have suffix
   * .sjef, it will be forced to do so.
   * @param construct if false, do not actually build the project on disk. Can
   * be used to generate the filename of the project.
   * @param default_suffix The file extension to be used for the project
   * directory name if filename does not have one
   * @param suffixes The file suffixes for special (input, output) files within
   * the project
   */
  explicit Project(const std::filesystem::path& filename, bool construct = true, const std::string& default_suffix = "",
                   const mapstringstring_t& suffixes = {{"inp", "inp"}, {"out", "out"}, {"xml", "xml"}});
  //  explicit Project(const std::string& filename, bool construct = true, const std::string& default_suffix = "",
  //                   const mapstringstring_t& suffixes = {{"inp", "inp"}, {"out", "out"}, {"xml", "xml"}}) :
  //                   Project(std::filesystem::path(filename),construct,default_suffix,suffixes) {}
  Project(const Project& source) = delete;
  Project(const Project&& source) = delete;
  virtual ~Project();
  /*!
   * @brief Copy the project to another location
   * @param destination_filename
   * @param force whether to first remove anything already existing at the new
   * location
   * @param keep_hash whether to clone the project_hash, or allow a fresh one to
   * be generated
   * @param slave if set, (a) omit copying the run directory (b) do not register
   * the project in recent projects list
   * @param keep_run_directories  Keep up to this number of run directories unless slave is set
   * @return true if the copy was successful
   */
  bool copy(const std::filesystem::path& destination_filename, bool force = false, bool keep_hash = false,
            bool slave = false, int keep_run_directories = std::numeric_limits<int>::max());
  /*!
   * @brief Move the project to another location
   * @param destination_filename
   * @param force whether to first remove anything already existing at the new
   * location
   * @return true if the move was successful
   */
  bool move(const std::filesystem::path& destination_filename, bool force = false);
  /*!
   * @brief Erase a project from the file system, and remove it from the recent
   * projects file
   */
  static void erase(const std::filesystem::path& filename, const std::string& default_suffix = "");
  /*!
   * @brief Import one or more files into the project. In the case of a .xml
   * output file, if the corresponding input file does not exist, it will be
   * generated.
   * @param file
   * @param overwrite Whether to overwrite an existing file.
   */
  bool import_file(const std::filesystem::path& file, bool overwrite = false);
  bool import_file(const std::vector<std::string>& files, bool overwrite = false) {
    bool result = true;
    for (const auto& file : files)
      result &= import_file(file, overwrite);
    return result;
  }
  /*!
   * @brief Export one or more files from the project.
   * @param file The relative or absolute path name of the destination; the base
   * name will be used to locate the file in the project.
   * @param overwrite Whether to overwrite an existing file.
   */
  bool export_file(const std::filesystem::path& file, bool overwrite = false);
  bool export_file(const std::vector<std::string>& files, bool overwrite = false) {
    bool result = true;
    for (const auto& file : files)
      result &= export_file(file, overwrite);
    return result;
  }
  /*!
   * @brief Set the warning/error diagnostic level and destination
   * @param stream
   * @param level
   * @param preambles
   */
  void set_warnings(const Logger::Levels level = Logger::Levels::WARNING, std::ostream& stream = std::cerr,
                    std::vector<std::string> preambles = {"sjef:: Error: ", "sjef:: Warning: ", "sjef:: Note:"}) {
    m_warn = Logger{stream, level, std::move(preambles)};
  }
  void set_verbosity(int verbosity, std::ostream& stream = std::cout) { m_trace = Logger(stream, verbosity); }

  /*!
   * @brief Start a sjef job
   * @param verbosity If >0, show underlying processing
   * @param force Whether to force submission of the job even though
   * run_needed() reports that it's unnecessary
   * @param wait Whether to wait for the job to complete instead of returning
   * after launching it
   * @return
   */
  bool run(int verbosity = 0, bool force = false, bool wait = false);
  bool run(const std::string& name, int verbosity = 0, bool force = false, bool wait = false) {
    change_backend(name);
    return run(verbosity, force, wait);
  }

  /*!
   * @brief Obtain the status of the job started by run()
   * @return
   * - 0 not found
   * - 1 running
   * - 2 waiting
   * - 3 completed
   * - 4 unevaluated
   * - 5 killed
   */
  sjef::status status() const;
  /*!
   *
   * @return An informative string about job status
   */
  std::string status_message(int verbosity = 0) const;
  /*!
   * @brief Wait unconditionally for status() to return neither 'waiting' nor
   * 'running'
   * @param maximum_microseconds The poll interval is successively increased
   * between calls to status() until reaching this value.
   */
  void wait(unsigned int maximum_microseconds = 10000) const;
  /*!
   * @brief Kill the job started by run()
   */
  void kill(int verbosity = 0);
  /*!
   * @brief Check whether the job output is believed to be out of date with
   * respect to the input and any other files contained in the project that
   * might be used in the job.
   * @param verbosity Show information on decision if greater than 0
   * @return true if the output is not up to date, false otherwise
   */
  bool run_needed(int verbosity = 0) const;
  /*!
   * @brief If possible, construct the input embedded in the output file
   * @param sync Whether to force a synchronisation with backend before getting
   * the file contents
   * @return The input used to construct the output, or, if that can't be
   * deduced, an empty string.
   */
  std::string input_from_output(bool sync = true) const;
  /*!
   * @brief Obtain the job status, if possible, by examining the output
   * @return The status. If nothing can be found, returns status()
   */
  sjef::status status_from_output() const;
  void rewrite_input_file(const std::string& input_file_name, const std::string& old_name);
  /*!
   * @brief Perform any project initialisation specific to the project suffix
   */
  void custom_initialisation();
  /*!
   * @brief Before launching a job, perform any required actions specific to the
   * project suffix
   */
  void custom_run_preface();
  /*!
   * @brief Get the xml output, completing any open tags if necessary
   * @param run If present, look for the file in a particular run directory.
   * Otherwise it will search in the currently focussed run directory, and if
   * not found, the main directory
   * @param sync Whether to force a synchronisation with backend before getting
   * the file contents
   * @return
   */
  std::string xml(int run = 0, bool sync = true) const;
  /*!
   * @brief Obtain the contents of a project file
   * @param suffix If present without \c name, look for a primary file with that
   * type. If absent, the file name of the bundle is instead selected
   * @param name If present,  look for a file of this name, appended with .\c
   * suffix if that is non-blank
   * @param run If present, look for the file in a particular run directory.
   * Otherwise it will search in the currently focussed run directory, and if
   * not found, the main directory
   * @param sync Whether to force a synchronisation with backend before getting
   * the file contents
   * @return the contents of the file
   */
  std::string file_contents(const std::string& suffix = "", const std::string& name = "", int run = 0,
                            bool sync = true) const;

  /*!
   * @brief Remove potentially unwanted files from the project
   * @param oldOutput Whether to remove old output files
   * @param output Whether to remove all output files
   * @param unused Whether to remove unused files
   * @param keep_run_directories Maximum number of run directories to keep
   */
  void clean(bool oldOutput = true, bool output = false, bool unused = false, int keep_run_directories = 0);
  /*!
   * @brief Set a property
   * @param property
   * @param value
   */
  void property_set(const std::string& property, const std::string& value);
  /*!
   * @brief Set one or more properties
   * @param properties Key-value pairs of properties to be set
   */
  void property_set(const mapstringstring_t& properties);
  /*!
   * @brief Get the value of a property
   * @param property
   * @return The value, or "" if key does not exist
   */
  std::string property_get(const std::string& property) const;
  /*!
   * @brief Get the values of several properties
   * @param properties
   * @return For each property found, a key-value pair
   */
  mapstringstring_t property_get(const std::vector<std::string>& properties) const;
  /*!
   * @brief Remove a variable
   * @param property
   */
  void property_delete(const std::string& property);
  void property_delete(const std::vector<std::string>& properties);
  /*!
   * @brief Get the names of all assigned properties
   * @return
   */
  std::vector<std::string> property_names() const;
  /*!
   * @brief Get the file name of the bundle, or a primary file of particular
   * type, or a general file in the bundle
   * @param suffix If present without \c name, look for a primary file with that
   * type. If absent, the file name of the bundle is instead selected
   * @param name If present,  look for a file of this name, appended with .\c
   * suffix if that is non-blank
   * @param run If specified, look in a run directory for the file, instead of
   * the main project directory. A value of 0 is interpreted as the most recent
   * run directory.
   * @return the fully-qualified name of the file
   */
  std::filesystem::path filename(std::string suffix = "", const std::string& name = "", int run = -1) const;
  /*!
   * @brief Obtain the path of a run directory
   * @param run
   * - 0: the currently focussed run directory
   * - other: the specified run directory
   * @return the fully-qualified name of the directory
   */
  std::filesystem::path run_directory(int run = 0) const;
  /*!
   * @brief Check a run exists, and resolve most recent
   * @param run The run number to check
   * @return run, or the most recent if run was zero. If the requested run is
   * not found, return 0
   */
  int run_verify(int run) const;
  /*!
   * @brief Obtain the list of run numbers in reverse order, ie the most recent
   * first
   * @return
   */
  using run_list_t = std::set<int, std::greater<int>>;
  run_list_t run_list() const;
  /*!
   * @brief Create a new run directory. Also copy into it the input file, and
   * any of its dependencies
   * @return The sequence number of the new run directory
   */
  int run_directory_new();
  /*!
   * @brief Delete a run directory
   * @param run
   */
  void run_delete(int run);
  /*!
   * @brief Obtain the sequence number of the next run directory to be created
   * @return
   */
  int run_directory_next() const;
  /*!
   * @brief
   * @return the base name of the project, ie its file name with directory and
   * suffix stripped off
   */
  std::string name() const;
  /*!
   * @brief Look for a project by name in the user-global recent project list
   * @param suffix the project suffix
   * @param filename
   * @return 0 if failure, otherwise the rank of the project (1 is newest)
   */
  static int recent_find(const std::string& suffix, const std::filesystem::path& filename);
  int recent_find(const std::filesystem::path& filename) const;
  /*!
   * @brief Look for a project by rank in the user-global recent project list
   * @param suffix the project suffix
   * @param number the rank of the project (1 is newest)
   * @return the filename of the project, or "" if not found
   */
  static std::string recent(const std::string& suffix, int number = 1);
  std::string recent(int number = 1) const;
  /*!
   * @brief Change the active backend
   * @param backend The name of the backend. If not a valid name, the function
   * throws std::invalid_argument.
   * @param force If false, and the current backend is equal to backend, do
   * nothing
   */
  void change_backend(std::string backend = std::string{""}, bool force = false);

  const std::map<std::string, Backend>& backends() const { return m_backends; }
  std::map<std::string, Backend>& backends() { return m_backends; }

private:
  Backend default_backend();
  void throw_if_backend_invalid(std::string backend = "") const;
  std::string get_project_suffix(const std::filesystem::path& filename, const std::string& default_suffix) const;
  static void recent_edit(const std::filesystem::path& add, const std::filesystem::path& remove = "");
  mutable std::filesystem::file_time_type m_property_file_modification_time;
  mutable std::map<std::string, std::filesystem::file_time_type, std::less<>> m_input_file_modification_time;
  std::set<std::string, std::less<>> m_run_directory_ignore;
  void property_delete_locked(const std::string& property);
  void check_property_file_locked() const;
  void check_property_file() const;
  void save_property_file_locked() const;
  void save_property_file() const;
  void load_property_file_locked() const;
  bool properties_last_written_by_me(bool removeFile = false) const;
  bool check_backends(const std::string& suffix) const;

public:
  std::filesystem::path propertyFile() const;
  /*!
   * @brief Get the location of the backend cache
   * @return host, path
   */
  const std::string backend_cache() const;

private:
  std::string cache(const Backend& backend) const;
  void force_file_names(const std::string& oldname);
  static void backend_watcher(sjef::Project& project_, int min_wait_milliseconds, int max_wait_milliseconds = 0,
                              int poll_milliseconds = 1);
  void shutdown_backend_watcher();
  /*!
   * @brief Take a line from a program input file, and figure out whether it
   * references some other files that would influence the program behaviour. If
   * so, return the contents of those files; otherwise, return the line.
   * @param line
   * @return
   */
  std::string referenced_file_contents(const std::string& line) const;

public:
  static const std::vector<std::string> suffix_keys;
  /*
   */
  /*!
   * @brief Return a globally-unique hash to identify the project.
   * The hash is generated on first call to this function, and should be relied
   * on to never change, including after calling move(). copy() normally results
   * in a new hash string, but this can be overridden if an exact clone is
   * really wanted.
   * @return hash
   */
  size_t project_hash();
  /*!
   * @brief  Construct a hash that is unique to the contents of the input file
   * and anything it references.
   * @return hash
   */
  size_t input_hash() const;

  /*!
   * @brief Obtain the value of a field in a backend
   * @param backend
   * @param key
   * @return
   */
  std::string backend_get(const std::string& backend, const std::string& key) const;

  /*!
   * @brief Perform parameter substitution for a backend run_command template
   * @param backend The name of the backend
   * @param templ The template to be expanded
   * - <tt>{prologue text%%param}</tt> is replaced by the value of project
   * property <tt>Backend/</tt>backend<tt>/param</tt> if it is defined, prefixed
   * by <tt>prologue text</tt>. Otherwise, the entire contents between
   * <tt>{}</tt> is elided.
   * - <tt>{prologue %%param:default value}</tt> works similarly, with
   * substitution of <tt>default value</tt> instead of elision if <tt>param</tt>
   * is not defined. If templ is not specified, the run_command of backend will
   * be used.
   * @return The expanded string
   */
  std::string backend_parameter_expand(const std::string& backend, std::string templ = "") const;

  /*!
   * @brief Get all of the parameters referenced in the run_command of a backend
   * @param backend The name of the backend
   * @param doc Whether to return documentation instead of default values
   * @return A map where the keys are the parameter names, and the values the
   * defaults.
   */
  mapstringstring_t backend_parameters(const std::string& backend, bool doc = false) const;

  void backend_parameter_set(const std::string& backend, const std::string& name, const std::string& value) {
    property_set("Backend/" + backend + "/" + name, value);
  }

  void backend_parameter_delete(const std::string& backend, const std::string& name) {
    property_delete("Backend/" + backend + "/" + name);
  }

  std::string backend_parameter_get(const std::string& backend, const std::string& name) const {
    auto p = property_get("Backend/" + backend + "/" + name);
    if (p.find("!") == std::string::npos)
      return p;
    return p.substr(0, p.find("!"));
  }

  /*!
   * @brief Return the documentation associated with a backend run parameter
   * @param backend The name of the backend
   * @param name The parameter
   * @return
   */
  std::string backend_parameter_documentation(const std::string& backend, const std::string& name) const {
    auto ps = backend_parameters(backend, true);
    if (ps.count(name) == 0)
      return "";
    return ps.at(name);
  }

  /*!
   * @brief Return the default value associated with a backend run parameter
   * @param backend The name of the backend
   * @param name The parameter
   * @return
   */
  std::string backend_parameter_default(const std::string& backend, const std::string& name) const {
    auto ps = backend_parameters(backend, false);
    if (ps.count(name) == 0)
      return "";
    return ps.at(name);
  }

  /*!
   * @brief Get the names of all the backend objects associated with the object
   * @return
   */
  std::vector<std::string> backend_names() const;

  /*!
   * @brief Introduce a new backend to the project and to the user's global
   * backend configuration for all projects of the same type.
   * @param name The name of the backend to be created. An existing backend of
   * the same name will be overwritten
   * @param fields The backend fields to be specified
   */
  void add_backend(const std::string& name, const mapstringstring_t& fields);

  /*!
   * @brief Remove a backend from the project and from the user's global backend
   * configuration for all projects of the same type.
   * @param name The name of the backend to be removed.
   */
  void delete_backend(const std::string& name);

  /*!
   * @brief Check whether the specification of a backend is valid
   * @param name The name of the backend to be checked.
   * @return
   */
  bool check_backend(const std::string& name) const;

  /*!
   * @brief Check the specification of all backends for validity
   * @return
   */
  bool check_all_backends() const;

  /*!
   * @brief Copy files from a run directory to the main project.
   * @param run Specifies the run to use as source, with 0 meaning the currently
   * focussed run directory.
   * @param fromname The file to copy.
   * @param toname The destination, defaulting to fromname.
   */
  void take_run_files(int run = 0, const std::string& fromname = "", const std::string& toname = "") const;

  /*!
   * @brief Set the focussed run directory
   * @param run The index of an existing run directory, a positive integer, or
   * zero, indicating that the focus is on the most recent run directory
   */
  void set_current_run(unsigned int run = 0);

  /*!
   * @brief Get the focussed run directory
   * @return The run directory, or zero if there is no run directory yet.
   */
  unsigned int current_run() const;

  /*!
   * @brief General XPath search on the xml document. Needs the pugixml library to parse the result
   * @param xpath_query
   * @param run
   * @return
   */
  pugi::xpath_node_set select_nodes(const std::string& xpath_query, int run = 0) const;
  /*!
   * @brief Simple XPath search on the xml document. For each matching node found, return a string that
   * contains the value of a specified attribute, or if the attribute is omitted, the node contents.
   * @param xpath_query
   * @param attribute
   * @param run
   * @return
   */
  std::vector<std::string> xpath_search(const std::string& xpath_query, const std::string& attribute = "",
                                        int run = 0) const;
};

/*!
 * @brief Check whether a backend specification file is valid. Only the
 * top-level structure of the file is checked, to the point where it could be
 * opened and used in a Project.
 * @param suffix /usr/local/etc/sjef/suffix/backends.xml and
 * ~/.sjef/suffix/backends.xml will be checked
 * @return
 */
bool check_backends(const std::string& suffix);

/*!
 * @brief Edit a file path name
 * - expand environment variables
 * - expand ~ as the user home directory
 * - map unix-specific environment variables HOME, TMPDIR to native counterparts
 * - force all path separators (/,\) to native form
 * - resolve a relative input path to its fully-resolved form using the current
 * working directory
 * - if the file name does not end with a supplied suffix, append it
 * @param path
 * @param suffix If given and not empty, if path does not end with this file
 * suffix, append it
 * @return the expanded and sanitised path
 */
std::filesystem::path expand_path(const std::filesystem::path& path, const std::string& suffix = "");

/*!
 * @brief Repair an xml dataset by completing any open tags
 * @param source The initial xml
 * @param injections xml nodes for which additional place-holder xml should be
 * injected if the node has to be completed. The key is the node name, and the
 * value the xml to be injected
 * @return The repaired xml
 */
std::string xmlRepair(const std::string& source, const mapstringstring_t& injections = {});

/*!
 * @brief Report the software version
 * @return
 */
std::string version() noexcept;

class runtime_error : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

} // namespace sjef

#endif // SJEF_SJEF_H
