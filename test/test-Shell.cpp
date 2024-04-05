#include <filesystem>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <limits.h>
#ifndef WIN32
#include <unistd.h>
#endif
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
      EXPECT_THROW(comm("echo hello"), sjef::util::Shell::runtime_error); // TODO this should throw an exception
      EXPECT_THROW(comm("echo hello", false, ".", 0, outfile), sjef::util::Shell::runtime_error);
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
#ifndef WIN32
    gethostname(hostname, HOST_NAME_MAX);
#else
    // test is not called on Windows as cannot ssh to hostname, comment out call to gethostname to avoid linking error
    strcpy(hostname, "localhost");
#endif
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

TEST(Shell, bad_command) {
  for (const auto& command : std::vector<std::string>{"@", "no_such_command", "ssh nobody@nowhere pwd"}) {
    sjef::util::Shell comm;
    bool caught{false};
    try {
      comm(command);
    } catch (const sjef::util::Shell::runtime_error& e) {
      caught = true;
      //          std::cout << e.what() << std::endl;
    }
    EXPECT_TRUE(caught);
  }
}

TEST(Shell, no_shell) {
#ifndef WIN32
  sjef::util::Shell comm("localhost", "");
  comm("ls '-d' .", true, ".", 0);
  EXPECT_EQ(comm.out(), ".");
//  std::cout << comm.out() << std::endl;
  comm("touch 'one two three'", true, ".", 0);
  EXPECT_EQ(comm.out(), "");
  comm("ls 'one two three'", true, ".", 0);
  EXPECT_EQ(comm.out(), "one two three");
  comm("rm -f 'one two three'", true, ".", 0);
#endif
}

TEST(Shell, tokenise) {
  std::map<std::string, std::vector<std::string>> tests;
  tests["a b c"] = {"a", "b", "c"};
  tests["'a b' c"] = {"a b", "c"};
  tests["\"a b\" c"] = {"a b", "c"};
  tests["\"ab\" c"] = {"ab", "c"};
  tests["\"ab\"c"] = {"abc"};
  tests["\"ab\"cd"] = {"abcd"};
  tests["a\"b\"cd"] = {"abcd"};
  for (const auto& [str, tokens] : tests) {
    EXPECT_EQ(sjef::util::tokenise(str), tokens);
  }
}
