#include <pugixml.hpp>
#include <fstream>
#include <boost/algorithm/string.hpp>
#include "sjef.h"
#include "sjef-backend.h"
///> @private
struct sjef::pugi_xml_document : public pugi::xml_document {};

// Functions for sjef that allow it to recognize stuff that occurs in particular applications
std::string sjef::Project::input_from_output() const {
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

std::string sjef::Project::referenced_file_contents(const std::string& line) const {
  //TODO implementation for Molpro geometry= and include
  return "";
}

void sjef::Project::rewrite_input_file(const std::string& input_file_name, const std::string& old_name) {
  if (m_project_suffix == "molpro") {
    std::ifstream in(input_file_name);
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    boost::replace_all(contents, "geometry=" + old_name + ".xyz", "geometry=" + this->name() + ".xyz");
    std::ofstream out(input_file_name);
    out << contents;
  }
}
