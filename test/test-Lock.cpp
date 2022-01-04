#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thread>

#include "Lock.h"
namespace fs = std::filesystem;

TEST(Lock, simple) {
  fs::path lockfile{"testing-lockfile"};
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
  {
    sjef::Lock l1(lockfile);
    EXPECT_TRUE(fs::exists(lockfile));
  }
  EXPECT_TRUE(fs::exists(lockfile));
  EXPECT_EQ(fs::file_size(lockfile), 0);
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
}

TEST(Lock, directory) {
  fs::path lockfile{"testing-lockfile-directory"};
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
  fs::create_directories(lockfile);
  {
    sjef::Lock l1(lockfile);
    EXPECT_TRUE(fs::exists(lockfile / sjef::Lock::directory_lock_file));
  }
  EXPECT_TRUE(fs::exists(lockfile / sjef::Lock::directory_lock_file));
  EXPECT_EQ(fs::file_size(lockfile / sjef::Lock::directory_lock_file), 0);
  fs::remove_all(lockfile);
}

TEST(Lock, no_permission) {
  fs::path lockfile{"/unlikely-directory/testing-lockfile"};
  std::ofstream(lockfile) << "hello";
  EXPECT_ANY_THROW(sjef::Lock l1(lockfile));
  EXPECT_THROW(sjef::Lock l1(lockfile), std::runtime_error);
  EXPECT_FALSE(fs::exists(lockfile));
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
}

TEST(Lock, many_write_threads) {
  fs::path lockfile{"testing-lockfile"};
  fs::path datafile{"testing-data"};
  if (fs::exists(datafile))
    fs::remove_all(datafile);
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
  //  { auto toucher = std::ofstream(lockfile); }
  auto l1 = std::make_unique<sjef::Lock>(lockfile);
  ASSERT_TRUE(fs::exists(lockfile));
  int n{100};
  std::vector<std::string> messages;
  messages.reserve(n);
  for (auto i = 0; i < n; ++i)
    messages.emplace_back(std::to_string(i));
  std::vector<std::thread> threads;
  threads.reserve(messages.size());
  auto writer = [](const std::string& path, const std::string& data, const std::string& message) {
    auto lock = sjef::Lock(path);
    std::ofstream(data, std::ios_base::app) << message << std::endl;
  };
  for (const auto& message : messages)
    threads.emplace_back(writer, lockfile, datafile, message);
  l1.reset(nullptr);
  for (auto& thread : threads)
    thread.join();
  ASSERT_TRUE(fs::exists(datafile));
  //  std::cerr << std::ifstream(datafile).rdbuf()<<std::endl;
  for (auto i = 0; i < n; ++i) {
    auto l = sjef::Lock(lockfile);
    std::string line;
    int lines = 0;
    for (auto s = std::ifstream(datafile); s; ++lines) {
      std::getline(s, line);
      if (line.empty())
        --lines;
      //      else std::cout << "line: " << line <<std::endl;
    }
    //    std::cout << lines << " lines" << std::endl;
    EXPECT_EQ(lines, threads.size());
  }
  EXPECT_TRUE(fs::exists(lockfile));
  fs::remove_all(lockfile);
  fs::remove_all(datafile);
}

// TODO test interprocess locking
