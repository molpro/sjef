#include "sjef-backend.h"
#include "sjef.h"
#include "util/Locker.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <pugixml.hpp>
#include <regex>
namespace fs = std::filesystem;
///> @private
struct sjef::pugi_xml_document : public pugi::xml_document {};

// Functions for sjef that allow it to recognize stuff that occurs in particular applications
std::string sjef::Project::input_from_output(bool sync) const {
  std::string result;
  sjef::pugi_xml_document outxml;
  outxml.load_string(xml().c_str());

  if (m_project_suffix == "molpro") { // look for Molpro input in Molpro output
    for (const auto& node : outxml.select_nodes("/molpro/job/input/p"))
      result += std::string{node.node().child_value()} + "\n";
    while (result.back() == '\n')
      result.pop_back();
  }

  return result;
}

sjef::status sjef::Project::status_from_output() const {
  if (m_project_suffix == "molpro") {
    sjef::pugi_xml_document outxml;
    outxml.load_string(xml().c_str());
    for (const auto& node : outxml.select_nodes("//error"))
      return sjef::status::failed;
  }
  return status();
}

int sjef::Project::local_pid_from_output() const {
  if (m_project_suffix == "molpro") {
    sjef::pugi_xml_document outxml;
    outxml.load_string(xml().c_str());
    for (const auto& node : outxml.select_nodes("//platform"))
      return node.node().attribute("pid").as_int();
  }
  return -1;
}

std::string sjef::Project::referenced_file_contents(const std::string& line) const {
  // TODO full robust implementation for Molpro geometry= and include
  auto pos = line.find("geometry=");
  if ((pos != std::string::npos) && (line[pos + 9] != '{') && (line.find_last_not_of(" \n\r\t") > 8)) {
    auto fn = filename("", line.substr(pos + 9));
    std::ifstream s2(fn);
    auto g = std::string(std::istreambuf_iterator<char>(s2), std::istreambuf_iterator<char>());
    if (not g.empty()) {
      g.erase(g.end() - 1, g.end());
      return g;
    }
  }
  return line;
}

void sjef::Project::rewrite_input_file(const std::string& input_file_name, const std::string& old_name) {}
void sjef::Project::custom_initialisation() {
  if (m_project_suffix == "molpro") {
    auto molprorc = filename("rc", "molpro");
    auto lockfile=molprorc; lockfile.replace_filename(".molpro.rc.lock");
    sjef::util::Locker source_lock(lockfile);
    auto lock = source_lock.bolt();
    if (not std::filesystem::exists(molprorc)) {
      std::ofstream s(molprorc);
      s << "--xml-output --no-backup";
    }
  }
}

void sjef::Project::custom_run_preface() {
  if (m_project_suffix == "molpro") {
    m_run_directory_ignore.insert(name() + ".pqb");
  }
}

sjef::Backend sjef::Project::default_backend() {
  if (m_project_suffix == "molpro") {
    return Backend(Backend::local(),"local", "localhost", "${PWD}",
           "molpro {-n %n!MPI size} {-M %M!Total memory} {-m %m!Process memory} {-G %G!GA memory}"
           );
  } else
    return Backend(Backend::local(),"local");
}
