#include <pugixml.hpp>
#include <fstream>
#include <boost/algorithm/string.hpp>
#include <iostream>
#include "sjef.h"
#include "sjef-backend.h"
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
      g.erase(g.end()-1,g.end());
      return g;
    }
  }
  return line;
}

void sjef::Project::rewrite_input_file(const std::string& input_file_name, const std::string& old_name) {
  if (m_project_suffix == "molpro") {
    constexpr bool debug=false;
    std::ifstream in(input_file_name);
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (debug)
      std::cerr << "rewrite_input_file("<<input_file_name<<", "<<old_name<<") original contents:\n"<<contents <<std::endl;
    boost::replace_all(contents, "geometry=" + old_name + ".xyz", "geometry=" + this->name() + ".xyz");
    if (debug)
      std::cerr << "rewrite_input_file("<<input_file_name<<", "<<old_name<<") final contents:\n"<<contents <<std::endl;
    std::ofstream out(input_file_name);
    out << contents;
  }
}
