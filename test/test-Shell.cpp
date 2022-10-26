#include <filesystem>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <limits.h>
//#include <unistd.h>
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif
#include <fstream>
#ifdef WIN32
#include <winsock.h>
#endif

#include <regex>
#include <sjef/util/Shell.h>
namespace fs = std::filesystem;

TEST(Shell, local) {
  sjef::util::Shell comm;
  auto pwd = fs::current_path().string();
#ifdef WIN32
  pwd = std::regex_replace(pwd, std::regex{"C:"}, "/c");
  pwd = std::regex_replace(pwd, std::regex{"\\\\"}, "/");
#endif
  for (int i = 0; i < 1; ++i)
    EXPECT_EQ(comm("pwd"), pwd);
  EXPECT_EQ(comm.out(), pwd);
  EXPECT_EQ(comm.job_number(), 0);
}

TEST(Shell, remote) {
#ifndef WIN32
  char hostname[HOST_NAME_MAX];
  gethostname(hostname, HOST_NAME_MAX);
  sjef::util::Shell comm(hostname);
  auto pwd = fs::current_path().string();
  pwd = std::regex_replace(pwd, std::regex{"C:"}, "/c");
  pwd = std::regex_replace(pwd, std::regex{"\\\\"}, "/");
  for (int i = 0; i < 2; ++i)
    EXPECT_EQ(comm("cd " + fs::current_path().string() + ";pwd"), pwd);
  for (int i = 0; i < 2; ++i)
    EXPECT_EQ(comm("pwd", true, fs::current_path().string()), pwd);
  EXPECT_EQ(comm.job_number(), 0);
#endif
}

TEST(Shell, local_asynchronous) {
  if (sjef::util::Shell::local_asynchronous_supported()) {
    const std::string testfile{"testfile"};
    sjef::util::Shell comm;
    comm("pwd", false);
    EXPECT_NE(comm.job_number(), 0) << "Output stream:\n"
                                    << comm.out() << std::endl
                                    << "Error stream:\n"
                                    << comm.err() << std::endl;
    comm.wait();
    EXPECT_FALSE(comm.running());
  }
}

TEST(Shell, bad_shell) {
  if (sjef::util::Shell::local_asynchronous_supported()) {
    const std::string outfile{"/tmp/outfile"};
    {
      sjef::util::Shell comm("localhost", "/bin/sh");
      EXPECT_EQ(comm("echo hello"), "hello");
#ifndef WIN32
      EXPECT_EQ(comm("echo hello", false, ".", 0, outfile), "");
      std::ifstream t(outfile);
      comm.wait();
      std::stringstream buffer;
      buffer << t.rdbuf();
      EXPECT_EQ(buffer.str(), "hello\n");
      sjef::util::Shell()("rm -rf " + outfile);
#endif
    }

    {
      sjef::util::Shell comm("localhost", "/bin/badshell");
      EXPECT_EQ(comm("echo hello"), ""); // TODO this should throw an exception
      //  EXPECT_ANY_THROW(comm("echo hello"));
      EXPECT_EQ(comm("echo hello", false, ".", 0, outfile), ""); // TODO this should throw an exception
      //    EXPECT_ANY_THROW(comm("echo hello",false,".",0,outfile)); // TODO this should throw an exception
      std::ifstream t(outfile);
      comm.wait();
      std::stringstream buffer;
      buffer << t.rdbuf();
      EXPECT_EQ(buffer.str(), "");
      sjef::util::Shell()("rm -rf " + outfile);
    }
  }
}

TEST(Shell, remote_asynchronous) {
  if (sjef::util::Shell::local_asynchronous_supported()) {
    char hostname[HOST_NAME_MAX];
    gethostname(hostname, HOST_NAME_MAX);
    sjef::util::Shell comm(hostname);
    fs::path testdir{fs::current_path() / "test directory"};
    fs::path testfile{testdir / "test_remote_asynchronous"};

    fs::create_directory(testdir);
    if (fs::exists(testfile))
      fs::remove(testfile);
    comm("sleep 0; touch \"" + testfile.string() + "\"", false, ".", 0);
    EXPECT_NE(comm.job_number(), 0);
    comm.wait();
    EXPECT_FALSE(comm.running());
    EXPECT_TRUE(fs::exists(testfile));
    if (fs::exists(testfile))
      fs::remove(testfile);

    if (fs::exists(testfile))
      fs::remove(testfile);
    comm("sleep 0; touch " + fs::relative(testfile, testdir).string(), false, testdir.string(), 0);
    EXPECT_NE(comm.job_number(), 0);
    comm.wait();
    EXPECT_FALSE(comm.running());
    EXPECT_TRUE(fs::exists(testfile));
    if (fs::exists(testfile))
      fs::remove(testfile);
    fs::remove(testdir);
  }
}
