#include "backend-config.h"
#include <string>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <mutex>

#include "sjef-backend.h"
#include "sjef.h"
#include <pugixml.hpp>
#include <yaml.h>

#include "util/Locker.h"

#include <regex>
namespace fs = std::filesystem;

namespace sjef {
    struct pugi_xml_document : public pugi::xml_document {
    };

    static std::string s_default_suffix{"yaml"};
    static std::string s_file_suffix{s_default_suffix};

    void set_backend_config_file_suffix(const std::string &suffix) {
        if (suffix != "xml" && suffix != "yaml") throw std::invalid_argument("Invalid suffix");
        s_file_suffix = suffix;
    }

    std::string backend_config_file_suffix() {
        return s_file_suffix;
    }

    inline fs::path sjef_config_directory() {
        return fs::path(expand_path(getenv("SJEF_CONFIG") == nullptr ? "~/.sjef" : getenv("SJEF_CONFIG")));
    }

    fs::path backend_config_file_path(const std::string &project_suffix, std::string config_file_suffix) {
        if (config_file_suffix == "") config_file_suffix = backend_config_file_suffix();
        return sjef_config_directory() / project_suffix / ("backends." + config_file_suffix);
    }

    void save_backend_config(const std::map<std::string, Backend> &backends, const std::string &project_suffix) {
     write_backend_config_file(backends, project_suffix, backend_config_file_suffix());
        sync_backend_config_file(project_suffix);
        ensure_local_backend(project_suffix);
        sync_backend_config_file(project_suffix);
    }

    inline std::string xml_escape_attribute(const std::string& value) {
        std::string result;
        result.reserve(value.size());
        for (char c: value) {
            switch (c) {
                case '&': result += "&amp;"; break;
                case '<': result += "&lt;"; break;
                case '>': result += "&gt;"; break;
                case '"': result += "&quot;"; break;
                default: result += c;
            }
        }
        return result;
    }

    inline std::string yaml1(const std::string& key, const std::string& value, bool multiline=false) {
        std::string result{"  "};
        result += key + ": ";
        if (multiline) {
            result += ">\n    " + value;
        } else {
            std::string escaped;
            escaped.reserve(value.size());
            for (char c: value) {
                if (c == '\'') escaped += "''";
                else escaped += c;
            }
            result += "'" + escaped + "'";
        }
        // std::cout << "yaml1 "<<key<<", "<<value<<"::: "<<result<<std::endl;
        return result;

    }

