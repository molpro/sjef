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
  {
    sjef::FileLock l1(lockfile);
    EXPECT_TRUE(fs::exists(lockfile));
  }
  EXPECT_FALSE(fs::exists(lockfile));
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
}

TEST(FileLock, many_write_threads) {
  std::string lockfile{"testing-lockfile"};
//  if (fs::exists(lockfile))
//    fs::remove_all(lockfile);
  { auto toucher = fs::ofstream(lockfile); }
  auto l1 = std::make_unique<sjef::FileLock>(lockfile, true);
  ASSERT_TRUE(fs::exists(lockfile));
  int n{1000};
  std::vector<std::string> messages;
  messages.reserve(n);
  for (auto i = 0; i < n; ++i)
    messages.emplace_back(std::to_string(i));
  std::vector<std::thread> threads;
  threads.reserve(messages.size());
  auto writer = [](const std::string& path, const std::string& message) {
    auto lock = sjef::FileLock(path, true);
    fs::ofstream(path, std::ios_base::app) << message << std::endl;
  };
  for (const auto& message : messages)
    threads.emplace_back(writer, lockfile, message);
  l1.reset();
  for (auto& thread : threads)
    thread.join();
  ASSERT_TRUE(fs::exists(lockfile));
  for (auto i = 0; i < n; ++i) {
    auto l = sjef::FileLock(lockfile, false);
    std::string line;
    int lines = 0;
    for (auto s = fs::ifstream(lockfile); s; ++lines) {
      std::getline(s, line);
      if (line.empty()) --lines;
//      else std::cout << "line: " << line <<std::endl;
    }
//    std::cout << lines << " lines" << std::endl;
    EXPECT_EQ(lines, threads.size());
  }
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
}

TEST(FileLock, repeated) {
  std::string lockfile{"testing-lockfile"};
//  if (fs::exists(lockfile))
//    fs::remove_all(lockfile);
  { auto toucher = fs::ofstream(lockfile); }
  {
    auto l1 = std::make_unique<sjef::FileLock>(lockfile, true);
    ASSERT_TRUE(fs::exists(lockfile));
    auto l2 = std::make_unique<sjef::FileLock>(lockfile, true);
    auto l3 = std::make_unique<sjef::FileLock>(lockfile, false);
  }
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
}


TEST(FileLock, promote) {
  std::string lockfile{"testing-lockfile"};
//  if (fs::exists(lockfile))
//    fs::remove_all(lockfile);
  { auto toucher = fs::ofstream(lockfile); }
  std::thread t;
  {
    auto l1 = std::make_unique<sjef::FileLock>(lockfile, false);
    auto l2 = std::make_unique<sjef::FileLock>(lockfile, true);
    auto l3 = std::make_unique<sjef::FileLock>(lockfile, false);
    auto writer = [](const std::string& path, const std::string& message) {
      auto lock = sjef::FileLock(path, true);
      fs::ofstream(path, std::ios_base::app) << message << std::endl;
    };
    t = std::thread(writer,lockfile,"hello");
  }
  t.join();
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
}

