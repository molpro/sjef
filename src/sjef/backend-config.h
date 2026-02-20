#ifndef SJEF_BACKEND_CONFIG_H
#define SJEF_BACKEND_CONFIG_H
#include <map>
#include <string>
#include "sjef-backend.h"

namespace sjef {
    void set_backend_config_file_suffix(const std::string &suffix);

    std::string backend_config_file_suffix();

    void write_backend_config_file(const std::map<std::string, Backend> &backends, const std::string &project_suffix, std::string config_file_suffix = "");

    std::map<std::string, Backend> read_backend_config_file(const std::string &project_suffix, std::string config_file_suffix = "");

    std::string sync_backend_config_file(const std::string &project_suffix);
}
#endif //SJEF_BACKEND_CONFIG_H
