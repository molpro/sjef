#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <limits.h>
#include <unistd.h>
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif

#include "util/Command.h"
namespace fs = std::filesystem;

TEST(Command, local) {
  sjef::util::Command comm;
  for (int i = 0; i < 2; ++i)
    EXPECT_EQ(comm("pwd"), fs::current_path().string());
  EXPECT_EQ(comm.out(), fs::current_path().string());
}

TEST(Command, remote) {
  char hostname[HOST_NAME_MAX];
  gethostname(hostname, HOST_NAME_MAX);
  sjef::util::Command comm(hostname);
  for (int i = 0; i < 2; ++i)
    EXPECT_EQ(comm("cd " + fs::current_path().string() + ";pwd"), fs::current_path().string());
  for (int i = 0; i < 2; ++i)
    EXPECT_EQ(comm("pwd", true, fs::current_path().string()), fs::current_path().string());
}

TEST(Command, local_asynchronous) {
  sjef::util::Command comm;
  comm("pwd", false);
  EXPECT_NE(comm.job_number(), 0);
  comm.wait();
  EXPECT_FALSE(comm.running());
}

TEST(Command, remote_asynchronous) {
  char hostname[HOST_NAME_MAX];
  gethostname(hostname, HOST_NAME_MAX);
  sjef::util::Command comm(hostname);
  std::string testfile{"test_remote_asynchronous"};

  if (fs::exists(testfile))
    fs::remove(testfile);
  comm("sleep 0; touch " + fs::current_path().string() + "/" + testfile, false, ".", 0);
  EXPECT_NE(comm.job_number(), 0);
  comm.wait();
  EXPECT_FALSE(comm.running());
  EXPECT_TRUE(fs::exists(testfile));
  if (fs::exists(testfile))
    fs::remove(testfile);

  if (fs::exists(testfile))
    fs::remove(testfile);
  comm("sleep 0; touch " + testfile, false, fs::current_path().string(), 0);
  EXPECT_NE(comm.job_number(), 0);
  comm.wait();
  EXPECT_FALSE(comm.running());
  EXPECT_TRUE(fs::exists(testfile));
  if (fs::exists(testfile))
    fs::remove(testfile);
}
