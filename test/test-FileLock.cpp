#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <filesystem>
#include <thread>
#include <fstream>

#include "FileLock.h"
namespace fs = std::filesystem;

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
  fs::path lockfile{"testing-lockfile"};
  //  if (fs::exists(lockfile))
  //    fs::remove_all(lockfile);
  { auto toucher = std::ofstream(lockfile); }
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
    std::ofstream(path, std::ios_base::app) << message << std::endl;
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
    for (auto s = std::ifstream(lockfile); s; ++lines) {
      std::getline(s, line);
      if (line.empty())
        --lines;
      //      else std::cout << "line: " << line <<std::endl;
    }
    //    std::cout << lines << " lines" << std::endl;
    EXPECT_EQ(lines, threads.size());
  }
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
}
