#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "util/Command.h"
namespace fs = std::filesystem;

TEST(Command, local) {
  for (int i=0; i<2; ++i)
  {
  sjef::util::Command comm;
//  auto result = comm("pwd", true,".",1);
//  std::cout << "result:\n"<<result<<std::endl;
//  std::cout << fs::current_path().string() << std::endl;
  EXPECT_EQ(comm("pwd"),fs::current_path().string());
  }
//  EXPECT_EQ(comm("pwd"),fs::current_path().string());
}
