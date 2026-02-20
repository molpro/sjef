#include "backend-config.h"
#include <string>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>

#include "sjef-backend.h"
#include "sjef.h"
#include <pugixml.hpp>
#include <yaml.h>

#include "util/Locker.h"
namespace fs = std::filesystem;

namespace sjef {
    struct pugi_xml_document : public pugi::xml_document {
    };

    static std::string s_default_suffix{"xml"};
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

    fs::path backend_config_file_path(const std::string &project_suffix, std::string config_file_suffix = "") {
        if (config_file_suffix == "") config_file_suffix = backend_config_file_suffix();
        return sjef_config_directory() / project_suffix / ("backends." + config_file_suffix);
    }

    void write_backend_config_file(const std::map<std::string, Backend> &backends, const std::string &project_suffix,
                                   std::string config_file_suffix) {
        if (config_file_suffix == "") config_file_suffix = backend_config_file_suffix();
        auto config_file = backend_config_file_path(project_suffix, config_file_suffix);
        util::Locker locker{fs::path{config_file}.parent_path()};
        auto locki = locker.bolt();
        auto stream = std::ofstream(config_file);
        if (config_file_suffix == "xml") {
            stream << R"(<?xml version="1.0" encoding="UTF-8"?>)" << std::endl;
            stream << "<backends>" << std::endl;
            for (const auto &[name, backend]: backends) {
                stream << "  <backend name=\"" + name + "\" ";
                if (backend.host != "")
                    stream << "\n           host=\"" + backend.host + "\" ";
                if (backend.run_command != "")
                    stream << "\n           run_command=\"" + backend.run_command + "\" ";
                if (backend.run_jobnumber != "")
                    stream << "\n           run_jobnumber=\"" + backend.run_jobnumber + "\" ";
                if (backend.status_command != "")
                    stream << "\n           status_command=\"" + backend.status_command +
                            "\" ";
                if (backend.status_waiting != "")
                    stream << "\n           status_waiting=\"" + backend.status_waiting +
                            "\" ";
                if (backend.status_running != "")
                    stream << "\n           status_running=\"" + backend.status_running +
                            "\" ";
                if (backend.kill_command != "")
                    stream << "\n           kill_command=\"" + backend.kill_command + "\" ";
                stream << "\n  />" << std::endl;
            }
            stream << "</backends>" << std::endl;
        } else if (config_file_suffix == "yaml") {
            for (const auto &[name, backend]: backends) {
                stream << name << ":" << std::endl;
                if (backend.host != "") stream << "  host: " << backend.host << std::endl;
                if (backend.run_command != "")
                    stream << "  run_command: >" << std::endl << "    " << backend.run_command <<
                            std::endl;
                if (backend.cache != "") stream << "  cache: " << backend.cache << std::endl;
                if (backend.run_jobnumber != "")
                    stream << "  run_jobnumber: Submitted batch job *([0-9]+)" << std::endl;
                if (backend.status_command != "") stream << "  status_command: " << backend.status_command << std::endl;
                if (backend.status_running != "")
                    stream << "  status_running:  " << backend.status_running << std::endl;
                if (backend.status_waiting != "")
                    stream << "  status_waiting:  " << backend.status_waiting << std::endl;
                if (backend.kill_command != "") stream << "  kill_command: " << backend.kill_command << std::endl;
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

    std::map<std::string, Backend> read_backend_config_file(const std::string &project_suffix,
                                                            std::string config_file_suffix) {
        if (config_file_suffix == "") config_file_suffix = backend_config_file_suffix();
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
            auto fh = fopen(backend_config_file_path(project_suffix, config_file_suffix).c_str(), "rb");
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
        if (result.find(sjef::Backend::default_name) == result.end()) {
            result.emplace(sjef::Backend::default_name, Backend::local());
            write_backend_config_file(result, project_suffix);
        }
        return result;
    }

    // Source - https://stackoverflow.com/a/37575457
    // Posted by mtrw, modified by community. See post 'Timeline' for change history
    // Retrieved 2026-02-19, License - CC BY-SA 3.0


    bool inline compareFiles(const std::string &p1, const std::string &p2) {
        std::ifstream f1(p1, std::ifstream::binary | std::ifstream::ate);
        std::ifstream f2(p2, std::ifstream::binary | std::ifstream::ate);

        if (f1.fail() || f2.fail()) {
            return false; //file problem
        }

        if (f1.tellg() != f2.tellg()) {
            return false; //size mismatch
        }

        //seek back to beginning and use std::equal to compare contents
        f1.seekg(0, std::ifstream::beg);
        f2.seekg(0, std::ifstream::beg);
        return std::equal(std::istreambuf_iterator<char>(f1.rdbuf()),
                          std::istreambuf_iterator<char>(),
                          std::istreambuf_iterator<char>(f2.rdbuf()));
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

        if (compareFiles(config_files[preferred_config_file_suffix],
                         config_files[unpreferred_config_file_suffix]))
            return "";

        if (!fs::exists(config_files[unpreferred_config_file_suffix]) ||
            (
                fs::exists(config_files[unpreferred_config_file_suffix]) &&
                fs::exists(config_files[preferred_config_file_suffix]) &&
                fs::last_write_time(config_files[unpreferred_config_file_suffix]) < fs::last_write_time(
                    config_files[preferred_config_file_suffix])
            )) {
            write_backend_config_file(read_backend_config_file(project_suffix, preferred_config_file_suffix),
                                      project_suffix, unpreferred_config_file_suffix);
            return unpreferred_config_file_suffix;
        }
        write_backend_config_file(read_backend_config_file(project_suffix, unpreferred_config_file_suffix),
                                  project_suffix, preferred_config_file_suffix);
        return preferred_config_file_suffix;
    }
}
