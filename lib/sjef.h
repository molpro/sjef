#ifndef SJEF_SJEF_H
#define SJEF_SJEF_H
#include <vector>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <boost/filesystem/path.hpp>
#include <boost/process/child.hpp>
#include <thread>
#include "sjef-backend.h"

namespace sjef {
class Backend; ///< @private
class pugi_xml_document; ///< @private
static constexpr int recentMax = 128;
enum status : int {
  unknown = 0,
  running = 1,
  waiting = 2,
  completed = 3,
};
class Project {
 protected:
  std::string m_project_suffix;
  std::string
      m_filename; ///< the name of the file bundle, expressed as an absolute pathname for the directory holding the bundle
//  std::vector<std::reference_wrapper<const Backend> > m_backend;
//  int m_jobnumber;
  std::vector<std::string> m_reserved_files; ///< Files which should never be copied back from backend
  bool m_erase_on_destroy;
  std::string m_writing_thread_file;
  std::unique_ptr<pugi_xml_document> m_properties;
  std::map<std::string, std::string> m_suffixes; ///< File suffixes for the standard files
  std::string m_recent_projects_file;
  std::map<std::string, Backend> m_backends;
  std::unique_ptr<pugi_xml_document> m_backend_doc;
  mutable std::unique_ptr<boost::process::ipstream> m_status_stream;
  mutable struct {
    boost::process::child process;
    boost::process::opstream in;
    boost::process::ipstream out;
    boost::process::ipstream err;
    std::string host;
  } m_remote_server;
  mutable std::string m_control_path_option;
  mutable std::chrono::milliseconds m_status_lifetime;
  mutable std::chrono::time_point<std::chrono::steady_clock> m_status_last;
  mutable sjef::status m_status;
  mutable std::thread m_backend_daemon;
  // put the flag into a container to deal conveniently with std:atomic_flag's lack of move constructor
  struct backend_daemon_flag_container {
    std::atomic_flag shutdown_flag;
    backend_daemon_flag_container() {}
    backend_daemon_flag_container(const backend_daemon_flag_container& source) {}
    backend_daemon_flag_container(const backend_daemon_flag_container&& source) {}
    std::mutex m_property_set_mutex;
  };
  mutable backend_daemon_flag_container m_unmovables;
 public:
  static const std::string s_propertyFile;
  /*!
   * @brief Construct, or attach to, a Molpro project bundle
   * @param filename The file name of the bundle. If it does not have suffix .sjef, it will be forced to do so.
   * @param source If not null, copy the bundle source. This is only for new projects, and an exception is thrown if *this has been attached to an existing project bundle.
   * @param erase_on_destroy If true, and the project was created new, then the destructor will destroy the disk copy of the project.
   * @param construct if false, do not actually build the project on disk. Can be used to generate the filename of the project.
   * @param default_suffix The file extension to be used for the project directory name if filename does not have one
   * @param suffixes The file suffixes for special (input, output) files within the project
   */
  explicit Project(const std::string& filename,
                   const Project* source = nullptr,
                   bool erase_on_destroy = false,
                   bool construct = true,
                   const std::string& default_suffix = "",
                   const std::map<std::string, std::string>& suffixes = {{"inp", "inp"}, {"out", "out"},
                                                                         {"xml", "xml"}});
  Project(Project&& source) = default;
  virtual ~Project();
  /*!
   * @brief Copy the project to another location
   * @param destination_filename
   * @param force whether to first remove anything already existing at the new location
   * @param keep_hash whether to clone the project_hash, or allow a fresh one to be generated
   * @return true if the copy was successful
   */
  bool copy(const std::string& destination_filename, bool force = false, bool keep_hash = false);
  /*!
   * @brief Move the project to another location
   * @param destination_filename
   * @param force whether to first remove anything already existing at the new location
   * @return true if the move was successful
   */
  bool move(const std::string& destination_filename, bool force = false);
  /*!
   * @brief Destroy the project
  */
  void erase();
  static void erase(const std::string& filename) {
//    std::cerr << "sjef::project::erase "<<filename<<std::endl;
    Project x(filename, nullptr, true, false);
  }
  /*!
   * @brief Import one or more files into the project. In the case of a .xml output file, if the corresponding
   * input file does not exist, it will be generated.
   * @param file
   * @param overwrite Whether to overwrite an existing file.
   */
  bool import_file(std::string file, bool overwrite = false);
  bool import_file(const std::vector<std::string> files, bool overwrite = false) {
    bool result;
    for (const auto& file : files) result &= import_file(file, overwrite);
    return result;
  }
  /*!
   * @brief Export one or more files from the project.
   * @param file The relative or absolute path name of the destination; the base name will be used to locate the file in the project.
   * @param overwrite Whether to overwrite an existing file.
   */
  bool export_file(std::string file, bool overwrite = false);
  bool export_file(const std::vector<std::string>& files, bool overwrite = false) {
    bool result;
    for (const auto& file : files) result &= export_file(file, overwrite);
    return result;
  }
  /*!
   * @brief Synchronize the project with a cached copy belonging to a backend. name.inp, name.xyz, Info.plist, and any files brought in with import(), will be pushed from the
   * master copy to the backend, and all other files will be pulled from the backend.
   * @param name
   * @param verbosity If >0, show underlying processing
   * @return
   */
  bool synchronize(std::string name="", int verbosity = 0) const;
 private:
  bool synchronize(const Backend& backend, int verbosity = 0, bool nostatus = false) const;
 public:
  /*!
   * @brief Start a sjef job
   * @param name The name of the backend
   * @param options Any options to be passed to the command run on the backend
   * @param verbosity If >0, show underlying processing
   * @param force Whether to force submission of the job even though run_needed() reports that it's unnecessary
   * @param wait Whether to wait for the job to complete instead of returning after launching it
   * @return
   */
  bool run(std::string name,
           std::vector<std::string> options = std::vector<std::string>{},
           int verbosity = 0,
           bool force = false, bool wait = false);
 public:
  /*!
   * @brief Obtain the status of the job started by run()
   * @param verbosity
   * - 0 print nothing
   * - 1 show result from underlying status commands
   * @param cached If true, don't actually get status, but instead use the last cached value.
   * initiate the request and return immediately; a subsequent call with wait=true will complete
   * @return
   * - 0 not found
   * - 1 running
   * - 2 queued
   */
  sjef::status status(int verbosity = 0, bool cached = true) const;
  /*!
   * @brief Wait unconditionally for status() to return 'completed'
   * @param maximum_microseconds The poll interval is successively increased between calls to status() until reaching this value.
   */
  void wait(unsigned int maximum_microseconds = 10000) const;
  /*!
   * @brief Kill the job started by run()
   * @return
   */
  void kill();
  /*!
   * @brief Check whether the job output is believed to be out of date with respect to the input and any other files contained in the project that might be used in the job.
   * @param verbosity Show information on decision if greater than 0
   * @return true if the output is not up to date, false otherwise
   */
  bool run_needed(int verbosity = 0);
  /*!
 * @brief If possible, construct the input embedded in the output file
 * @return The input used to construct the output, or, if that can't be deduced, an empty string.
 */
  std::string input_from_output() const;
  void rewrite_input_file(const std::string& input_file_name, const std::string& old_name);
  /*!
   * @brief Get the xml output, completing any open tags if necessary
   * @return
   */
  std::string xml() const;
  /*!
   * @brief Obtain the contents of a project file
   * @param suffix If present without \c name, look for a primary file with that type. If absent, the file name of the bundle is instead selected
   * @param name If present,  look for a file of this name, appended with .\c suffix if that is non-blank
   * @param sync Whether to force a synchronisation with backend before getting the file contents
   * @return the fully-qualified name of the file
   */
  std::string file_contents(const std::string& suffix = "", const std::string& name = "", bool sync = true) const;

