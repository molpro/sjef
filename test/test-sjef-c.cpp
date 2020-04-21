#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sjef.h"
#include "sjef-backend.h"
#include "sjef-c.h"
#include <map>
#include <list>
#include <unistd.h>
#include <libgen.h>
#include <boost/filesystem.hpp>
#include <boost/process/search_path.hpp>

namespace fs = boost::filesystem;

class savestate {
  std::string rf;
 public:
  savestate() {
    for (const auto& suffix : std::vector<std::string>{"sjef", "molpro"}) {
      rf = sjef::expand_path(std::string{"~/."} + suffix + "/projects");
      if (!fs::exists(rf)) rf.clear();
      if (!rf.empty()) {
        fs::rename(rf, rf + ".save");
//      std::cerr << "savestate saves " << rf << std::endl;
      }
    }
  }
  ~savestate() {
    for (const auto& suffix : std::vector<std::string>{"sjef", "molpro"}) {
      rf = sjef::expand_path(std::string{"~/."} + suffix + "/projects");
      if (!rf.empty() and fs::exists(rf + ".save")) {
//      std::cerr << "savestate restores " << rf << std::endl;
        fs::rename(rf + ".save", rf);
      }
    }
  }

};

TEST(project, c_binding) {
  savestate x;
  char projectname[] = "$TMPDIR/test-sjef-project/cproject.sjef";
  char projectname2[] = "$TMPDIR/test-sjef-project/cproject2.sjef";
  sjef_project_erase(projectname);
  sjef_project_erase(projectname2);
  char key[] = "testkey";
  char value[] = "testvalue";
  char value2[] = "testvalue2";
  sjef_project_open(projectname);
  sjef_project_property_set(projectname, key, value);
  ASSERT_EQ(std::string{value}, std::string{sjef_project_property_get(projectname, key)});
  sjef_project_property_set(projectname, key, value2);
  ASSERT_EQ(std::string{value2}, std::string{sjef_project_property_get(projectname, key)});
  sjef_project_property_delete(projectname, key);
  ASSERT_EQ(std::string{}, std::string{sjef_project_property_get(projectname, key)});
  ASSERT_EQ(std::string{}, std::string{sjef_project_property_get(projectname, "unknown key")});
  sjef_project_property_set(projectname, key, value);
  sjef_project_copy(projectname, projectname2, 0);
  ASSERT_EQ(std::string{value}, std::string{sjef_project_property_get(projectname, key)});
  sjef_project_open(projectname2);
  ASSERT_EQ(std::string{value}, std::string{sjef_project_property_get(projectname2, key)});
  sjef_project_close(projectname2);
  sjef_project_erase(projectname2);
  ASSERT_EQ(sjef_project_move(projectname, projectname2), 1);
  sjef_project_open(projectname);
  ASSERT_EQ(std::string{}, std::string{sjef_project_property_get(projectname, key)});
  sjef_project_close(projectname);
  ASSERT_EQ(std::string{value}, std::string{sjef_project_property_get(projectname2, key)});
  sjef_project_close(projectname2);
  sjef_project_erase(projectname);
  sjef_project_erase(projectname2);
}

TEST(backend, C_keys) {
  auto allKeys = sjef_backend_keys();
  ASSERT_NE(allKeys, nullptr);
  size_t i;
  for (i = 0; allKeys[i] != nullptr; ++i) {
//    std::cout << allKeys[i] << std::endl;
    free(allKeys[i]);
  }
  EXPECT_EQ(i, 9);
  free(allKeys);
}
TEST(project, C_quick_destroy) {
  char projname[] = "C_project.molpro";
  sjef_project_open(projname);
  sjef_project_close(projname);
}

TEST(backend, C_values) {//TODO actually implement some of this for C
  char projname[] = "C_project.molpro";
  sjef_project_open(projname);
  auto allBackends = sjef_project_backend_names(projname);
//  std::cerr << "back from making allBackends"<<std::endl;
//char** allBackends = NULL;
  EXPECT_THROW(sjef_backend_value(projname,"!*@£junk", "name"), std::runtime_error);
  sjef_project_close(projname);
  if (false) {

  sjef::Project p("Cpp_project.molpro");
  auto allBackendsCpp = p.backend_names();
  bool localFound = false;
  size_t i;
  for (i = 0; allBackends[i] != nullptr; ++i) {
    EXPECT_EQ(std::string{allBackends[i]},allBackendsCpp[i]);
    std::cout << allBackends[i] << std::endl;
    EXPECT_THROW(sjef_backend_value(projname,allBackends[i], "!*@£junk"), std::runtime_error);
    ASSERT_NE(sjef_backend_value(projname,allBackends[i], "name"), nullptr);
    localFound = localFound or std::string{sjef_backend_value(projname,allBackends[i], "name")} == "local";
    EXPECT_EQ(std::string{sjef_backend_value(projname,allBackends[i], "name")}, std::string{allBackends[i]});
    free(allBackends[i]);
  }
  EXPECT_EQ(i, allBackendsCpp.size());
  EXPECT_TRUE(localFound);
  ASSERT_NE(sjef_backend_value(projname,"", "name"), nullptr);
  EXPECT_EQ(std::string{sjef_backend_value(projname,"", "name")}, "local");
  ASSERT_NE(sjef_backend_value(projname,nullptr, "name"), nullptr);
  EXPECT_EQ(std::string{sjef_backend_value(projname,nullptr, "name")}, "local");
  free(allBackends);
  }
}

