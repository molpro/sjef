#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "sjef-backend.h"
#include "sjef-c.h"
#include "sjef.h"
#include "test-sjef.h"
#include <filesystem>
#include <libgen.h>
#include <list>
#include <map>
#include <unistd.h>

namespace fs = std::filesystem;



TEST(project, c_binding) {
  savestate x;
  const char* projectname = strdup(x.testproject("cproject").c_str());
  const char* projectname2 = strdup(x.testproject("cproject2").c_str());
  //  sjef_project_erase(projectname);
  //  sjef_project_erase(projectname2);
  char key[] = "testkey";
  char value[] = "testvalue";
  char value2[] = "testvalue2";
  sjef_project_open(projectname);
  //  if (system((std::string{"ls -ltraR "} + projectname).c_str())) {
  //  }
  //  if (system((std::string{"cat "} + projectname+"/Info.plist").c_str())) {
  //  }
  //  std::cout << "before sjef_project_property_set"<<std::endl;
  sjef_project_property_set(projectname, key, value);
  //  if (system((std::string{"ls -ltraR "} + projectname).c_str())) {
  //  }
  //  if (system((std::string{"cat "} + projectname+"/Info.plist").c_str())) {
  //  }
  ASSERT_EQ(std::string{value}, std::string{sjef_project_property_get(projectname, key)});
  sjef_project_property_set(projectname, key, value2);
  ASSERT_EQ(std::string{value2}, std::string{sjef_project_property_get(projectname, key)});
  sjef_project_property_delete(projectname, key);
  ASSERT_EQ(std::string{}, std::string{sjef_project_property_get(projectname, key)});
  ASSERT_EQ(std::string{}, std::string{sjef_project_property_get(projectname, "unknown key")});
  sjef_project_property_set(projectname, key, value);
  sjef_project_copy(projectname, projectname2, 0);
  //  std::cout << "after copy, from="<<projectname<<std::endl;
  //  if (system((std::string{"ls -ltraR "} + projectname).c_str())) { }
  //  if (system((std::string{"cat "} + projectname+"/Info.plist").c_str())) {}
  //  std::cout << "after copy, to="<<projectname2<<std::endl;
  //  if (system((std::string{"ls -ltraR "} + projectname2).c_str())) { }
  //    if (system((std::string{"cat "} + projectname2+"/Info.plist").c_str())) { }
  ASSERT_EQ(std::string{value}, std::string{sjef_project_property_get(projectname, key)});
  //  if (system((std::string{"cat "} + projectname2+"/Info.plist").c_str())) { }
  sjef_project_open(projectname2);
  //  std::cout << "after open"<<std::endl;
  //  if (system((std::string{"cat "} + projectname2+"/Info.plist").c_str())) { }
  //  sleep(1);
  ASSERT_EQ(std::string{value}, std::string{sjef_project_property_get(projectname2, key)});
  sjef_project_close(projectname2);
  sjef_project_erase(projectname2);
  sjef_project_close(projectname);
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
  savestate state;
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
  savestate state;
  const char* projname = strdup(state.testproject("C_project").c_str());
  sjef_project_open(projname);
  sjef_project_close(projname);
}

TEST(backend, C_values) { // TODO actually implement some of this for C
  savestate state;
  const char* projname = strdup(state.testproject("C_project").c_str());
  fs::remove_all(projname);
  sjef_project_open(projname);
  EXPECT_THROW(sjef_backend_value(projname, "!*@£junk", "name"), std::runtime_error);
  sjef_project_close(projname);
  EXPECT_THAT(std::string{sjef_project_recent(1, state.suffix().c_str())}, ::testing::HasSubstr(std::string{"C_project."}+state.suffix()));
  EXPECT_EQ(sjef_project_recent_find(sjef_project_recent(1, state.suffix().c_str())), 1);
  auto allBackends = sjef_project_backend_names(projname);
  //  std::cerr << "back from making allBackends"<<std::endl;
  // char** allBackends = NULL;
  if (false) {

    sjef::Project p("Cpp_project.molpro");
    auto allBackendsCpp = p.backend_names();
    bool localFound = false;
    size_t i;
    for (i = 0; allBackends[i] != nullptr; ++i) {
      EXPECT_EQ(std::string{allBackends[i]}, allBackendsCpp[i]);
      std::cout << allBackends[i] << std::endl;
      EXPECT_THROW(sjef_backend_value(projname, allBackends[i], "!*@£junk"), std::runtime_error);
      ASSERT_NE(sjef_backend_value(projname, allBackends[i], "name"), nullptr);
      localFound = localFound or std::string{sjef_backend_value(projname, allBackends[i], "name")} == "local";
      EXPECT_EQ(std::string{sjef_backend_value(projname, allBackends[i], "name")}, std::string{allBackends[i]});
      free(allBackends[i]);
    }
    EXPECT_EQ(i, allBackendsCpp.size());
    EXPECT_TRUE(localFound);
    ASSERT_NE(sjef_backend_value(projname, "", "name"), nullptr);
    EXPECT_EQ(std::string{sjef_backend_value(projname, "", "name")}, "local");
    ASSERT_NE(sjef_backend_value(projname, nullptr, "name"), nullptr);
    EXPECT_EQ(std::string{sjef_backend_value(projname, nullptr, "name")}, "local");
    free(allBackends);
  }
}
