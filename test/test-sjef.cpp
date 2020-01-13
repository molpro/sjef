#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sjef.h"
#include "sjef-backend.h"
#include <map>
#include <list>
#include <unistd.h>
#include <libgen.h>
#include <boost/filesystem.hpp>
#include <boost/process/search_path.hpp>
#include <regex>

namespace fs = boost::filesystem;

class savestate {
  std::string rf;
 public:
  savestate() {
    for (const auto& suffix : std::vector<std::string>{"sjef", "molpro", "someprogram"}) {
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
TEST(project, filename) {
  std::string slash{boost::filesystem::path::preferred_separator};
  char* e = getenv(
#ifdef _WIN32
      "TEMP"
#else
      "TMPDIR"
#endif
  );
  std::string tmpdir{e == nullptr ? "/tmp" : e};
  std::map<std::string, std::string> names;
//  system((std::string{"ls -la "}+tmpdir).c_str());
  names["$TMPDIR/tmp.sjef/"] = tmpdir + slash + "tmp.sjef";
  names["$TMPDIR/tmp"] = tmpdir + slash + "tmp.sjef";
  for (const auto& n : names)
    ASSERT_EQ(sjef::Project(n.first, nullptr, true, false,"sjef").filename(), n.second);
}

TEST(project, expand_path) {
  std::string slash{boost::filesystem::path::preferred_separator};
  std::string cwd{boost::filesystem::current_path().string()};
  std::string home{getenv(
#ifdef _WIN32
      "USERPROFILE"
#else
      "HOME"
#endif
  )};
  std::map<std::string, std::string> names;
  names["/x/y/z"] = slash + "x" + slash + "y" + slash + "z";
  names["\\x/y\\z"] = slash + "x" + slash + "y" + slash + "z";
  names["/x.ext"] = slash + "x.ext";
  names["x/y/z"] = cwd + slash + "x" + slash + "y" + slash + "z";
  names["~/x"] = home + slash + "x";
  names["$HOME/x"] = home + slash + "x";
  names["${HOME}/x"] = home + slash + "x";
  for (const auto& n : names)
    ASSERT_EQ(sjef::expand_path(n.first), n.second);
  names.clear();
  names["/x"] = slash + "x.ext";
  names["x"] = cwd + slash + "x.ext";
  names["/x.ext1"] = slash + "x.ext1.ext";
  names["x.ext1"] = cwd + slash + "x.ext1.ext";
  for (const auto& n : names)
    ASSERT_EQ(sjef::expand_path(n.first, "ext"), n.second);
}

TEST(project, construction) {
  std::string name("sjef-project-test");
  for (const auto& dirs : std::vector<std::string>{"$TMPDIR", "$HOME", "${HOME}", "~"}) {
    std::string filename(dirs + "/" + name + ".sjef");
    sjef::Project::erase(filename); // remove any previous contents
    sjef::Project x(filename, nullptr, true);
    ASSERT_EQ(x.name(), name);
  }
}

TEST(project, move) {
  savestate x;
  std::string name("sjef-project-test");
  std::string filename("$TMPDIR/" + name + ".sjef");
  sjef::Project::erase(filename); // remove any previous contents
  sjef::Project x2(filename, nullptr, true);
  ASSERT_EQ (x2.name(), name);
  auto name2 = name + "2";
  std::string filename2("$TMPDIR/" + name2 + ".sjef");
  sjef::Project::erase(filename2); // remove any previous contents
  x2.move(filename2);
  ASSERT_EQ(x2.name(), name2);
  ASSERT_FALSE(fs::exists(fs::path(filename)));
  ASSERT_TRUE(fs::exists(fs::path(x2.filename())));
  x2.copy(filename);
  ASSERT_TRUE(fs::exists(fs::path(x2.filename())));
  ASSERT_TRUE(fs::exists(sjef::expand_path(filename2)));
  ASSERT_TRUE(fs::exists(sjef::expand_path(filename)));
  x2.move(filename, true);
  EXPECT_FALSE(fs::exists(sjef::expand_path(filename2)));
  EXPECT_TRUE(fs::exists(sjef::expand_path(filename)));
}

TEST(project, moveMolpro) {
savestate x;
std::string filename_old("moveMolproOld.molpro");
std::string filename_new("moveMolproNew.molpro");
sjef::Project p(filename_old, nullptr, true);
std::ofstream(p.filename("inp")) << "geometry="+p.name()+".xyz"+"\n";
std::ofstream(p.filename("xyz")) << "1\n\nHe 0 0 0\n";
p.move(filename_new,true);
EXPECT_FALSE(fs::exists(sjef::expand_path(filename_old)));
EXPECT_TRUE(fs::exists(sjef::expand_path(filename_new)));
std::string inp;
std::ifstream(p.filename("inp")) >> inp;
EXPECT_EQ(inp,"geometry="+p.name()+".xyz");
}

TEST(project, copyMolpro) {
  savestate x;
  std::string filename_old("copyMolproOld.molpro");
  std::string filename_new("copyMolproNew.molpro");
  {
  sjef::Project p(filename_old, nullptr, true);
  std::ofstream(p.filename("inp")) << "geometry="+p.name()+".xyz"+"\n";
  std::ofstream(p.filename("xyz")) << "1\n\nHe 0 0 0\n";
  p.copy(filename_new,true);
  }
  sjef::Project p(filename_new, nullptr, true);
  EXPECT_TRUE(fs::exists(sjef::expand_path(filename_old)));
  EXPECT_TRUE(fs::exists(sjef::expand_path(filename_new)));
  std::string inp;
  std::ifstream(p.filename("inp")) >> inp;
  EXPECT_EQ(inp,"geometry="+p.name()+".xyz");
}

TEST(project, erase) {
  std::string filename("$TMPDIR/sjef-project-test.sjef");
  sjef::Project::erase(filename); // remove any previous contents
  sjef::Project x(filename);
  filename = x.filename();
  ASSERT_TRUE(fs::exists(fs::path(filename)));
  ASSERT_TRUE(fs::exists(fs::path(filename + "/Info.plist")));
  ASSERT_TRUE(fs::is_directory(fs::path(filename)));
  x.erase();
  ASSERT_EQ(x.filename(), "");
  ASSERT_FALSE(fs::exists(fs::path(filename)));
}

TEST(project, import) {
  std::string filename("$TMPDIR/sjef-project-test.sjef");
  sjef::Project::erase(filename); // remove any previous contents
  sjef::Project x(filename);
  filename = x.filename();
  auto importfile = sjef::expand_path("$TMPDIR/sjef-project-test.import");
  boost::filesystem::ofstream ofs{importfile};
  ofs << "Hello" << std::endl;
  x.import_file(importfile);
  x.import_file(importfile, true);
}

TEST(project, clean) {
  std::string filename("$TMPDIR/sjef-project-test.sjef");
  sjef::Project::erase(filename); // remove any previous contents
  sjef::Project x(filename);
  filename = x.filename();
  ASSERT_FALSE(fs::exists(fs::path(filename) / fs::path(filename + ".out")));
  { fs::ofstream s(fs::path(filename) / fs::path(x.name() + ".out")); }
  ASSERT_TRUE(fs::exists(fs::path(filename) / fs::path(x.name() + ".out")));
  x.clean(true, true);
  ASSERT_FALSE(fs::exists(fs::path(filename) / fs::path(x.name() + ".out")));
  ASSERT_FALSE(fs::is_directory(fs::path(filename) / fs::path(x.name() + ".d")));
  fs::create_directory(fs::path(filename) / fs::path(x.name() + ".d"));
  ASSERT_TRUE(fs::is_directory(fs::path(filename) / fs::path(x.name() + ".d")));
  x.clean(true);
  ASSERT_FALSE(fs::is_directory(fs::path(filename) / fs::path(x.name() + ".d")));
}

TEST(project, properties) {
  sjef::Project::erase("$TMPDIR/try.sjef"); // remove any previous contents
  sjef::Project x("$TMPDIR/try.sjef", nullptr, true);
  x.property_rewind();
  std::string key;
  int ninitial;
  for (ninitial = 0; (key = x.property_next()) != ""; ++ninitial);
  std::map<std::string, std::string> data;
  data["first key"] = "first value";
  data["second key"] = "second value";
  data["third key"] = "third value";
  for (const auto& keyval : data)
    x.property_set(keyval.first, keyval.second);
  for (const auto& keyval : data) {
//    std::cout << "key "<<keyval.first<<" expect value: "<<keyval.second<<" actual value: "<<x.property_get(keyval.first)<<std::endl;
    ASSERT_EQ(x.property_get(keyval.first), keyval.second);
  }
  x.property_rewind();
  int n;
  for (n = 0; (key = x.property_next()) != ""; ++n) {
//    std::cout << "key "<<key<<std::endl;
    if (data.count("key") != 0)
      ASSERT_EQ(x.property_get(key), data[key]);
  }
//  std::cout << "data.size() "<<data.size()<<std::endl;
  ASSERT_EQ(n, data.size() + ninitial);
  x.property_rewind();
  for (x.property_rewind(); (key = x.property_next()) != ""; x.property_rewind()) {
//    system(("echo start deletion loop key="+key+"; cat "+x.filename()+"/Info.plist").c_str());
    x.property_delete(key);
//    system(("echo end deletion loop key="+key+"; cat "+x.filename()+"/Info.plist").c_str());
  }
  x.property_rewind();
  ASSERT_EQ(x.property_next(), "");

  ASSERT_EQ(x.property_get("vacuous"), "");
  x.property_set("empty", "");
  ASSERT_EQ(x.property_get("empty"), "");
}

TEST(project, recent_files) {
  auto rf = sjef::expand_path("~/.sjef/projects");
  auto oldfile = fs::exists(rf);
  if (oldfile)
    fs::rename(rf, rf + ".save");
  std::list<sjef::Project> p;
  for (size_t i = 0; i < 10; i++) {
    sjef::Project::erase("$TMPDIR/p" + std::to_string(i)+".someprogram"); // remove any previous contents
    p.emplace_back("$TMPDIR/p" + std::to_string(i)+".someprogram");
  }
  size_t i = p.size();
  for (const auto& pp : p)
    ASSERT_EQ(pp.recent(i--), pp.filename());
  i = p.size();
  for (const auto& pp : p)
    ASSERT_EQ(pp.recent_find(pp.filename()), i--);
  p.back().erase();
  p.pop_back();
//  system(("cat "+rf).c_str());
  i = p.size();
  for (const auto& pp : p)
    ASSERT_EQ(pp.recent(i--), pp.filename());
  i = p.size();
  for (const auto& pp : p)
    ASSERT_EQ(pp.recent(i--), pp.filename());
  for (auto& pp : p)
    pp.erase();
  for (const auto& pp : p)
    ASSERT_EQ(pp.recent_find(pp.filename()), 0);
  if (oldfile)
    fs::rename(rf + ".save", rf);
}

TEST(project, project_hash) {
  sjef::Project::erase("$TMPDIR/try.sjef"); // remove any previous contents
  sjef::Project x("$TMPDIR/try.sjef", nullptr, true);
  auto xph = x.project_hash();
  sjef::Project::erase("$TMPDIR/try2.sjef"); // remove any previous contents
  ASSERT_TRUE(x.copy("$TMPDIR/try2.sjef"));
  sjef::Project x2("$TMPDIR/try2.sjef", nullptr, true, false);
  ASSERT_NE(xph, x2.project_hash());
  sjef::Project::erase("$TMPDIR/try3.sjef"); // remove any previous contents
  x.move("$TMPDIR/try3.sjef");
  ASSERT_EQ(xph, x.project_hash());
}

TEST(project, input_hash) {
  sjef::Project::erase("$TMPDIR/try.sjef"); // remove any previous contents
  sjef::Project x("$TMPDIR/try.sjef", nullptr, true);
  {
    std::ofstream ss(x.filename("inp"));
    ss << "one\ngeometry=try.xyz\ntwo" << std::endl;
  }
  {
    std::ofstream ss(x.filename("xyz"));
    ss << "1\nThe xyz file\nHe 0 0 0" << std::endl;
  }
  auto xph = x.input_hash();
  sjef::Project::erase("$TMPDIR/try2.sjef"); // remove any previous contents
  ASSERT_TRUE(x.copy("$TMPDIR/try2.sjef"));
  sjef::Project x2("$TMPDIR/try2.sjef", nullptr, true, false);
  ASSERT_EQ(xph, x2.input_hash());
  sjef::Project::erase("$TMPDIR/try3.sjef"); // remove any previous contents
  x.move("$TMPDIR/try3.sjef");
  ASSERT_EQ(xph, x.input_hash());
}

TEST(project, xmlRepair) {
  using sjef::xmlRepair;
  EXPECT_EQ(xmlRepair("<root>some stuff</root>"), "<root>some stuff</root>");
  EXPECT_EQ(xmlRepair("<root/>"), "<root/>");
  EXPECT_EQ(xmlRepair(""), "<?xml version=\"1.0\"?><root/>");
  EXPECT_EQ(xmlRepair("<?xml version=\"1.0\"?><root></root>"), "<?xml version=\"1.0\"?><root></root>");
  EXPECT_EQ(xmlRepair("<root>some stuff"), "<root>some stuff</root>");
  EXPECT_EQ(xmlRepair("<root>some stuff<"), "<root>some stuff</root>");
  EXPECT_EQ(xmlRepair("<root>some stuff</"), "<root>some stuff</root>");
  EXPECT_EQ(xmlRepair("<root>some stuff</r"), "<root>some stuff</root>");
  EXPECT_EQ(xmlRepair("<root><sub>some stuff</"), "<root><sub>some stuff</sub></root>");
  EXPECT_EQ(xmlRepair("<root><sub attribute=\"value\">some stuff</"),
            "<root><sub attribute=\"value\">some stuff</sub></root>");
  EXPECT_EQ(xmlRepair("<orbitals>"), "<orbitals></orbitals>");
  std::map<std::string, std::string> plurals;
  plurals["orbitals"] = "<orbital a=\"b\"/>";
  EXPECT_EQ(xmlRepair("<orbitals>", plurals), "<orbitals><orbital a=\"b\"/></orbitals>");
}

TEST(project, xmloutput) {
  sjef::Project He("He.molpro");
  EXPECT_EQ(He.file_contents("xml"), He.xml());
  {
    sjef::Project newProject("test___.someprogram", nullptr, true);
    EXPECT_EQ(newProject.file_contents("xml"), "");
    EXPECT_EQ(newProject.xml(), "<?xml version=\"1.0\"?><root/>");
  }
}

TEST(project, input_from_output) {
  savestate x;
  sjef::Project He("He.molpro");
  std::string input = He.file_contents("inp");
  input = std::regex_replace(input,std::regex{" *\n\n*"},"\n");
  EXPECT_EQ(input, He.input_from_output());
  {
    He.copy("Hecopy");
    sjef::Project Hecopy("Hecopy.molpro");
    EXPECT_EQ(He.file_contents("inp"),Hecopy.file_contents("inp"));
    EXPECT_EQ(input, He.input_from_output());
    EXPECT_EQ(Hecopy.input_from_output(), "");
    Hecopy.erase();
  }

}

TEST(project, run_needed) {
  savestate x;
  sjef::Project He("He.molpro");
  EXPECT_FALSE(He.run_needed());
}

//TEST(project,input_from_output) {
//  savestate x;
//  sjef::Project p("test.sjef", nullptr,true);
//  std::string tempinp{"/tmp/test.inp"};
//  std::string inpstring ="one\n\ntwo  \n";
//  std::ofstream(tempinp) <<  inpstring;
//  p.import_file(tempinp);
//  EXPECT_TRUE(inpstring,p.)
//}

TEST(project, spawn_many) {
  sjef::Project p("spawn_many.someprogram", nullptr, true);
  { std::ofstream(p.filename("inp")) << ""; }
  std::vector<std::string> backends{sjef::Backend::dummy_name};
  if (not boost::process::search_path(sjef::Backend().run_command).empty()) // test the default backend only if it exists
    backends.insert(backends.begin(), sjef::Backend().name);
  for (const auto& backend : backends)
    for (auto i = 0; i < 100; ++i) {
      ASSERT_TRUE(p.run(backend, {}, -1, true, true));
      EXPECT_NE(p.property_get("jobnumber"), "-1");
      EXPECT_EQ(p.status(), sjef::completed);
    }

}

TEST(backend, backend_parameter_expand) {
  sjef::Project He("He.someprogram");
  std::string backend = "random_backend_name";
  for (const auto& prologue : std::vector<std::string>{"A prologue ", ""}) {
    He.backend_parameter_set(backend, "present", "123");
    He.backend_parameter_delete(backend, "missing");
    EXPECT_EQ(He.property_get("Backend/" + backend + "/missing"), "");
    EXPECT_THROW(He.backend_parameter_expand(backend, "thing {nothing to substitute} thing2"), std::runtime_error);
    EXPECT_EQ(He.backend_parameter_expand(backend, "thing {" + prologue + "%missing} thing2"), "thing  thing2");
    EXPECT_EQ(He.backend_parameter_expand(backend, "thing {" + prologue + "%missing:default value} thing2"),
              "thing " + prologue + "default value thing2");
    EXPECT_EQ(He.property_get("Backend/" + backend + "/missing"), "");

    EXPECT_EQ(He.backend_parameter_expand(backend, "thing {" + prologue + "%present} thing2"),
              "thing " + prologue + "123 thing2");
    EXPECT_EQ(He.backend_parameter_expand(backend, "thing {" + prologue + "%present:default value} thing2"),
              "thing " + prologue + "123 thing2");
  }
}

TEST(sjef, atomic) {
  sjef::Project object1("He.someprogram");
  sjef::Project object2("He.someprogram");
  std::string testval = "testval";
  object1.property_set("testprop", testval);
  auto testval2 = object2.property_get("testprop");
  ASSERT_EQ(object2.property_get("testprop"), testval);
  object1.property_delete("testprop");
  ASSERT_EQ(object2.property_get("testprop"), "");
}

TEST(project, recent) {
  savestate x;
  std::string fn;
  for (auto i=0; i<2; ++i)
  {
    sjef::Project p("completely_new"+std::to_string(i)+".someprogram", nullptr, true);
    fn = p.filename();
    EXPECT_EQ(p.recent_find(fn), 1);
    EXPECT_EQ(sjef::Project("another.someprogram",nullptr,true).recent_find(fn), 2);
    EXPECT_EQ(p.recent(1), fn);
  }
  EXPECT_EQ(sjef::Project(fn,nullptr,true,false).recent_find(fn.c_str()), 0);
}

TEST(project, dummy_backend) {
  savestate x;
  sjef::Project p("completely_new", nullptr, true,true,"sjef");
  p.run(sjef::Backend::dummy_name, {}, 0, true);
  p.wait();
  EXPECT_EQ(p.file_contents("out"),"dummy");
  EXPECT_EQ(p.xml(), "<?xml version=\"1.0\"?>\n<root/>");
}