    void write_backend_config_file(const std::map<std::string, Backend> &backends, const std::string &project_suffix,
                                   std::string config_file_suffix) {
        if (config_file_suffix == "") config_file_suffix = backend_config_file_suffix();
        auto config_file = backend_config_file_path(project_suffix, config_file_suffix);
        util::Locker locker{fs::path{config_file}.parent_path()};
        auto locki = locker.bolt();
        auto stream = std::ofstream(config_file);
        auto backends_ = backends;
        if ( backends_.find(Backend::default_name) == backends_.end()) {
            // std::cout << "writing default backend " << Backend::default_name << std::endl;
            backends_.emplace(Backend::default_name, Backend::local());
        }
        // std::cout << "backends to be written for suffix "<<config_file_suffix<<":";
        // for (const auto &[name, backend]: backends_) std::cout << " " << name;
        // std::cout << std::endl;
        if (config_file_suffix == "xml") {
            stream << R"(<?xml version="1.0" encoding="UTF-8"?>)" << std::endl;
            stream << "<backends>" << std::endl;
            for (const auto &[name, backend]: backends_) {
                // std::cout << "writing backend " << name << std::endl;
                stream << "  <backend name=\"" + xml_escape_attribute(name) + "\" ";
                if (backend.host != "")
                    stream << "\n           host=\"" + xml_escape_attribute(backend.host) + "\" ";
                if (backend.run_command != "")
                    stream << "\n           run_command=\"" + xml_escape_attribute(backend.run_command) + "\" ";
                if (backend.run_jobnumber != "")
                    stream << "\n           run_jobnumber=\"" + xml_escape_attribute(backend.run_jobnumber) + "\" ";
                if (backend.status_command != "")
                    stream << "\n           status_command=\"" + xml_escape_attribute(backend.status_command) +
                            "\" ";
                if (backend.status_waiting != "")
                    stream << "\n           status_waiting=\"" + xml_escape_attribute(backend.status_waiting) +
                            "\" ";
                if (backend.status_running != "")
                    stream << "\n           status_running=\"" + xml_escape_attribute(backend.status_running) +
                            "\" ";
                if (backend.kill_command != "")
                    stream << "\n           kill_command=\"" + xml_escape_attribute(backend.kill_command) + "\" ";
                stream << "\n  />" << std::endl;
            }
            stream << "</backends>" << std::endl;
        } else if (config_file_suffix == "yaml") {
            for (const auto &[name, backend]: backends_) {
                stream << name << ":" << std::endl;
                if (backend.host != "") stream << yaml1("host" , backend.host) << std::endl;
                if (backend.run_command != "")
                    stream << yaml1("run_command" ,backend.run_command, true) <<
                            std::endl;
                if (backend.cache != "") stream << yaml1("cache" , backend.cache) << std::endl;
                if (backend.run_jobnumber != "")
                    stream << yaml1("run_jobnumber",backend.run_jobnumber) << std::endl;
                if (backend.status_command != "") stream << yaml1("status_command" , backend.status_command) << std::endl;
                if (backend.status_running != "")
                    stream << yaml1("status_running" , backend.status_running) << std::endl;
                if (backend.status_waiting != "")
                    stream << yaml1("status_waiting" , backend.status_waiting) << std::endl;
                if (backend.kill_command != "") stream << yaml1("kill_command" , backend.kill_command) << std::endl;
                stream << std::endl;
            }
        } else throw std::invalid_argument("Invalid suffix");
    }

    inline std::string getattribute(pugi::xpath_node node, const std::string &name) {
        return node.node().attribute(name.c_str()).value();
    }

    ///> @private
    inline bool localhost(const std::string_view &host) {
        return (host.empty() || host == "localhost");
    }

    namespace {
        struct BackendConfigStat {
            bool xml_exists = false;
            fs::file_time_type xml_mtime{};
            bool yaml_exists = false;
            fs::file_time_type yaml_mtime{};
            bool operator==(const BackendConfigStat &other) const {
                return xml_exists == other.xml_exists && xml_mtime == other.xml_mtime &&
                       yaml_exists == other.yaml_exists && yaml_mtime == other.yaml_mtime;
            }
        };
        struct BackendConfigCacheEntry {
            BackendConfigStat stat;
            std::map<std::string, Backend> backends;
        };
        BackendConfigStat stat_backend_config_files(const std::string &project_suffix) {
            BackendConfigStat result;
            if (auto path = backend_config_file_path(project_suffix, "xml"); fs::exists(path)) {
                result.xml_exists = true;
                result.xml_mtime = fs::last_write_time(path);
            }
            if (auto path = backend_config_file_path(project_suffix, "yaml"); fs::exists(path)) {
                result.yaml_exists = true;
                result.yaml_mtime = fs::last_write_time(path);
            }
            return result;
        }
        std::mutex s_backend_config_cache_mutex;
        std::map<std::string, BackendConfigCacheEntry> s_backend_config_cache;
    } // namespace

