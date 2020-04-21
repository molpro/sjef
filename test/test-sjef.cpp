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
  std::vector<std::string> testfiles;
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
//    for (const auto& file : testfiles)
//      std::cerr << "~savestate testfile " << file << std::endl;
    for (const auto& file : testfiles)
      sjef::Project::erase(file);
    for (const auto& suffix : std::vector<std::string>{"sjef", "molpro"}) {
      rf = sjef::expand_path(std::string{"~/."} + suffix + "/projects");
      if (!rf.empty() and fs::exists(rf + ".save")) {
//      std::cerr << "savestate restores " << rf << std::endl;
        fs::rename(rf + ".save", rf);
      }
    }
//    std::cerr << "~savestate returns " << std::endl;
  }
  std::string testfile(const std::string& file) {
    testfiles.push_back(sjef::expand_path(file));
    sjef::Project::erase(testfiles.back());
    return testfiles.back();
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
    ASSERT_EQ(sjef::Project(n.first, nullptr, true, false, "sjef").filename(), n.second);
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
  savestate state;
  std::string name("sjef-project-test");
  for (const auto& dirs : std::vector<std::string>{"$TMPDIR", "$HOME", "${HOME}", "~"}) {
    auto filename = state.testfile(dirs + "/" + name + ".sjef");
    sjef::Project x(filename);
    ASSERT_EQ(x.name(), name);
  }
}

TEST(project, move) {
  savestate state;
  std::string name("sjef-project-test");
  auto filename = state.testfile("$TMPDIR/" + name + ".sjef");
  auto name2 = name + "2";
  auto filename2 = state.testfile("$TMPDIR/" + name2 + ".sjef");
  sjef::Project x2(filename);
  ASSERT_EQ (x2.name(), name);
  ASSERT_EQ(x2.filename(), filename);
  x2.move(filename2);
  ASSERT_EQ(x2.name(), name2);
  ASSERT_EQ(x2.filename(), filename2);
  ASSERT_FALSE(fs::exists(fs::path(filename)));
  ASSERT_TRUE(fs::exists(fs::path(x2.filename())));
//  std::cerr << "filename="<<filename<<std::endl;
  x2.copy(filename);
  ASSERT_TRUE(fs::exists(fs::path(x2.filename())));
  ASSERT_TRUE(fs::exists(sjef::expand_path(filename2)));
  ASSERT_TRUE(fs::exists(sjef::expand_path(filename)));
  x2.move(filename, true);
  EXPECT_FALSE(fs::exists(sjef::expand_path(filename2)));
  EXPECT_TRUE(fs::exists(sjef::expand_path(filename)));
}

TEST(project, moveMolpro) {
  savestate state;
  auto filename_old = state.testfile("moveMolproOld.molpro");
  auto filename_new = state.testfile("moveMolproNew.molpro");
  sjef::Project p(filename_old, nullptr, true);
  std::ofstream(p.filename("inp")) << "geometry=" + p.name() + ".xyz" + "\n";
  std::ofstream(p.filename("xyz")) << "1\n\nHe 0 0 0\n";
  p.property_set("inpFile", p.name() + ".inp");
  EXPECT_EQ(p.property_get("inpFile"), p.name() + ".inp");
  p.move(filename_new, true);
//  EXPECT_FALSE(fs::exists(sjef::expand_path(filename_old)));
//  EXPECT_TRUE(fs::exists(sjef::expand_path(filename_new)));
//  std::string inp;
//  std::ifstream(p.filename("inp")) >> inp;
//  EXPECT_EQ(inp, "geometry=" + p.name() + ".xyz");
//  EXPECT_EQ(p.property_get("inpFile"), p.name() + ".inp");
//  exit(0);
}

TEST(project, copyMolpro) {
  savestate state;
  auto filename_old = state.testfile("copyMolproOld.molpro");
  auto filename_new = state.testfile("copyMolproNew.molpro");
  {
    sjef::Project p(filename_old);
    std::ofstream(p.filename("inp")) << "geometry=" + p.name() + ".xyz" + "\n";
    std::ofstream(p.filename("xyz")) << "1\n\nHe 0 0 0\n";
    p.copy(filename_new, true);
  }
  sjef::Project::erase(filename_old);
  EXPECT_FALSE(fs::exists(sjef::expand_path(filename_old)));
  sjef::Project p(filename_new);
  EXPECT_TRUE(fs::exists(sjef::expand_path(filename_new)));
  std::string inp;
  std::ifstream(p.filename("inp")) >> inp;
  EXPECT_EQ(inp, "geometry=" + p.name() + ".xyz");
}

