#include "sjef-backend.h"

#include <iostream>
#include <mutex>
#include <sstream>

std::string sjef::Backend::default_name = "local";
std::string sjef::Backend::dummy_name = "__dummy";
const std::vector<std::string> sjef::Backend::s_keys = std::vector<std::string>{
    // clang-format off
    "name",
    "host",
    "cache",
    "run_command",
    "run_jobnumber",
    "status_command",
    "status_running",
    "status_waiting",
    "kill_command"
    // clang-format on
};

std::string sjef::Backend::str() const {
    std::stringstream ss;
    ss << "sjef backend " << name << ": "
            << " host=\"" << host << "\""
            << " cache=\"" << cache << "\""
            << " run_command=\"" << run_command << "\""
            << " status_command=\"" << status_command << "\""
            << " kill_command=\"" << kill_command << "\"";
    return ss.str();
}

const std::vector<std::string> &sjef::Backend::keys() { return s_keys; }

extern "C" char **sjef_backend_keys() {
    char **result = (char **) malloc((sjef::Backend::s_keys.size() + 1) * sizeof(char *));
    size_t offset = 0;
    for (const auto &key: sjef::Backend::s_keys)
        result[offset++] = strdup(key.c_str());
    result[offset] = nullptr;
    return result;
}

bool sjef::operator==(const Backend &lhs, const Backend &rhs) {
    // std::cout << "operator=(Backend,Backend)\n"<<lhs.str()<<"\n"<<rhs.str()<<"\n"<<(lhs.str()==rhs.str())<<std::endl;
    return lhs.str() == rhs.str();
}
