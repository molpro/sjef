#include <pugixml.hpp>
#include <fstream>
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <boost/filesystem.hpp>
#include "sjef.h"
#include "sjef-backend.h"
#include "FileLock.h"
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
  //TODO full robust implementation for Molpro geometry= and include
  auto pos = line.find("geometry=");
  if ((pos != std::string::npos) && (line[pos + 9] != '{')) {
    auto fn = filename("", line.substr(pos + 9));
    std::ifstream s2(fn);
    auto g = std::string(std::istreambuf_iterator<char>(s2),
                         std::istreambuf_iterator<char>());
    if (not g.empty()) {
      g.erase(g.end() - 1, g.end());
      return g;
    }
  }
  return line;
}

void sjef::Project::rewrite_input_file(const std::string& input_file_name, const std::string& old_name) {
  if (m_project_suffix == "molpro") {
    constexpr bool debug = false;
    std::ifstream in(input_file_name);
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (debug)
      std::cerr << "rewrite_input_file(" << input_file_name << ", " << old_name << ") original contents:\n" << contents
                << std::endl;
    boost::replace_all(contents, "geometry=" + old_name + ".xyz", "geometry=" + this->name() + ".xyz");
    if (debug)
      std::cerr << "rewrite_input_file(" << input_file_name << ", " << old_name << ") final contents:\n" << contents
                << std::endl;
    std::ofstream out(input_file_name);
    out << contents;
  }
}
void sjef::Project::custom_initialisation() {
  if (m_project_suffix == "molpro") {
    sjef::FileLock source_lock(".molpro.rc.lock", true, true);
    if (not boost::filesystem::exists("molpro.rc")) {
      std::ofstream s("molpro.rc");
      s << "--xml-output --no-backup" << std::endl;
    }
  }
}

void sjef::Project::custom_run_preface() {
  if (m_project_suffix == "molpro") {
    { // manage backups
      constexpr int default_max_backups = 3;
      bool needed = false;
      for (const auto& suffix : std::vector<std::string>{"out", "xml", "log"})
        needed = needed or fs::exists(filename(suffix));
      if (needed) {
        auto max_backupss = property_get("output_backups");
        int max_backups = max_backupss.empty() ? default_max_backups : std::stoi(max_backupss);
        property_set("output_backups", std::to_string(max_backups));
        auto backup = fs::path{filename("", "backup")};
        fs::create_directories(backup);
        auto ss = property_get("last_output_backup");
        int seq = ss.empty() ? 1 : std::stoi(ss) + 1;
        property_set("last_output_backup", std::to_string(seq));
        auto backup_dir = backup / std::to_string(seq);
        fs::create_directories(backup_dir);
        for (const auto& suffix : std::vector<std::string>{"out", "xml", "log"})
          if (fs::exists(filename(suffix)))
            fs::rename(filename(suffix), backup_dir / fs::path(filename(suffix)).filename());
        for (int old = seq - max_backups; old > 0; --old)
          if (fs::exists(backup / std::to_string(old)))
            fs::remove_all(backup / std::to_string(old));
      }
    }
  }
}

sjef::Backend sjef::Project::default_backend() {
  if (m_project_suffix == "molpro") {
    return Backend("local",
                   "localhost",
                   "${PWD}",
                   "molpro {-n %n!MPI size} {-M %M!Total memory} {-m %m!Process memory} {-G %G!GA memory}"
    );
  } else
    return Backend("local");
}
