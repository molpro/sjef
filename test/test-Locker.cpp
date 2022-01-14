#include <boost/process.hpp>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sstream>
#include <thread>
#include <sstream>

#include "Locker.h"
namespace fs = std::filesystem;

TEST(Locker, Locker) {
  fs::path lockfile{"testing-lockfile"};
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
  sjef::Locker locker(lockfile);
  {
    auto l1 = locker.bolt();
    //    EXPECT_TRUE(fs::exists(lockfile));
    auto second_bolt = locker.bolt();
  }
  //  EXPECT_TRUE(fs::exists(lockfile));
  //  EXPECT_EQ(fs::file_size(lockfile), 0);
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
}

TEST(Locker, directory) {
  fs::path lockfile{"testing-lockfile-directory"};
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
  fs::create_directories(lockfile);
  sjef::Locker locker(lockfile);
  {
    auto l1 = locker.bolt();
    //    EXPECT_TRUE(fs::exists(lockfile / sjef::Interprocess_lock::directory_lock_file));
  }
  //  EXPECT_TRUE(fs::exists(lockfile / sjef::Interprocess_lock::directory_lock_file));
  //  EXPECT_EQ(fs::file_size(lockfile / sjef::Interprocess_lock::directory_lock_file), 0);
  fs::remove_all(lockfile);
}

TEST(Locker, no_permission) {
  fs::path lockdir{"no_permission_dir"};
  std::ofstream(lockdir.string()) << "content" << std::endl;
  fs::path lockfile{lockdir / "file"};
  EXPECT_ANY_THROW(sjef::Locker locker(lockfile); locker.bolt());
  EXPECT_THROW(sjef::Locker locker(lockfile); locker.bolt(), std::runtime_error);
  EXPECT_FALSE(fs::exists(lockfile));
  if (fs::exists(lockdir))
    fs::remove_all(lockdir);
}

TEST(Locker, write_many_threads) {
  std::ostringstream ss;
  ss << std::this_thread::get_id();
  ss << getpid();
  fs::path lockfile{ss.str()+"testing-lockfile-write_many_threads"};
  fs::path datafile{ss.str()+"testing-data-write_many_threads"};
  if (fs::exists(datafile))
    fs::remove_all(datafile);
  if (fs::exists(lockfile))
    fs::remove_all(lockfile);
  sjef::Locker locker(lockfile);
  //  { auto toucher = std::ofstream(lockfile); }
  ASSERT_TRUE(fs::exists(lockfile));
  int n{100};
  std::vector<std::string> messages;
  messages.reserve(n);
  for (auto i = 0; i < n; ++i)
    messages.emplace_back(std::to_string(i));
  std::vector<std::thread> threads;
  threads.reserve(messages.size());
  auto writer = [&locker](const std::string& data, const std::string& message) {
    //    std::cout << "thread about to lock "<<std::this_thread::get_id()<<std::endl;
    auto bolt = locker.bolt();
    //    std::cout << "thread locked "<<std::this_thread::get_id()<<std::endl;
    std::ofstream(data, std::ios_base::app) << message << std::endl;
    //    std::cout << "first message written "<<std::this_thread::get_id()<<std::endl;
    auto second_bolt = locker.bolt();
    //    std::cout << "second bolt placed "<<std::this_thread::get_id()<<std::endl;
    auto third_bolt = locker.bolt();
    auto duration = std::stoi(message) % 5;
    std::this_thread::sleep_for(std::chrono::milliseconds(duration));
    //    std::cout << "awake "<<std::this_thread::get_id()<<std::endl;
    std::ofstream(data, std::ios_base::app) << message << std::endl;
    //    std::cout << "second message written "<<std::this_thread::get_id()<<std::endl;
    //    std::cout << "thread finishing " <<std::this_thread::get_id()<< std::endl;
  };
  for (const auto& message : messages)
    threads.emplace_back(writer, datafile.string(), message);
  for (auto& thread : threads)
    thread.join();
  ASSERT_TRUE(fs::exists(datafile));
  //  std::cerr << std::ifstream(datafile).rdbuf()<<std::endl;
  for (auto i = 0; i < n; ++i) {
    auto l = locker.bolt();
    int lines = 0;
    for (auto s = std::ifstream(datafile); s; ++lines) {
      std::string line;
      std::string line2;
      std::getline(s, line);
      if (line.empty())
        --lines;
      else {
        std::getline(s, line2);
        EXPECT_EQ(line, line2);
        //        std::cout << "line: " << line << std::endl;
      }
    }
    //    std::cout << lines << " lines" << std::endl;
    EXPECT_EQ(lines, threads.size());
  }
  //  EXPECT_TRUE(fs::exists(lockfile));
  fs::remove_all(lockfile);
  fs::remove_all(datafile);
}

TEST(Locker, thread_common_mutex) {
  std::string lockfile{"thread_common_mutex.lock"};
  sjef::Locker locker(lockfile);
  int flag = 0;
  //  std::cout << "master: sets flag=0 " << std::this_thread::get_id() << std::endl;

  auto func = [&flag, &locker]() {
    //    std::cout << "slave " << std::this_thread::get_id() << std::endl;
    auto bolt = locker.bolt();
    flag = 1;
    //    std::cout << "slave: gets control " << flag << std::endl;
  };

  std::thread slave;
  {
    auto bolt = locker.bolt();
    //    std::cout << "master has locked " << std::endl;
    slave = std::thread(func);
    std::this_thread::sleep_for(std::chrono::duration(std::chrono::milliseconds(1)));
    flag = 0;
    //    std::cout << "master: about to release lock " << flag << std::endl;
  }
  slave.join();
  EXPECT_EQ(flag, 1);
  fs::remove(lockfile);
}

// TODO test interprocess locking
TEST(Locker, Interprocess) {
  namespace bp = ::boost::process;
  const std::string logfile = "Interprocess.log";
  std::filesystem::remove(logfile);
  std::filesystem::remove(logfile + ".lock");
  std::vector<bp::child> processes;
  for (int i = 0; i < 50; ++i) {
    processes.emplace_back("./logger", std::vector<std::string>{logfile, "1"});
  }
  for (auto& p : processes)
    p.wait();
//  std::cout << std::ifstream(logfile).rdbuf() << std::endl;
  auto is = std::ifstream(logfile);
  std::string line;
  while (std::getline(is, line)) {
    std::string line2;
    std::getline(is, line2);
    auto pid1 = std::stoi(line.substr(line.find_last_of(" ") + 1));
    auto pid2 = std::stoi(line2.substr(line2.find_last_of(" ") + 1));
    EXPECT_EQ(pid1, pid2) << std::ifstream(logfile).rdbuf() << std::endl;
  }
  std::filesystem::remove(logfile);
  std::filesystem::remove(logfile + ".lock");
}