TEST(project, erase) {
  savestate state;
  std::string filename = state.testfile("$TMPDIR/sjef-project-test.sjef");
  {
    sjef::Project x(filename);
    filename = x.filename();
    ASSERT_TRUE(fs::exists(fs::path(filename)));
    ASSERT_TRUE(fs::exists(fs::path(filename + "/Info.plist")));
    ASSERT_TRUE(fs::is_directory(fs::path(filename)));
  }
  sjef::Project::erase(filename);
  ASSERT_FALSE(fs::exists(fs::path(filename)));
}

TEST(project, import) {
  savestate state;
  std::string filename = state.testfile("$TMPDIR/sjef-project-test.sjef");
  sjef::Project x(filename);
  filename = x.filename();
  auto importfile = sjef::expand_path("$TMPDIR/sjef-project-test.import");
  boost::filesystem::ofstream ofs{importfile};
  ofs << "Hello" << std::endl;
  x.import_file(importfile);
  x.import_file(importfile, true);
}

TEST(project, clean) {
  savestate state;
  std::string filename = state.testfile("$TMPDIR/sjef-project-test.sjef");
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
  savestate state;
  auto filename = state.testfile("$TMPDIR/try.sjef");
  sjef::Project x(filename);
//  const auto keys = x.property_names();
//  std::string key;
  int ninitial;// = keys.size();
  std::map<std::string, std::string> data;
  data["first key"] = "first value";
//  data["second key"] = "second value";
//  data["third key"] = "third value";
  for (const auto& keyval : data)
    x.property_set(keyval.first, keyval.second);
//  system((std::string{"cat "}+x.propertyFile()).c_str());
//while(sleep(1));
//  std::cout<< "\n==================\n"<<std::endl;
  for (const auto& keyval : data) {
//    std::cout << "key "<<keyval.first<<" expect value: "<<keyval.second<<" actual value: "
//    <<x.property_get(keyval.first)<<std::endl;
    ASSERT_EQ(x.property_get(keyval.first), keyval.second);
  }
  if (false) {

  const auto keysnew = x.property_names();
  for (const auto& key : keysnew) {
//  for (n = 0; (key = x.property_next()) != ""; ++n) {
//    std::cout << "key "<<key<<std::endl;
    if (data.count("key") != 0)
      ASSERT_EQ(x.property_get(key), data[key]);
  }
//  std::cout << "data.size() "<<data.size()<<std::endl;
  ASSERT_EQ(keysnew.size(), data.size() + ninitial);
  for (const auto& key : x.property_names()) {
//    system(("echo start deletion loop key="+key+"; cat "+x.filename()+"/Info.plist").c_str());
    x.property_delete(key);
//    system(("echo end deletion loop key="+key+"; cat "+x.filename()+"/Info.plist").c_str());
  }
  ASSERT_TRUE(x.property_names().empty());

  ASSERT_EQ(x.property_get("vacuous"), "");
  x.property_set("empty", "");
  ASSERT_EQ(x.property_get("empty"), "");
  }
}

TEST(project, recent_files) {
  {

    savestate state;
    std::string suffix{"someprogram"};
    auto rf = sjef::expand_path("~/.sjef/" + suffix + "/projects");
    auto oldfile = fs::exists(rf);
    if (oldfile)
      fs::rename(rf, rf + ".save");
    std::string probername("$TMPDIR/prober." + suffix);
    state.testfile(probername);
    sjef::Project prober(probername);
    std::list<std::string> p;
    for (size_t i = 0; i < 3; i++) {
      {
        sjef::Project proj("$TMPDIR/p" + std::to_string(i) + "." + suffix);
        p.emplace_back(proj.filename());
      }
      state.testfile(p.back());
      {
        sjef::Project proj(p.back());
      }
    }
//  system((std::string{"cat "}+rf).c_str());
//    for (const auto& pp : p)
//      std::cerr << "p entry " << pp << std::endl;
    size_t i = p.size();
//    for (const auto& pp : p)
//      std::cerr << "p entry, recent table " << i << " " << sjef::Project(pp).recent(i--) << std::endl;
//    i = p.size();
    for (const auto& pp : p)
      ASSERT_EQ(prober.recent(i--), pp);
    i = p.size();
    for (const auto& pp : p)
      ASSERT_EQ(prober.recent_find(pp), i--);
    sjef::Project::erase(p.back());
    p.pop_back();
    i = p.size();
    for (const auto& pp : p)
      ASSERT_EQ(prober.recent(i--), pp);
    i = p.size();
    for (const auto& pp : p)
      ASSERT_EQ(prober.recent(i--), pp);
    for (auto& pp : p)
      sjef::Project::erase(pp);
    for (const auto& pp : p)
      ASSERT_EQ(prober.recent_find(pp), 0);
    if (oldfile)
      fs::rename(rf + ".save", rf);
  }
}

