#include "sjef-backend.h"
#include <sstream>

std::string sjef::Backend::default_name = "local";
std::string sjef::Backend::dummy_name = "__dummy";

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

extern "C" char** sjef_backend_keys() {
  char** result = (char**) malloc((sjef::Backend::s_keys.size() + 1) * sizeof(char*));
  size_t offset = 0;
  for (const auto& key : sjef::Backend::s_keys)
    result[offset++] = strdup(key.c_str());
  result[offset] = nullptr;
  return result;
}

