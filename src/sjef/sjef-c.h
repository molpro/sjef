#ifndef SJEF_LIB_SJEF_C_H_
#define SJEF_LIB_SJEF_C_H_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
#include <stddef.h>
int sjef_project_open(const char* project);
void sjef_project_close(const char* project);
int sjef_project_copy(const char* project, const char* destination_filename, int keep_hash, int keep_run_directories);
int sjef_project_move(const char* project, const char* destination_filename);
void sjef_project_erase(const char* project);
void sjef_project_property_erase(const char* project);
void sjef_project_clean(const char* project, int keep_run_directories);
int sjef_project_import(const char* project, const char* file);
int sjef_project_export(const char* project, const char* file);
int sjef_project_run_needed(const char* project);
int sjef_project_synchronize(const char* project, const char* backend, int verbosity);
int sjef_project_run(const char* project, const char* backend, int verbosity, int force, int wait);
int sjef_project_status(const char* project, int verbosity);
const char* sjef_project_status_message(const char* project, int verbosity);
int sjef_project_status_initiate(const char* project, int verbosity);
void sjef_project_kill(const char* project);
void sjef_project_property_set(const char* project, const char* key, const char* value);
void sjef_project_properties_set(const char* project, const char** key, const char** value);
char* sjef_project_property_get(const char* project, const char* key);
char** sjef_project_properties_get(const char* project, const char** key);
void sjef_project_property_delete(const char* project, const char* key);
char* sjef_project_filename(const char* project);
const char* sjef_project_backend_cache(const char* project);
/*!
 * @brief Get the file name of the bundle, or a primary file of particular type, or a general file in the bundle
 * @param project The project
 * @param suffix If present without \c name, look for a primary file with that type. If absent, the file name of the
 * bundle is instead selected
 * @param name If present,  look for a file of this name, appended with .\c suffix if that is non-blank
 * @param run If specified, look in a run directory for the file, instead of the main project directory. A value of 0 is
 * interpreted as the most recent run directory.
 * @return the fully-qualified name of the file
 */
char* sjef_project_filename_general(const char* project, const char* suffix, const char* name, int run);
char* sjef_project_name(const char* project);
size_t sjef_project_project_hash(const char* project);
size_t sjef_project_input_hash(const char* project);
int sjef_project_recent_find(const char* filename);
char* sjef_project_recent(int number, const char* suffix);
int sjef_project_change_backend(const char* project, const char* backend);
char* sjef_project_backend_parameter_get(const char* project, const char* backend, const char* parameter);
char* sjef_project_backend_parameter_expand(const char* project, const char* backend, const char* templ);
void sjef_project_backend_parameter_set(const char* project, const char* backend, const char* parameter,
                                        const char* value);
void sjef_project_backend_parameter_delete(const char* project, const char* backend, const char* parameter);
/*!
 * @brief Get all of the parameters referenced in the run_command of a backend
 * @param project The name of the project
 * @param backend The name of the backend
 * @param def If zero, return the parameter names, otherwise the default values.
 * @return
 */
char** sjef_project_backend_parameters(const char* project, const char* backend, int def);
char** sjef_project_backend_names(const char* project);
// char** sjef_global_backends();
char* sjef_expand_path(const char* path, const char* default_suffix);
/*!
 * @brief Obtain a list of the names of the defined fields in a sjef::Backend
 * @return null-terminated list of pointers to malloc-allocated key names
 */
char** sjef_backend_keys();

/*!
 * @brief Obtain the value of a defined field in a backend belonging to a project
 * @param project The project.
 * @param backend The backend. If empty string or null, the default backend is assumed
 * @param key The field required
 * @return malloc-allocated value. If the backend does not exist, or if key is not valid, a null pointer is returned.
 */
char* sjef_backend_value(const char* project, const char* backend, const char* key);

char* sjef_project_backend_parameter_documentation(const char* project, const char* backend, const char* parameter);

/*!
 * @brief Obtain the path of a run directory
 * @param project The project
 * @param run
 * - 0: the most recent run directory
 * - other: the specified run directory
 * @return the fully-qualified name of the directory
 */
char* sjef_project_run_directory(const char* project, int run);
static inline char* sjef_project_run_directory_default(const char* project) {
  return sjef_project_run_directory(project, 0);
}
/*!
 * @brief Delete a run directory
 * @param project The project
 * @param run
 */
void sjef_project_run_delete(const char* project, int run);
/*!
 * @brief Copy files from a run directory to the main project.
 * @param project The project
 * @param run Specifies the run to use as source, with 0 meaning the most recent.
 * @param fromname The file to copy.
 * @param toname The destination, defaulting to fromname.
 */
void sjef_project_take_run_files(const char* project, int run, const char* fromname, const char* toname);
/*!
 * @brief Set the focussed run directory
 * @param project
 * @param run The index of an existing run directory, a positive integer, or zero, indicating that the focus is on the
 * most recent run directory
 */
void sjef_project_set_current_run(const char* project, unsigned int run);
/*!
 * @brief Get the focussed run directory
 * @param project
 * @return
 */
unsigned int sjef_project_current_run(const char* project);
#ifdef __cplusplus
}
#endif // __cplusplus

#endif // SJEF_LIB_SJEF_C_H_