TEST(project, project_hash) {
  savestate state;
  sjef::Project x(state.testfile("$TMPDIR/try.sjef"));
  auto xph = x.project_hash();
  state.testfile("$TMPDIR/try2.sjef"); // remove any previous contents
  ASSERT_TRUE(x.copy("$TMPDIR/try2.sjef"));
  sjef::Project x2("$TMPDIR/try2.sjef");
  ASSERT_NE(xph, x2.project_hash());
  state.testfile("$TMPDIR/try3.sjef"); // remove any previous contents
  x.move("$TMPDIR/try3.sjef");
  ASSERT_EQ(xph, x.project_hash());
}

TEST(project, input_hash_molpro) {
  savestate state;
  sjef::Project x(state.testfile("$TMPDIR/try.molpro"));
  {
    std::ofstream ss(x.filename("inp"));
    ss << "one\ngeometry=try.xyz\ntwo" << std::endl;
  }
  {
    std::ofstream ss(x.filename("xyz"));
    ss << "1\nThe xyz file\nHe 0 0 0" << std::endl;
  }
  auto xph = x.input_hash();
  state.testfile("$TMPDIR/try2.molpro");
  ASSERT_TRUE(x.copy("$TMPDIR/try2.molpro"));
  sjef::Project x2("$TMPDIR/try2.molpro");
  ASSERT_EQ(xph, x2.input_hash());
  state.testfile("$TMPDIR/try3.molpro");
  x.move("$TMPDIR/try3.molpro");
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
  savestate state;
  sjef::Project He("He.molpro");
  EXPECT_EQ(He.file_contents("xml"), He.xml());
  {
    sjef::Project newProject(state.testfile("test___.someprogram"));
    EXPECT_EQ(newProject.file_contents("xml"), "");
    EXPECT_EQ(newProject.xml(), "<?xml version=\"1.0\"?><root/>");
  }
}

