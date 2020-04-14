#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <boost/filesystem.hpp>
#include <thread>

#include "FileLock.h"
namespace fs = boost::filesystem;

TEST(FileLock, simple) {
  std::string lockfile{"testing-lockfile"};
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
  { sjef::FileLock l1(lockfile); }
  system((std::string{"ls -l "}+lockfile).c_str());
  ASSERT_FALSE(fs::exists(lockfile));
  sjef::FileLock l2(lockfile);
  ASSERT_TRUE(fs::exists(lockfile));
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
}

void locker(std::string path, bool exclusive) {
  sjef::FileLock(path,exclusive);
}
TEST(FileLock, thread) {
  std::string lockfile{"testing-lockfile"};
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
  auto l1 = std::make_unique<sjef::FileLock>(lockfile,true);
  ASSERT_TRUE(fs::exists(lockfile));
  auto thread = std::thread(locker, lockfile, true);
  l1.reset();
  thread.join();
  ASSERT_FALSE(fs::exists(lockfile));
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
}