  /*!
   * @brief Remove potentially unwanted files from the project
   * @param oldOutput Whether to remove old output files
   * @param output Whether to remove all output files
   * @param unused Whether to remove unused files
   */
  void clean(bool oldOutput = true, bool output = false, bool unused = false);
  /*!
   * @brief Set a variable
   * @param property
   * @param value
   * @param save whether to write the updated property set to disk. Do not set to false unless you are sure you are going to immediately do something else that does save the properties - there is potential for data race with other objects mapping the project.
   */
  void property_set(const std::string& property, const std::string& value, bool save = true);
  /*!
   * @brief Get the value of a variable
   * @param property
   * @return The value, or "" if key does not exist
   */
  std::string property_get(const std::string& property) const;
  /*!
   * @brief Remove a variable
   * @param property
   * @param save whether to write the updated property set to disk. Do not set to false unless you are sure you are going to immediately do something else that does save the properties - there is potential for data race with other objects mapping the project.
   */
  void property_delete(const std::string& property, bool save = true);
  /*!
   * @brief Set the pointer for property_next() to the beginning of the list of variables.
   */
  void property_rewind();
  /*!
   * @brief Get the sequentially next variable.
   * @return
   */
  std::string property_next();
  /*!
   * @brief Get the file name of the bundle, or a primary file of particular type, or a general file in the bundle
   * @param suffix If present without \c name, look for a primary file with that type. If absent, the file name of the bundle is instead selected
   * @param name If present,  look for a file of this name, appended with .\c suffix if that is non-blank
   * @return the fully-qualified name of the file
   */
  std::string filename(std::string suffix = "", const std::string& name = "") const;
  /*!
   * @brief
   * @return the base name of the project, ie its file name with directory and suffix stripped off
   */
  std::string name() const;
  /*!
   * @brief Look for a project by name in the user-global recent project list
   * @param filename
   * @return 0 if failure, otherwise the rank of the project (1 is newest)
   */
  int recent_find(const std::string& filename) const;
  /*!
   * @brief Look for a project by rank in the user-global recent project list
   * @param number the rank of the project (1 is newest)
   * @return the filename of the project, or "" if not found
   */
  std::string recent(int number = 1) const;
  void ensure_remote_server() const;
  /*!
   * @brief Change the active backend
   * @param backend The name of the backend. If not a valid name, the function throws std::invalid_argument.
   */
  void change_backend(std::string backend = std::string{""});
 protected:
  std::string get_project_suffix(const std::string& filename, const std::string& default_suffix) const;
  void recent_edit(const std::string& add, const std::string& remove = "");
  mutable time_t m_property_file_modification_time;
  void check_property_file() const;
  void save_property_file() const;
  void load_property_file() const;
  bool properties_last_written_by_me(bool removeFile = false) const;
  std::string propertyFile() const;
  std::string cache(const Backend& backend) const;
  void force_file_names(const std::string& oldname);
  static void backend_daemon(sjef::Project& project, const std::string& backend, int wait_milliseconds);
  void shutdown_backend_daemon();
  /*!
   * @brief Take a line from a program input file, and figure out whether it references some other files that would influence the program behaviour. If so, return the contents of those files; otherwise, return the line.
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
   * in a new hash string, but this can be overridden if an exact clone is really wanted.
   * @return hash
   */
  size_t project_hash();
  /*!
   * @brief  Construct a hash that is unique to the contents of the input file and anything it references.
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
   * - <tt>{prologue text%%param}</tt> is replaced by the value of project property <tt>Backend/</tt>backend<tt>/param</tt> if it is defined, prefixed by <tt>prologue text</tt>. Otherwise, the entire contents between <tt>{}</tt> is elided.
   * - <tt>{prologue %%param:default value}</tt> works similarly, with substitution of <tt>default value</tt> instead of elision if <tt>param</tt> is not defined.
   * @return The expanded string
   */
  std::string backend_parameter_expand(const std::string& backend, const std::string& templ);

