#ifndef SJEF_TEST_TEST_SJEF_H_
#define SJEF_TEST_TEST_SJEF_H_
#include <string>
#include <vector>
#include <set>
#include <boost/filesystem.hpp>
#include <sjef.h>
namespace fs = boost::filesystem;

class savestate {
  std::string rf;
  std::vector<std::string> testfiles;
  std::vector<std::string> m_suffixes;
  std::set<std::string> m_not_preexisting;

public:
  explicit savestate(const std::vector<std::string> suffixes = {"sjef", "molpro", "someprogram"})
      : m_suffixes(suffixes) {
    fs::create_directories(sjef::expand_path(std::string{"~/.sjef"}));
    for (const auto& suffix : m_suffixes) {
      auto path = sjef::expand_path(std::string{"~/.sjef/"} + suffix);
      if (not fs::exists(path))
        m_not_preexisting.insert(path);
      if (fs::exists(path) and not fs::exists(path + ".save"))
        fs::rename(path, path + ".save");
      if (not fs::exists(path)) {
        if (not fs::create_directories(path))
          throw std::runtime_error(std::string{"Creating directory "} + path + " has failed");
      }
    }
  }
  explicit savestate(std::string suffix) : savestate(std::vector<std::string>{{suffix}}) {}
  ~savestate() {
    for (const auto& file : testfiles)
      fs::remove_all(file);
    for (const auto& suffix : m_suffixes) {
      auto path = sjef::expand_path(std::string{"~/.sjef/"} + suffix);
      if (m_not_preexisting.count(path) != 0) {
        //        std::cout << "removing " << path << std::endl;
        fs::remove_all(path);
      } else if (fs::exists(path + ".save")) {
        //        std::cout << "restoring " << path << std::endl;
        fs::remove_all(path);
        fs::rename(path + ".save", path);
      }
    }
  }
  std::string testfile(const char* file) { return testfile(std::string{file}); }
  std::string testfile(const fs::path& file) { return testfile(file.string()); }
  std::string testfile(const std::string& file) {
    testfiles.push_back(sjef::expand_path(file));
    fs::remove_all(testfiles.back());
    return testfiles.back();
  }
};
#endif // SJEF_TEST_TEST_SJEF_H_