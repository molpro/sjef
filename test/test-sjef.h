#ifndef SJEF_TEST_TEST_SJEF_H_
#define SJEF_TEST_TEST_SJEF_H_
#include <set>
#include <sjef/sjef.h>
#include <sjef/util/Locker.h>
#include <stdlib.h>
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

class test_sjef : public ::testing::Test {
protected:
  std::string rf;
  std::vector<std::filesystem::path> testfiles;
  std::string m_default_suffix;
  std::vector<std::string> m_suffixes;
  fs::path m_dot_sjef;
  std::vector<std::unique_ptr<sjef::util::Locker>> m_lockers;
  std::set<std::filesystem::path, std::less<>> m_not_preexisting;

protected:
  void _SetUp(const std::vector<std::string>& suffixes = {}) {
    m_default_suffix = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    m_suffixes = suffixes;
    std::string tmp="/tmp";
    if (std::getenv("TMPDIR") != nullptr) tmp = std::getenv("TMPDIR");
    if (std::getenv("TEMP") != nullptr) tmp = std::getenv("TEMP");
    if (std::getenv("TMP") != nullptr) tmp = std::getenv("TMP");
    m_dot_sjef = "/tmp/test_sjef_config";
    fs::create_directories(sjef::expand_path(m_dot_sjef));
    setenv("SJEF_CONFIG", m_dot_sjef.string().c_str(), 1);
    m_suffixes.push_back(m_default_suffix);
    for (const auto& suffix : m_suffixes) {
      const auto path = sjef::expand_path(m_dot_sjef / suffix);
      if (!fs::exists(path))
        m_not_preexisting.insert(path);
      m_lockers.emplace_back(std::make_unique<sjef::util::Locker>(path.string() + ".lock"));
      m_lockers.back()->add_bolt();
      auto path_ = path;
      path_ += ".save";
      if (fs::exists(path) && !fs::exists(path_)) {
        fs::rename(path, path_);
      }
      if (!fs::exists(path) && !fs::create_directories(path))
        throw std::runtime_error(std::string{"Creating directory "} + path.string() + " has failed");
    }

    auto path = fs::absolute(fs::current_path().parent_path() / "src" / "sjef").string() + path_environment_separator +
                std::getenv("PATH");
    setenv("PATH", path.c_str(), 1);
  }
  void SetUp() override { _SetUp({}); }
  //  explicit test_sjef(const std::string& suffix) : test_sjef(std::vector<std::string>{{suffix}}) {}
  //  test_sjef(const test_sjef&) = delete;
  virtual void TearDown() override {
    for (const auto& file : testfiles)
      fs::remove_all(file);
    for (auto& l : m_lockers) {
      const auto path = l->path();
      l->remove_bolt();
      l.reset(nullptr);
    }
    fs::remove_all(m_dot_sjef);
  }
  const std::string& suffix() const { return m_default_suffix; }
  std::filesystem::path testproject(const std::string& file) {
    return testfile(std::filesystem::path{file + "." + m_default_suffix});
  }
  std::filesystem::path testfile(const char* file) { return testfile(std::string{file}); }
  std::filesystem::path testfile(const fs::path& file) { return testfile(file.string()); }
  std::filesystem::path testfile(const std::string& file) {
    testfiles.push_back(sjef::expand_path(file));
    fs::remove_all(testfiles.back());
    return testfiles.back();
  }
};

#endif // SJEF_TEST_TEST_SJEF_H_