  /*!
   * @brief Get all of the parameters referenced in the run_command of a backend
   * @param backend The name of the backend
   * @return A map where the keys are the parameter names, and the values the defaults.
   */
  std::map<std::string, std::string> backend_parameters(const std::string& backend) const;

  void backend_parameter_set(const std::string& backend, const std::string& name, const std::string& value) {
    property_set("Backend/" + backend + "/" + name, value);
  }

  void backend_parameter_delete(const std::string& backend, const std::string& name) {
    property_delete("Backend/" + backend + "/" + name);
  }

  std::string backend_parameter_get(const std::string& backend, const std::string& name) const {
    return property_get("Backend/" + backend + "/" + name);
  }

  /*!
   * @brief Get the names of all the backend objects associated with the object
   * @return
   */
  std::vector<std::string> backend_names() const;

  /*!
   * @brief Introduce a new backend to the project and to the user's global backend configuration for all projects of the same type.
   * @param name The name of the backend to be created. An existing backend of the same name will be overwritten
   * @param fields The backend fields to be specified
   */
  void add_backend(const std::string& name, const std::map<std::string, std::string>& fields);

  /*!
   * @brief Remove a backend from the project and from the user's global backend configuration for all projects of the same type.
   * @param name The name of the backend to be removed.
   */
  void delete_backend(const std::string& name);
};

/*!
 * @brief Edit a file path name
 * - expand environment variables
 * - expand ~ as the user home directory
 * - map unix-specific environment variables HOME, TMPDIR to native counterparts
 * - force all path separators (/,\) to native form
 * - resolve a relative input path to its fully-resolved form using the current working directory
 * - if the file name does not end with a supplied suffix, append it
 * @param path
 * @param suffix If given and not empty, if path does not end with this file suffix, append it
 * @return the expanded and sanitised path
 */
std::string expand_path(const std::string& path, const std::string& suffix = "");

/*!
 * @brief Repair an xml dataset by completing any open tags
 * @param source The initial xml
 * @param injections xml nodes for which additional place-holder xml should be injected if the node has to be completed. The key is the node name, and the value the xml to be injected
 * @return The repaired xml
 */
std::string xmlRepair(const std::string& source,
                      const std::map<std::string, std::string>& injections = {});

}

#endif //SJEF_SJEF_H