TEST(project, input_from_output) {
  savestate state;
  sjef::Project He("He.molpro");
  std::string input = He.file_contents("inp");
  input = std::regex_replace(input, std::regex{" *\n\n*"}, "\n");
  EXPECT_EQ(input, He.input_from_output());
  auto copy = state.testfile("Hecopy.molpro");
  He.copy(copy);
  sjef::Project Hecopy(copy);
  EXPECT_EQ(He.file_contents("inp"), Hecopy.file_contents("inp"));
  EXPECT_EQ(input, He.input_from_output());
  EXPECT_EQ(Hecopy.input_from_output(), "");
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

TEST(project, spawn_many_dummy) {
  savestate state;
  sjef::Project p(state.testfile("spawn_many.someprogram"));
  { std::ofstream(p.filename("inp")) << ""; }
  const auto& backend = sjef::Backend::dummy_name;
  for (auto i = 0; i < 5; ++i) {
//    std::cerr << "run number " << i << std::endl;
    ASSERT_TRUE(p.run(backend, -1, true, true));
    EXPECT_NE(p.property_get("jobnumber"), "-1");
    EXPECT_EQ(p.status(), sjef::completed);
  }

}

TEST(project, spawn_many_molpro) {
  savestate state;
  sjef::Project p(state.testfile("spawn_many.molpro"), nullptr, true);
  { std::ofstream(p.filename("inp")) << ""; }
  const auto& backend = sjef::Backend::default_name;
  if (not boost::process::search_path("molpro").empty()) // test the default backend only if it exists
    for (auto i = 0; i < 5; ++i) {
      ASSERT_TRUE(p.run(backend, -1, true, true));
      EXPECT_NE(p.property_get("jobnumber"), "-1");
      EXPECT_EQ(p.status(), sjef::completed);
    }

}

TEST(backend, backend_parameter_expand) {
  savestate state;
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
  { // another set of tests
    savestate x;
    auto& backend = sjef::Backend::dummy_name;
    sjef::Project p(state.testfile("backend_parameter_expand.molpro"));
    p.property_set("backend", backend);
    p.backend_parameter_set(backend, "thing", "its value");
    std::map<std::string, std::string> tests;
    std::vector<std::string> preambles{"stuff ", ""};
    auto test = [&preambles, &p, &backend](const std::string& run_command,
                                           const std::string& expect_resolved,
                                           const std::string& expect_documentation) {
      for (const auto& preamble : preambles) {
        p.m_backends[backend].run_command = preamble + run_command + " more stuff";
//        std::cout << "run_command set to "<<p.m_backends[backend].run_command<<std::endl;
//        std::cout << "documentation returned "<<p.backend_parameter_documentation(backend,"n")<<std::endl;
//        std::cout << "documentation expected "<<expect_documentation<<std::endl;
        EXPECT_EQ(p.backend_parameter_documentation(backend, "n"), expect_documentation);
        EXPECT_EQ(p.backend_parameter_expand(backend), preamble + expect_resolved + " more stuff");
      }
    };
    test("{ -n %n}", "", "");
    test("{ -n %n! with documentation}", "", " with documentation");
    test("{ -n %n:99}", " -n 99", "");
    test("{ -n %n:99! with documentation}", " -n 99", " with documentation");
    test("{ -n %thing}", " -n its value", "");
    test("{ -n %thing! with documentation}", " -n its value", "");
    test("{ -n %thing:99}", " -n its value", "");
    test("{ -n %thing:99! with documentation}", " -n its value", "");
    std::map<std::string, std::string> badtests;
    badtests["{ -n !%n:99 This is an ill-formed parameter string because the % comes in the comment, so should be detected as absent}"] =
        "";
    for (const auto& preamble : preambles)
      for (const auto& test : badtests)
        EXPECT_THROW(p.backend_parameter_expand(backend, preamble + test.first + " more stuff"), std::runtime_error);
  }
}

TEST(sjef, atomic) {
  savestate state;
  auto filename = state.testfile("He.someprogram");
  sjef::Project object1(filename);
  sjef::Project object2(filename);
  std::string testval = "testval";
  object1.property_set("testprop", testval);
  auto testval2 = object2.property_get("testprop");
  ASSERT_EQ(object2.property_get("testprop"), testval);
  object1.property_delete("testprop");
  ASSERT_EQ(object2.property_get("testprop"), "");
}

TEST(project, recent) {
  savestate state;
  std::string fn;
  auto fn2 = state.testfile("transient.someprogram");
  for (auto i = 0; i < 2; ++i) {
    sjef::Project p(state.testfile("completely_new" + std::to_string(i) + ".someprogram"));
    fn = p.filename();
    EXPECT_EQ(p.recent_find(fn), 1);
    {
      auto p2 = sjef::Project(fn2);
    }
    EXPECT_EQ(p.recent(1), fn2);
    EXPECT_EQ(p.recent_find(fn), 2);
    sjef::Project::erase(fn2);
    EXPECT_EQ(p.recent(1), fn);
    EXPECT_EQ(p.recent_find(fn), 1);
  }
  EXPECT_EQ(sjef::Project(fn).recent_find(fn.c_str()), 1);
}

TEST(project, dummy_backend) {
  savestate state;
  sjef::Project p(state.testfile("completely_new.sjef"));
  p.run(sjef::Backend::dummy_name, 0, true, false);
  p.wait();
  EXPECT_EQ(p.file_contents("out"), "dummy");
  EXPECT_EQ(p.xml(), "<?xml version=\"1.0\"?>\n<root/>");
}

TEST(project, project_name_embedded_space) {
  savestate state;
  sjef::Project p(state.testfile("completely new.molpro"));
  std::ofstream(p.filename("inp")) << "geometry={He};rhf\n";
  p.run(sjef::Backend::dummy_name, 0, true, false);
  p.wait();
  EXPECT_EQ(p.file_contents("out"), "dummy");
  EXPECT_EQ(p.xml(), "<?xml version=\"1.0\"?>\n<root/>");
}

