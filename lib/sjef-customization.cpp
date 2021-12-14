#include "FileLock.h"
#include "sjef-backend.h"
#include "sjef.h"
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <iostream>
#include <pugixml.hpp>
#include <regex>
namespace fs = boost::filesystem;
///> @private
struct sjef::pugi_xml_document : public pugi::xml_document {};

// Functions for sjef that allow it to recognize stuff that occurs in particular applications
std::string sjef::Project::input_from_output(bool sync) const {
  std::string result;
  sjef::pugi_xml_document outxml;
  outxml.load_string(xml(sync).c_str());

  if (m_project_suffix == "molpro") { // look for Molpro input in Molpro output
    for (const auto& node : outxml.select_nodes("/molpro/job/input/p"))
      result += std::string{node.node().child_value()} + "\n";
    while (result.back() == '\n')
      result.pop_back();
  }

  return result;
}

std::string sjef::Project::referenced_file_contents(const std::string& line) const {
  // TODO full robust implementation for Molpro geometry= and include
  auto pos = line.find("geometry=");
  if ((pos != std::string::npos) && (line[pos + 9] != '{')) {
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
    auto lockfile = std::regex_replace(molprorc, std::regex{"molpro.rc"}, ".molpro.rc.lock");
    sjef::FileLock source_lock(lockfile, true, true);
    if (not boost::filesystem::exists(molprorc)) {
      std::ofstream s(molprorc);
      s << "--xml-output --no-backup" << std::endl;
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
    return Backend("local", "localhost", "${PWD}",
                   "molpro {-n %n!MPI size} {-M %M!Total memory} {-m %m!Process memory} {-G %G!GA memory}");
  } else
    return Backend("local");
}
