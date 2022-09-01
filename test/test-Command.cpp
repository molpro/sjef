#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <limits.h>
#include <unistd.h>
#include <filesystem>
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif

#include "util/Command.h"
namespace fs = std::filesystem;

TEST(Command, local) {
  sjef::util::Command comm;
  for (int i = 0; i < 1; ++i)
    EXPECT_EQ(comm("pwd"), fs::current_path().string());
  EXPECT_EQ(comm.out(), fs::current_path().string());
  EXPECT_EQ(comm.job_number(), 0);
}

TEST(Command, remote) {
  char hostname[HOST_NAME_MAX];
  gethostname(hostname, HOST_NAME_MAX);
  sjef::util::Command comm(hostname);
  for (int i = 0; i < 2; ++i)
    EXPECT_EQ(comm("cd " + fs::current_path().string() + ";pwd"), fs::current_path().string());
  for (int i = 0; i < 2; ++i)
    EXPECT_EQ(comm("pwd", true, fs::current_path().string()), fs::current_path().string());
  EXPECT_EQ(comm.job_number(), 0);
}

TEST(Command, local_asynchronous) {
  const std::string testfile{"testfile"};
  sjef::util::Command comm;
  comm("pwd", false);
  EXPECT_NE(comm.job_number(), 0)<<"Output stream:\n"<<comm.out()<<std::endl<<"Error stream:\n"<<comm.err()<<std::endl;
  comm.wait();
  EXPECT_FALSE(comm.running());
}

TEST(Command, remote_asynchronous) {
  char hostname[HOST_NAME_MAX];
  gethostname(hostname, HOST_NAME_MAX);
  sjef::util::Command comm(hostname);
  fs::path testdir{fs::current_path()/"test directory"};
  fs::path testfile{testdir/"test_remote_asynchronous"};

  fs::create_directory(testdir);
  if (fs::exists(testfile))
    fs::remove(testfile);
  comm("sleep 0; touch \"" +  testfile.string()+"\"", false, ".", 0);
  EXPECT_NE(comm.job_number(), 0);
  comm.wait();
  EXPECT_FALSE(comm.running());
  EXPECT_TRUE(fs::exists(testfile));
  if (fs::exists(testfile))
    fs::remove(testfile);

  if (fs::exists(testfile))
    fs::remove(testfile);
  comm("sleep 0; touch " + fs::relative(testfile,testdir).string(), false, testdir.string(), 0);
  EXPECT_NE(comm.job_number(), 0);
  comm.wait();
  EXPECT_FALSE(comm.running());
  EXPECT_TRUE(fs::exists(testfile));
  if (fs::exists(testfile))
    fs::remove(testfile);
  fs::remove(testdir);
}