    std::map<std::string, Backend> load_backend_config(const std::string &project_suffix) {
        // sync_backend_config_file()/ensure_local_backend() each read and parse both the xml and
        // yaml config files (to compare/reconcile them, and to repair older malformed entries),
        // and the whole chain below runs on every call. That's real, unconditional file I/O and
        // parsing for data that's static for the lifetime of a process, in the common case where
        // nothing about the backend configuration has changed -- which matters here because
        // load_backend_config() runs on every single Project construction, and a script working
        // through a large Database constructs one Project per molecule. Skip straight to a cached
        // result while the config files' existence and modification times haven't changed since
        // it was built; the check itself is cheap (stat, not read+parse), and correctly detects a
        // change made from another process (e.g. iMolpro editing backend settings concurrently),
        // not just from this one.
        auto stat = stat_backend_config_files(project_suffix);
        {
            std::lock_guard<std::mutex> lock(s_backend_config_cache_mutex);
            if (auto it = s_backend_config_cache.find(project_suffix);
                it != s_backend_config_cache.end() && it->second.stat == stat)
                return it->second.backends;
        }
        sync_backend_config_file(project_suffix);
        ensure_local_backend(project_suffix);
        sync_backend_config_file(project_suffix);
        auto result = read_backend_config_file(project_suffix, backend_config_file_suffix());
        // The calls above may themselves have written the config files (to sync or repair them),
        // so re-stat rather than reusing the stat taken before them.
        auto new_stat = stat_backend_config_files(project_suffix);
        {
            std::lock_guard<std::mutex> lock(s_backend_config_cache_mutex);
            s_backend_config_cache[project_suffix] = {new_stat, result};
        }
        return result;
    }
    std::map<std::string, Backend> read_backend_config_file(const std::string &project_suffix,
                                                            std::string config_file_suffix) {
        if (config_file_suffix == "") config_file_suffix = backend_config_file_suffix();
        // std::cout << "read_backend_config_file suffix "<<config_file_suffix<<std::endl;
        std::map<std::string, Backend> result;
        if (config_file_suffix == "xml") {
            pugi_xml_document backend_doc;
            try {
                auto path = backend_config_file_path(project_suffix, config_file_suffix);
                backend_doc.load_file(path.c_str());
                auto backends = backend_doc.select_nodes("/backends/backend");
                for (const auto &be: backends) {
                    auto kName = getattribute(be, "name");
                    if (auto kHost = getattribute(be, "host"); localhost(kHost))
                        result.try_emplace(kName, Backend::local(), kName);
                    else
                        result.try_emplace(kName, Backend::Linux(), kName);
                    if (const auto kVal = getattribute(be, "host"); kVal != "")
                        result[kName].host = kVal;
                    if (const auto kVal = getattribute(be, "cache"); kVal != "")
                        result[kName].cache = kVal;
                    if (const auto kVal = getattribute(be, "run_command"); kVal != "")
                        result[kName].run_command = kVal;
                    if (const auto kVal = getattribute(be, "run_jobnumber"); kVal != "")
                        result[kName].run_jobnumber = kVal;
                    if (const auto kVal = getattribute(be, "status_command"); kVal != "")
                        result[kName].status_command = kVal;
                    if (const auto kVal = getattribute(be, "status_running"); kVal != "")
                        result[kName].status_running = kVal;
                    if (const auto kVal = getattribute(be, "status_waiting"); kVal != "")
                        result[kName].status_waiting = kVal;
                    if (const auto kVal = getattribute(be, "kill_command"); kVal != "")
                        result[kName].kill_command = kVal;
                }
            } catch (...) {
            }
        } else if (config_file_suffix == "yaml") {
            auto fh = fopen(backend_config_file_path(project_suffix, config_file_suffix).string().c_str(), "rb");
            if (fh != NULL) {
                yaml_parser_t parser;
                if (!yaml_parser_initialize(&parser))
                    throw std::runtime_error("Failed to initialize parser");
                yaml_parser_set_input_file(&parser, fh);

                yaml_token_t token;
                int level = 0;
                std::string key, value, backend_key;
                bool key_selected = false;
                do {
                    yaml_parser_scan(&parser, &token);
                    switch (token.type) {
                        /* Stream start/end */
                        case YAML_STREAM_START_TOKEN:
                            break;
                        case YAML_STREAM_END_TOKEN:
                            break;
                        case YAML_KEY_TOKEN:
                            key_selected = true;
                            break;
                        case YAML_VALUE_TOKEN:
                            key_selected = false;
                            break;
                        case YAML_BLOCK_SEQUENCE_START_TOKEN:
                            break;
                        case YAML_BLOCK_ENTRY_TOKEN:
                            break;
                        case YAML_BLOCK_END_TOKEN:
                            level--;
                            break;
                        case YAML_BLOCK_MAPPING_START_TOKEN:
                            break;
                        case YAML_SCALAR_TOKEN:
                            if (key_selected) {
                                key = (char *) token.data.scalar.value;
                                if (level == 0) {
                                    backend_key = key;
                                    result.try_emplace(key, Backend::local(), key);
                                    // TODO improvable for linux defaults?
                                    level++;
                                }
                            } else {
                                value = (char *) token.data.scalar.value;
                                if (level == 0) {
                                    throw std::runtime_error("Invalid backend config file");
                                } else {
                                    if (!value.empty() && value.back() == '\n') value.pop_back();
                                    if (key == "host") result[backend_key].host = value;
                                    if (key == "cache") result[backend_key].cache = value;
                                    if (key == "run_command") result[backend_key].run_command = value;
                                    if (key == "run_jobnumber") result[backend_key].run_jobnumber = value;
                                    if (key == "status_command") result[backend_key].status_command = value;
                                    if (key == "status_running") result[backend_key].status_running = value;
                                    if (key == "status_waiting") result[backend_key].status_waiting = value;
                                    if (key == "kill_command") result[backend_key].kill_command = value;
                                }
                            }
                        default:
                            ;
                    }
                    if (token.type != YAML_STREAM_END_TOKEN)
                        yaml_token_delete(&token);
                } while (token.type != YAML_STREAM_END_TOKEN);
                yaml_token_delete(&token);
                yaml_parser_delete(&parser);
                fclose(fh);
            }
        } else
            throw std::invalid_argument("Invalid suffix");

        return result;
    }
    void ensure_local_backend(const std::string& project_suffix, std::string config_file_suffix) {
        auto backends = read_backend_config_file(project_suffix, config_file_suffix);
        if (backends.find(sjef::Backend::default_name) == backends.end()) {
            backends.emplace(sjef::Backend::default_name, sjef::default_backend(project_suffix));
            write_backend_config_file(backends, project_suffix);
        }
        for (auto &[name, backend] : backends) { // repair config files from earlier mistake
          if ((std::regex_match(backend.run_command, std::regex("^[a-zA-Z0-9_/'._-]*/molpro.*$")) or
              std::regex_match(backend.run_command, std::regex("^molpro.*$")))
               and std::regex_match(backend.run_jobnumber, std::regex("Submitted.*"))) {

          // std::cout << "repairing " << name << std::endl;
            backend.run_jobnumber = "([0-9]+)";
            write_backend_config_file(backends, project_suffix);
          }
        }
    }
    std::string sync_backend_config_file(const std::string &project_suffix) {
        std::map<std::string, fs::path> config_files;


        auto preferred_config_file_suffix = backend_config_file_suffix();
        std::string unpreferred_config_file_suffix;
        for (auto config_file_suffix: {"xml", "yaml"})
            if (config_file_suffix != preferred_config_file_suffix)
                unpreferred_config_file_suffix = config_file_suffix;

        for (auto config_file_suffix: {"xml", "yaml"}) {
            config_files[config_file_suffix] = backend_config_file_path(project_suffix, config_file_suffix);
        }

            if (read_backend_config_file(project_suffix, preferred_config_file_suffix) ==read_backend_config_file(project_suffix, unpreferred_config_file_suffix))
            return "";

        if (!fs::exists(config_files[unpreferred_config_file_suffix]) ||
            (
                fs::exists(config_files[unpreferred_config_file_suffix]) &&
                fs::exists(config_files[preferred_config_file_suffix]) &&
                fs::last_write_time(config_files[unpreferred_config_file_suffix]) < fs::last_write_time(
                    config_files[preferred_config_file_suffix])
            )) {
            // std::cout << "taking preferred_config_file_suffix "<<preferred_config_file_suffix<<", unpreferred existence"<<fs::exists(config_files[unpreferred_config_file_suffix])<<std::endl;
            write_backend_config_file(read_backend_config_file(project_suffix, preferred_config_file_suffix),
                                      project_suffix, unpreferred_config_file_suffix);
            return unpreferred_config_file_suffix;
        }
        write_backend_config_file(read_backend_config_file(project_suffix, unpreferred_config_file_suffix),
                                  project_suffix, preferred_config_file_suffix);
        return preferred_config_file_suffix;
    }


}
