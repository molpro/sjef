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
  std::vector<std::filesystem::path> testfiles;
  const std::string m_default_suffix;
  std::vector<std::string> m_suffixes;
  std::vector<std::unique_ptr<sjef::Locker>> m_lockers;
  std::set<std::filesystem::path, std::less<>> m_not_preexisting;

public:
  explicit savestate(const std::vector<std::string>& suffixes = {})
      : m_default_suffix(::testing::UnitTest::GetInstance()->current_test_info()->name()), m_suffixes(suffixes) {
    const auto sympath = std::filesystem::path{"~"} / ".sjef";
    fs::create_directories(sjef::expand_path(sympath));
    m_suffixes.push_back(m_default_suffix);
    for (const auto& suffix : m_suffixes) {
      const auto path = sjef::expand_path(sympath / suffix);
      if (!fs::exists(path))
        m_not_preexisting.insert(path);
      m_lockers.emplace_back(std::make_unique<sjef::Locker>(path.string() + ".lock"));
      m_lockers.back()->add_bolt();
      auto path_ = path;
      path_+=".save";
      if (fs::exists(path) && !fs::exists(path_)) {
        fs::rename(path, path_);
      }
      if (!fs::exists(path) && !fs::create_directories(path))
        throw std::runtime_error(std::string{"Creating directory "} + path.string() + " has failed");
    }

    auto path = fs::absolute(fs::current_path().parent_path() / "lib").string() + path_environment_separator +
                std::getenv("PATH");
    setenv("PATH", path.c_str(), 1);
  }
  explicit savestate(const std::string& suffix) : savestate(std::vector<std::string>{{suffix}}) {}
  savestate(const savestate&) = delete;
  ~savestate() {
    for (const auto& file : testfiles)
      fs::remove_all(file);
    for (const auto& suffix : m_suffixes) {
      const auto sympath = std::filesystem::path{"~"} / ".sjef"/suffix;
      auto path = sjef::expand_path(sympath);
      auto path_ = path;
      path_.append(".save");
      if (m_not_preexisting.count(path) != 0) {
        fs::remove_all(path);
      } else if (fs::exists(path_)) {
        fs::remove_all(path);
        fs::rename(path_, path);
      }
    }
    for (auto& l : m_lockers) {
      const auto path = l->path();
      l->remove_bolt();
      l.reset(nullptr);
    }
  }
  const std::string& suffix() const { return m_default_suffix; }
  std::filesystem::path testproject(const std::string& file) { return testfile(std::filesystem::path{file + "." + m_default_suffix}); }
  std::filesystem::path testfile(const char* file) { return testfile(std::string{file}); }
  std::filesystem::path testfile(const fs::path& file) { return testfile(file.string()); }
  std::filesystem::path testfile(const std::string& file) {
    testfiles.push_back(sjef::expand_path(file));
    fs::remove_all(testfiles.back());
    return testfiles.back();
  }
};

#endif // SJEF_TEST_TEST_SJEF_H_
