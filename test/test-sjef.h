#ifndef SJEF_TEST_TEST_SJEF_H_
#define SJEF_TEST_TEST_SJEF_H_
#include <Locker.h>
#include <set>
#include <sjef.h>
#include <string>
#include <vector>
namespace fs = std::filesystem;

#ifdef WIN32
const std::string path_environment_separator = ";";
inline int setenv(const char* name, const char* value, int overwrite) {
  int errcode = 0;
  if (!overwrite) {
    size_t envsize = 0;
    errcode = getenv_s(&envsize, NULL, 0, name);
    if (errcode || envsize)
      return errcode;
  }
  return _putenv_s(name, value);
}
#else
const std::string path_environment_separator = ":";
#endif

class savestate {
  std::string rf;
  std::vector<std::string> testfiles;
  const std::string m_default_suffix;
  std::vector<std::string> m_suffixes;
  std::vector<std::unique_ptr<sjef::Locker>> m_lockers;
  std::set<std::string> m_not_preexisting;

public:
  explicit savestate(const std::vector<std::string> suffixes = {})
      : m_default_suffix(::testing::UnitTest::GetInstance()->current_test_info()->name()),
        m_suffixes(std::move(suffixes)) {
    fs::create_directories(sjef::expand_path(std::string{"~/.sjef"}));
    m_suffixes.push_back(m_default_suffix);
    //    m_suffixes.push_back("__lock__");
    for (const auto& suffix : m_suffixes) {
      auto path = sjef::expand_path(std::string{"~/.sjef/"} + suffix);
      if (not fs::exists(path))
        m_not_preexisting.insert(path);
      m_lockers.emplace_back(std::make_unique<sjef::Locker>(path + ".lock"));
      m_lockers.back()->add_bolt();
      //      std::cout << "savestate " << m_default_suffix << " acquires lock on " << m_lockers.back()->path()<< " pid
      //      "<<getpid() << std::endl;
      if (fs::exists(path) and not fs::exists(path + ".save")) {
        //        std::cout << "saving " << path << std::endl;
        fs::rename(path, path + ".save");
      }
      if (not fs::exists(path)) {
        if (not fs::create_directories(path))
          throw std::runtime_error(std::string{"Creating directory "} + path + " has failed");
      }
    }

    auto path = fs::absolute(fs::current_path().parent_path() / "lib").string() + path_environment_separator +
                std::getenv("PATH");
    //    std::cout << "PWD: " << fs::current_path() << std::endl;
    //    std::cout << "PATH: " << path << std::endl;
    setenv("PATH", path.c_str(), 1);
  }
  explicit savestate(std::string suffix) : savestate(std::vector<std::string>{{suffix}}) {}
  ~savestate() {
    for (const auto& file : testfiles)
      fs::remove_all(file);
    for (const auto& suffix : m_suffixes) {
      auto path = sjef::expand_path(std::string{"~/.sjef/"} + suffix);
      if (m_not_preexisting.count(path) != 0) {
        //                std::cout << "removing " << path << std::endl;
        fs::remove_all(path);
      } else if (fs::exists(path + ".save")) {
        //                std::cout << "restoring " << path << std::endl;
        fs::remove_all(path);
        fs::rename(path + ".save", path);
      }
    }
    for (auto& l : m_lockers) {
      const auto path = l->path();
      //      std::cout << "savestate " << m_default_suffix << " relinquishes lock on " << path << std::endl;
      l->remove_bolt();
      l.reset(nullptr);
      //      fs::remove(path);
    }
  }
  const std::string& suffix() const { return m_default_suffix; }
  std::string testproject(const std::string& file) { return testfile(file + "." + m_default_suffix); }
  std::string testfile(const char* file) { return testfile(std::string{file}); }
  std::string testfile(const fs::path& file) { return testfile(file.string()); }
  std::string testfile(const std::string& file) {
    testfiles.push_back(sjef::expand_path(file));
    fs::remove_all(testfiles.back());
    return testfiles.back();
  }
};

#endif // SJEF_TEST_TEST_SJEF_H_
