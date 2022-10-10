#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test-sjef.h"
#include <filesystem>
#include <fstream>
#include <libgen.h>
#include <list>
#include <map>
#include <regex>
#include <sjef/sjef-backend.h>
#include <sjef/sjef.h>
#include <sjef/util/Locker.h>
#include <sjef/util/Shell.h>
#include <stdlib.h>
#include <unistd.h>

namespace fs = std::filesystem;

TEST_F(test_sjef, expand_path) {
  std::string slash{std::filesystem::path::preferred_separator};
  std::string cwd{std::filesystem::current_path().string()};
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

TEST_F(test_sjef, construction) {
  ASSERT_EQ(this->suffix(), ::testing::UnitTest::GetInstance()->current_test_info()->name());
  std::string name("sjef-project-test");
  auto filename = testproject(name);
  sjef::Project x(filename);
  ASSERT_EQ(x.name(), name);
}

TEST_F(test_sjef, move_generic) {
  std::string name("sjef-project-test");
  auto filename = testfile(name + "." + suffix());
  auto name2 = name + "2";
  auto filename2 = testfile(name2 + "." + suffix());
  sjef::Project x2(filename);
  ASSERT_EQ(x2.name(), name);
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
  EXPECT_TRUE(fs::exists(sjef::expand_path((fs::path{filename} / "Info.plist").string())));
}

TEST_F(test_sjef, moveMolpro) {
  auto filename_old = testfile("moveMolproOld." + suffix());
  auto filename_new = testfile("moveMolproNew." + suffix());
  sjef::Project p(filename_old);
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

TEST_F(test_sjef, copyMolpro) {
  auto filename_old = testfile("copyMolproOld." + suffix());
  auto filename_new = testfile("copyMolproNew." + suffix());
  sjef::Project::erase(filename_old);
  sjef::Project::erase(filename_new);
  std::string input;
  const int nkeep = 3;
  {
    sjef::Project p(filename_old);
    input = "geometry=" + p.name() + ".xyz";
    std::ofstream(p.filename("inp")) << input + "\n";
    std::ofstream(p.filename("xyz")) << "1\n\nHe 0 0 0\n";
    for (int i = 0; i < 5; ++i)
      p.run_directory_new();
    p.copy(filename_new, true, false, false, nkeep);
  }
  sjef::Project::erase(filename_old);
  EXPECT_FALSE(fs::exists(sjef::expand_path(filename_old)));
  sjef::Project p(filename_new);
  EXPECT_TRUE(fs::exists(sjef::expand_path(filename_new)));
  std::string inp;
  std::ifstream(p.filename("inp")) >> inp;
  EXPECT_EQ(inp, input);
  EXPECT_EQ(p.run_list().size(), nkeep) << std::system((std::string{"ls -lR "} + p.filename().string()).c_str());
}

TEST_F(test_sjef, erase) {
  auto filename = testproject("sjef-project-test");
  {
    sjef::Project x(filename);
    filename = x.filename();
    ASSERT_TRUE(fs::exists(fs::path(filename)));
    ASSERT_TRUE(fs::exists(filename / "Info.plist"));
    ASSERT_TRUE(fs::is_directory(fs::path(filename)));
  }
  sjef::Project::erase(filename);
  ASSERT_FALSE(fs::exists(fs::path(filename)));
}

TEST_F(test_sjef, import) {
  auto filename = testproject("sjef-project-test");
  sjef::Project x(filename);
  filename = x.filename();
  auto importfile = testfile("sjef-project-test.importfile");
  std::ofstream ofs{importfile};
  ofs << "Hello" << std::endl;
  x.import_file(importfile);
  x.import_file(importfile, true);
}

TEST_F(test_sjef, clean) {
  auto filename = testproject("sjef-project-test");
  sjef::Project x(filename);
  filename = x.filename();
  ASSERT_FALSE(fs::exists(fs::path(filename) / fs::path(filename.string() + ".out")));
  { std::ofstream s(fs::path(filename) / fs::path(x.name() + ".out")); }
  ASSERT_TRUE(fs::exists(fs::path(filename) / fs::path(x.name() + ".out")));
  x.clean(true, true);
  ASSERT_FALSE(fs::exists(fs::path(filename) / fs::path(x.name() + ".out")));
  ASSERT_FALSE(fs::is_directory(fs::path(filename) / fs::path(x.name() + ".d")));
  fs::create_directory(fs::path(filename) / fs::path(x.name() + ".d"));
  ASSERT_TRUE(fs::is_directory(fs::path(filename) / fs::path(x.name() + ".d")));
  x.clean(true);
  ASSERT_FALSE(fs::is_directory(fs::path(filename) / fs::path(x.name() + ".d")));
  const int ncreate = 5;
  for (int nkeep = 0; nkeep < ncreate + 2; ++nkeep) {
    x.clean(true, false, false, 0);
    ASSERT_EQ(x.run_list().size(), 0);
    for (int i = 0; i < ncreate; ++i)
      x.run_directory_new();
    ASSERT_EQ(x.run_list().size(), ncreate);
    x.clean(true, false, false, nkeep);
    ASSERT_EQ(x.run_list().size(), std::max(0, std::min(ncreate, nkeep)))
        << std::system((std::string{"ls -lR "} + x.filename().string()).c_str());
    int i = ncreate;
    auto runlist = x.run_list();
    for (auto it = runlist.begin(); it != runlist.end(); ++it)
      ASSERT_EQ(i--, *it);
  }
}

TEST_F(test_sjef, properties) {
  auto filename = testproject("try");
  sjef::Project x(filename);
  //  const auto keys = x.property_names();
  //  std::string key;
  int ninitial; // = keys.size();
  std::map<std::string, std::string> data;
  data["first key"] = "first value";
  //  data["second key"] = "second value";
  //  data["third key"] = "third value";
  for (const auto& keyval : data)
    x.property_set(keyval.first, keyval.second);
  //  system((std::string{"cat "}+x.propertyFile()).c_str());
  // while(sleep(1));
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
      if (data.count("key") != 0) {
        ASSERT_EQ(x.property_get(key), data[key]);
      }
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

TEST_F(test_sjef, recent_files) {
  {
    auto suffix = this->suffix();
    auto rf = sjef::expand_path(m_dot_sjef / suffix / "projects");
    auto rf_ = rf;
    rf_ += ".save";
    auto oldfile = fs::exists(rf);
    if (oldfile)
      fs::rename(rf, rf_);
    std::string probername("p r o b e r." + suffix);
    testfile(probername);
    sjef::Project prober(probername);
    std::list<std::filesystem::path> p;
    for (size_t i = 0; i < 3; i++) {
      {
        sjef::Project proj("p " + std::to_string(i) + "." + suffix);
        p.emplace_back(proj.filename());
      }
      testfile(p.back());
      { sjef::Project proj(p.back()); }
    }
    size_t i = p.size();
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
      fs::rename(rf_, rf);
  }
}

TEST_F(test_sjef, project_hash) {
  for (int repeat = 0; repeat < 10; ++repeat) {
    sjef::Project x(testproject("project_hash_try"));
    ASSERT_GT(fs::file_size(x.propertyFile()), 0);
    auto xph = x.project_hash();
    ASSERT_NE(xph, 0);
    auto f2 = testproject("project_hash_try2"); // remove any previous contents
    ASSERT_TRUE(x.copy(f2));
    sjef::Project x2(f2);
    EXPECT_GT(fs::file_size(x2.propertyFile()), 0) << x2.propertyFile() << ": " << fs::file_size(x2.propertyFile());
    EXPECT_NE(xph, x2.project_hash());
    auto f3 = testproject("project_hash_try3"); // remove any previous contents
    x.move(f3);
    EXPECT_EQ(xph, x.project_hash());
  }
}

TEST_F(test_sjef, input_hash_molpro) {
  sjef::Project x(testproject("project_hash_try"));
  {
    std::ofstream ss(x.filename("inp"));
    ss << "one\ngeometry=try.xyz\ntwo" << std::endl;
  }
  {
    std::ofstream ss(x.filename("xyz"));
    ss << "1\nThe xyz file\nHe 0 0 0" << std::endl;
  }
  auto xph = x.input_hash();
  auto try2 = testproject("project_hash_try2");
  fs::remove_all(try2);
  ASSERT_TRUE(x.copy(try2));
  sjef::Project x2(try2);
  ASSERT_EQ(xph, x2.input_hash());
  auto try3 = testproject("project_hash_try3");
  fs::remove_all(try3);
  x.move(try3);
  ASSERT_EQ(xph, x.input_hash());
}

TEST_F(test_sjef, xmlRepair) {
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
  sjef::mapstringstring_t plurals;
  plurals["orbitals"] = "<orbital a=\"b\"/>";
  EXPECT_EQ(xmlRepair("<orbitals>", plurals), "<orbitals><orbital a=\"b\"/></orbitals>");
}

TEST_F(test_sjef, xmloutput) {
  {
    sjef::Project newProject(testproject("test___"));
    EXPECT_EQ(newProject.file_contents("xml"), "");
    EXPECT_EQ(newProject.xml(), "<?xml version=\"1.0\"?><root/>");
  }
}

// TEST_F(test_sjef,input_from_output) {
//   test_sjef x;
//   sjef::Project p("test.sjef", nullptr,true);
//   std::string tempinp{"/tmp/test.inp"};
//   std::string inpstring ="one\n\ntwo  \n";
//   std::ofstream(tempinp) <<  inpstring;
//   p.import_file(tempinp);
//   EXPECT_TRUE(inpstring,p.)
// }

TEST_F(test_sjef, spawn_many_dummy) {
  sjef::Project p(testproject("spawn_many"));
  { std::ofstream(p.filename("inp")) << ""; }
  const auto& backend = sjef::Backend::dummy_name;
  for (auto i = 0; i < 5; ++i) {
    //    std::cerr << "run number " << i << std::endl;
    ASSERT_TRUE(p.run(backend, -1, true, true));
    EXPECT_EQ(p.status(), sjef::completed);
    //    p.wait();
    EXPECT_NE(p.property_get("jobnumber"), "-1");
  }
}

TEST_F(test_sjef, restart) {
  std::string path(getenv("PATH"));
  path = fs::current_path().string() + ":" + path;
  //  std::cout << "PATH " << path << std::endl;
  setenv("PATH", path.c_str(), 1);
  { std::ofstream("dummy") << "#!/bin/sh\nsleep 0;if [ $1 = '-v' ]; then shift; fi; echo dummyxml > ${1%.inp}.xml"; }
  fs::permissions("dummy", fs::perms::owner_exec, fs::perm_options::add);

  const std::filesystem::path& filename = testproject("restart");
  for (int restart = 0; restart < 2; ++restart) {
    sjef::Project p(filename);
    EXPECT_EQ(p.status(), restart == 0 ? sjef::status::unevaluated : sjef::status::completed);
    const auto& backend = sjef::Backend::dummy_name;
    for (auto i = 0; i < 2; ++i) {
      //      std::cerr << "run number " << i << std::endl;
      ASSERT_TRUE(p.run(backend, 0, true, false));
      p.wait();
      //    std::cout << p.xml() <<std::endl;
      EXPECT_EQ(p.xml(), "dummyxml") << p.file_contents("xml");
      EXPECT_EQ(p.status(), sjef::completed);
      EXPECT_NE(p.property_get("jobnumber"), "-1");
    }
  }
  fs::remove("dummy");
}

TEST_F(test_sjef, backend_cache) {
  sjef::Project p(testproject("backend_cache"));
  { std::ofstream(p.filename("inp")) << ""; }
  const auto& backend = sjef::Backend::dummy_name;
  ASSERT_TRUE(p.run(backend, -1, true, true));
  auto host = p.backends().at(backend).host;
  auto cache = p.backends().at(backend).cache + std::filesystem::path::preferred_separator + p.filename().string();
  EXPECT_EQ(p.backend_cache(), host + ":" + cache);
}

TEST_F(test_sjef, many_projects) {
  constexpr int n_projects = 500;
  constexpr int n_projects_with_jobs = 10;
  std::vector<std::unique_ptr<sjef::Project>> projects;
  projects.reserve(n_projects);
  for (int i = 0; i < n_projects; ++i)
    projects.emplace_back(
        new sjef::Project(testfile(std::string{"many_projects_"} + std::to_string(i) + ".molpro"), true, "molpro", {}));
  const auto& backend = sjef::Backend::dummy_name;
  for (auto& p : projects)
    EXPECT_EQ(p->status(), sjef::status::unevaluated);
  for (int i = 0; i < n_projects_with_jobs; ++i) {
    const auto& p = projects[i];
    { std::ofstream(p->filename("inp")) << ""; }
    ASSERT_TRUE(p->run(backend, -1, true, false));
    EXPECT_NE(p->property_get("jobnumber"), "-1");
  }
  for (int i = 0; i < n_projects_with_jobs; ++i) {
    const auto& p = projects[i];
    p->wait();
    EXPECT_EQ(p->status(), sjef::completed);
  }
}

#ifndef WIN32
TEST_F(test_sjef, early_change_backend) {
  auto suffix = this->suffix();
  auto backenddirectory = sjef::expand_path((m_dot_sjef / suffix).string());
  fs::create_directories(backenddirectory);
  auto backendfile = sjef::expand_path((fs::path{backenddirectory} / "backends.xml").string());
  std::ofstream(backendfile) << "<?xml version=\"1.0\"?>\n<backends><backend name=\"local\" "
                                "run_command=\"true\"/><backend name=\"test\" host=\"127.0.0.1\" "
                                "run_command=\"true\"/></backends>"
                             << std::endl;
  auto filename = testproject("early_change_backend");
  sjef::Project(filename).change_backend("test");
  EXPECT_EQ(sjef::Project(filename).filename(), filename);
  EXPECT_EQ(sjef::Project(filename).property_get("backend"), "test");
  EXPECT_THROW(sjef::Project(filename).change_backend("test2"), std::runtime_error);
  std::ofstream(backendfile) << "<?xml version=\"1.0\"?>\n<backends><backend name=\"test2\" host=\"127.0.0.1\" "
                                "run_command=\"true\"/></backends>"
                             << std::endl;
  EXPECT_EQ(sjef::Project(filename).filename(), filename);
  EXPECT_EQ(sjef::Project(filename).property_get("backend"), "local");
  EXPECT_THROW(sjef::Project(filename).change_backend("test"), std::runtime_error);
}
#endif

TEST_F(test_sjef, backend_parameter_expand) {
  sjef::Project He(testproject("He"));
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
TEST_F(test_sjef, backend_parameter_expand2) {
  auto& backend = sjef::Backend::dummy_name;
  sjef::Project p(testproject("backend_parameter_expand"));
  p.property_set("backend", backend);
  p.backend_parameter_set(backend, "thing", "its value");
  std::map<std::string, std::string> tests;
  std::vector<std::string> preambles{"stuff ", ""};
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-lambda-capture"
#endif
  auto test = [&preambles, &p, &backend](const std::string& run_command, const std::string& expect_resolved,
                                         const std::string& expect_documentation) {
    for (const auto& preamble : preambles) {
      p.backends()[backend].run_command = preamble + run_command + " more stuff";
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
  badtests["{ -n !%n:99 This is an ill-formed parameter string because the % comes in the comment, so should be "
           "detected as absent}"] = "";
  for (const auto& preamble : preambles)
    for (const auto& test : badtests)
      EXPECT_THROW(p.backend_parameter_expand(backend, preamble + test.first + " more stuff"), std::runtime_error);
}

TEST_F(test_sjef, atomic) {
  auto filename = testproject("He");
  std::string testval = "testval";
  sjef::Project object1(filename);
  sjef::Project object2(filename);
  //    std::cout << "@@@ constructors done"<<std::endl;
  object1.property_set("testprop", testval);
  //    std::cout << "@@@ set done"<<std::endl;
  //  std::cout << std::ifstream(fs::path{filename} / "Info.plist").rdbuf() << "\n@@@@@@@@@@@@@@@@@@@@" << std::endl;
  ASSERT_EQ(sjef::Project(filename).property_get("testprop"), testval);
  auto testval2 = object2.property_get("testprop");
  //    std::cout << "@@@ get done"<<std::endl;
  ASSERT_EQ(object2.property_get("testprop"), testval);
  object1.property_delete("testprop");
  ASSERT_EQ(object2.property_get("testprop"), "");
}

TEST_F(test_sjef, recent) {
  std::filesystem::path fn;
  std::string suffix = this->suffix();
  auto fn2 = testproject("transient");
  fs::path recent(fs::path("/Volumes/Home/Users/peterk/.sjef") / suffix / "projects");
  for (auto i = 0; i < 2; ++i) {
    {
      sjef::Project p(testproject("completely_new" + std::to_string(i)));
      fn = p.filename();
      suffix = fs::path(fn).extension().string().substr(1);
    }
    EXPECT_EQ(sjef::Project::recent_find(suffix, fn), 1);
    { auto p2 = sjef::Project(fn2); }
    EXPECT_EQ(sjef::Project::recent(suffix, 1), fn2);
    EXPECT_EQ(sjef::Project::recent_find(suffix, fn), 2);
    sjef::Project::erase(fn2);
    EXPECT_EQ(sjef::Project::recent(suffix, 1), fn);
    EXPECT_EQ(sjef::Project::recent_find(suffix, fn), 1);
  }
  EXPECT_EQ(sjef::Project::recent_find(suffix, fn), 1);
}

TEST_F(test_sjef, dummy_backend) {
  sjef::Project p(testproject("completely_new"));
  p.run(sjef::Backend::dummy_name, 0, true, false);
  p.wait();
  timespec delay;
  delay.tv_sec = 0;
  delay.tv_nsec = 100000000;
  nanosleep(&delay, NULL);
  EXPECT_EQ(p.file_contents("out"), "dummy")
      << "\nstdout:\n"
      << p.file_contents("stdout") << "\nstderr:\n"
      << p.file_contents("stderr") << sjef::util::Shell()("ls -lRa " + p.filename().string())
      << "\nPATH: " << std::getenv("PATH") << "\noutput from dummy thing.inp:\n"
      << sjef::util::Shell()("dummy thing.inp") << "\noutput from ../src/sjef/dummy another_thing.inp:\n"
      << sjef::util::Shell()("../src/sjef/dummy another_thing.inp") << "\ncurrent dir:\n"
      << sjef::util::Shell()("pwd; ls -lRa .") << std::endl;
  EXPECT_EQ(p.xml(), "<?xml version=\"1.0\"?>\n<root/>");
  EXPECT_EQ(p.file_contents("out", "", 1), "dummy");
  EXPECT_EQ(p.xml(1), "<?xml version=\"1.0\"?>\n<root/>");
}

TEST_F(test_sjef, project_name_embedded_space) {
  auto suffix = this->suffix();
  ASSERT_TRUE(fs::is_directory(sjef::expand_path(std::string{m_dot_sjef / suffix})));
  sjef::Project p(testproject("completely new"));
  std::ofstream(p.filename("inp")) << "geometry={He};rhf\n";
  p.run(sjef::Backend::dummy_name, 0, true, false);
  p.wait();
  EXPECT_EQ(p.file_contents("out"), "dummy");
  EXPECT_EQ(p.xml(), "<?xml version=\"1.0\"?>\n<root/>");
}

TEST_F(test_sjef, project_dir_embedded_space) {
  auto suffix = this->suffix();
  auto dir = fs::absolute("has some spaces");
  fs::remove_all(dir);
  std::cout << dir << std::endl;
  ASSERT_TRUE(fs::create_directories(dir));
  ASSERT_TRUE(fs::is_directory(sjef::expand_path(std::string{m_dot_sjef / suffix})));
  {
    sjef::Project p(testfile((dir / (std::string{"run_directory."} + suffix)).string()));
    std::ofstream(p.filename("inp")) << "geometry={He};rhf\n";
    p.run(sjef::Backend::dummy_name, 0, true, false);
    p.wait();
    EXPECT_EQ(p.file_contents("out"), "dummy");
    EXPECT_EQ(p.xml(), "<?xml version=\"1.0\"?>\n<root/>");
  }
  fs::remove_all(dir);
}

TEST_F(test_sjef, run_directory) {
  auto filename = testproject("run_directory");
  sjef::Project p(filename);
  std::string input = "geometry=" + p.name() + ".xyz";
  std::ofstream(p.filename("inp")) << input + "\n";
  std::string input2;
  std::ifstream(p.filename("inp")) >> input2;
  EXPECT_EQ(input, input2);
  std::ofstream(p.filename("xyz")) << "1\n\nHe 0 0 0\n";
  EXPECT_TRUE(fs::exists(sjef::expand_path(filename)));
  for (int i = 1; i < 4; i++) {
    auto si = std::to_string(i) + "." + suffix();
    auto rundir = p.run_directory_new();
    EXPECT_EQ(rundir, i);
    EXPECT_EQ(rundir, p.run_verify(rundir));
    EXPECT_EQ(rundir, p.run_verify(0));
    EXPECT_EQ(p.run_directory(), p.filename("", "", 0));
    EXPECT_EQ(p.run_directory(0), (fs::path{p.filename()} / "run" / si).string());
    EXPECT_EQ(p.filename("out", "", 0), (fs::path{p.filename()} / "run" / si / (std::to_string(i) + ".out")).string());
  }
  p.take_run_files(3, "3.inp", "copied.inp");
  std::ifstream(p.filename("", "copied.inp")) >> input2;
  EXPECT_EQ(input, input2);
  int seq = p.run_list().size();
  for (const auto& r : p.run_list())
    EXPECT_EQ(r, seq--); // the run_list goes in reverse order
  p.run_delete(3);
  EXPECT_EQ(2, p.run_verify(0));
  p.run_delete(1);
  EXPECT_EQ(2, p.run_verify(0));
  EXPECT_EQ(p.run_list(), sjef::Project::run_list_t{2});
  //  system((std::string("ls -lR ")+p.filename()).c_str());
}

#ifndef WIN32
TEST_F(test_sjef, sync_backend) {
  auto suffix = this->suffix();
  ASSERT_TRUE(fs::is_directory(sjef::expand_path(std::string{m_dot_sjef / suffix})));
  std::map<std::string, bool> cache_names{
      {"test-remote-cache", true},  {"test-r$mote-cache", false}, {"test-r?mote-cache", false},
      {"test-rémote-cache", true}, {"test-r%mote-cache", false}, {"test-rümote-cache", true},
      {"test-r*mote-cache", false}, {"test-r&mote-cache", false}, {"test-r mote-cache", false}};
  for (const auto& cache_name : cache_names) {
    const auto cache = testfile(fs::current_path() / cache_name.first);
    if (not fs::create_directories(cache))
      throw std::runtime_error("cannot create " + cache.string());
    const auto run_script = testfile("light.sh").string();
    std::ofstream(sjef::expand_path(std::string{m_dot_sjef / suffix / "backends.xml"}))
        << "<?xml version=\"1.0\"?>\n<backends>\n <backend name=\"local\" run_command=\"true\"/><backend "
           "name=\"test-remote\" run_command=\"sh "
        << run_script << "\" host=\"127.0.0.1\" cache=\"" << cache.string() << "\"/>\n</backends>";
    std::ofstream(run_script) << "while [ ${1#-} != ${1} ]; do shift; done; "
                                 "echo dummy > \"${1%.*}.out\";echo '<?xml "
                                 "version=\"1.0\"?>\n<root/>' > \"${1%.*}.xml\";";
    auto start_time = std::chrono::steady_clock::now();
    auto p = sjef::Project(testfile(std::string{"test_sync_backend."} + suffix));
    //    std::cout
    //        << "time to end of Project() "
    //        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
    //        start_time).count()
    //        << "ms" << std::endl;
    std::ofstream(p.filename("inp")) << "some input";
    //  std::cerr << "input file created " << p.filename("inp") << std::endl;

    if (cache_name.second) {
      p.run("test-remote", 0, true, false);
      //    std::cout
      //        << "time to end of run() "
      //        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
      //        start_time).count()
      //        << "ms" << std::endl;
      p.wait();
      //    std::cout
      //        << "time to end of wait() "
      //        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
      //        start_time).count()
      //        << "ms" << std::endl;
      ASSERT_EQ(p.file_contents("out"), "dummy");
      ASSERT_EQ(p.file_contents("xml"), "<?xml version=\"1.0\"?>\n<root/>");
      //  std::cout << "output: " << p.file_contents("out");
      //  std::cout << "xml: " << p.file_contents("xml");
      //      std::cout << "sleeping"<<std::endl;
      //      sleep(3);
      //    std::cout
      //        << "time to end "
      //        << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
      //        start_time).count()
      //        << "ms" << std::endl;
    } else {
      EXPECT_THROW(p.run("test-remote", 0, true, false), std::runtime_error) <<"cache name: "<< cache_name.first;
    }
  }
}
#endif

TEST_F(test_sjef, version) {
  std::cerr << "version: " << sjef::version() << std::endl;
  EXPECT_EQ(sjef::version(), SJEF_VERSION);
}

TEST_F(test_sjef, xpath_search) {
  auto suffix = this->suffix();
  auto p = sjef::Project(testfile(std::string{"xpath_search."} + suffix));
  std::ofstream(p.filename("inp")) << "test" << std::endl;
  p.run(sjef::Backend::dummy_name, 0, true, true);
  std::ofstream(p.filename("xml", "", 0))
      << "<?xml version=\"1.0\"?>\n<root><try att1=\"value1\">content1</try><try>content2<subtry/> </try></root>"
      << std::endl;
  EXPECT_EQ(p.status(), sjef::status::completed);
  EXPECT_EQ(p.select_nodes("/try").size(), 0) << p.xml() << std::endl;
  ASSERT_EQ(p.select_nodes("//try").size(), 2) << p.xml() << std::endl;
  auto node_set = p.select_nodes("//try");
  EXPECT_EQ(std::string{node_set[0].node().attribute("att1").value()}, "value1");
  EXPECT_EQ(std::string{node_set[1].node().attribute("att1").value()}, "");
  EXPECT_EQ(std::string{node_set[0].node().child_value()}, "content1");
  //  for (const auto& node : node_set) {
  //    std::cout << node.node().attribute("att1").value() <<std::endl;
  //    std::cout << node.node().child_value() <<std::endl;
  //  }
  EXPECT_EQ(p.xpath_search("/try").size(), 0);
  EXPECT_EQ(p.xpath_search("//try").size(), 2);
  //  for (const auto& s : p.xpath_search("//try"))
  //    std::cout << s << std::endl;
  EXPECT_EQ(p.xpath_search("//try", "att1").size(), 2);
  //  for (const auto& s : p.xpath_search("//try", "att1"))
  //    std::cout << s << std::endl;
  ASSERT_EQ(p.xpath_search("//try[@att1='value1']").size(), 1);
  EXPECT_EQ(p.xpath_search("//try[@att1='value1']").front(), "content1");
  EXPECT_EQ(p.xpath_search("//try[@att1='value1']", "att1").front(), "value1");
}

TEST_F(test_sjef, molpro_xpath_search) {
  auto suffix = this->suffix();
  auto p = sjef::Project(testproject("xpath_search"));
  std::ofstream(p.filename("xml"))
      << "<?xml version=\"1.0\"?>\n"
         "<molpro xmlns=\"http://www.molpro.net/schema/molpro-output\"\n"
         "  xmlns:xsd=\"http://www.w3.org/1999/XMLSchema\"\n"
         "  xmlns:cml=\"http://www.xml-cml.org/schema\"\n"
         "  xmlns:stm=\"http://www.xml-cml.org/schema\"\n"
         "  xmlns:xhtml=\"http://www.w3.org/1999/xhtml\">\n"
         " <job>\n"
         "  <jobstep command=\"RHF-SCF\" commandset=\"SCFPRO\">\n"
         "   <cml:molecule>\n"
         "    <cml:symmetry pointGroup=\"D2h\">\n"
         "     <cml:transform3 title=\"generator\" id=\"X\">\n"
         "      -1  0  0  0  0  1  0  0  0  0  1  0  0  0  0  1\n"
         "     </cml:transform3>\n"
         "     <cml:transform3 title=\"generator\" id=\"Y\">\n"
         "       1  0  0  0  0 -1  0  0  0  0  1  0  0  0  0  1\n"
         "     </cml:transform3>\n"
         "     <cml:transform3 title=\"generator\" id=\"Z\">\n"
         "       1  0  0  0  0  1  0  0  0  0 -1  0  0  0  0  1\n"
         "     </cml:transform3>\n"
         "    </cml:symmetry>\n"
         "    <cml:atomArray>\n"
         "     <cml:atom id=\"a1\" elementType=\"He\" x3=\"0.0\" y3=\"0.0\" z3=\"0.0\"/>\n"
         "    </cml:atomArray>\n"
         "    <cml:bondArray>\n"
         "    </cml:bondArray>\n"
         "   </cml:molecule>\n"
         "   <property name=\"Energy\" method=\"RHF\" principal=\"true\" stateSymmetry=\"1\" stateNumber=\"1\"\n"
         "     value=\"-2.85516047724273\"/>\n"
         "   <property name=\"One-electron energy\" method=\"RHF\" value=\"-3.88202510260424\"/>\n"
         "   <property name=\"Two-electron energy\" method=\"RHF\" value=\"1.0268646253615\"/>\n"
         "   <property name=\"Kinetic energy\" method=\"RHF\" value=\"2.85517613807823\"/>\n"
         "   <property name=\"Nuclear energy\" method=\"RHF\" value=\"0.0\"/>\n"
         "   <property name=\"Virial quotient\" method=\"RHF\" value=\"-0.999994514931921\"/>\n"
         "   <property name=\"Dipole moment\" method=\"RHF\" principal=\"true\" stateSymmetry=\"1\"\n"
         "     stateNumber=\"1\" value=\"0.0 0.0 0.0\"/>\n"
         "   <time start=\"18:15:58\" end=\"18:15:59\" cpu=\"0.41\" system=\"0.33\" real=\"0.92\"/>\n"
         "   <storage units=\"megabyte\" memory=\"0.0\" sf=\"0.0\" df=\"33.05\" eaf=\"0.0\"/>\n"
         "   <summary overall_method=\"RHF/cc-pVDZ\"/>\n"
         "  </jobstep>\n"
         "  <jobstep command=\"RKS-SCF\" commandset=\"SCFPRO\">\n"
         "   <property name=\"Energy\" method=\"RKS\" principal=\"true\" stateSymmetry=\"1\" stateNumber=\"1\"\n"
         "     value=\"-2.82670655414156\"/>\n"
         "   <property name=\"One-electron energy\" method=\"RKS\" value=\"-3.86358808216988\"/>\n"
         "   <property name=\"Two-electron energy\" method=\"RKS\" value=\"2.01853015705295\"/>\n"
         "   <property name=\"Kinetic energy\" method=\"RKS\" value=\"2.76263255051467\"/>\n"
         "   <property name=\"Nuclear energy\" method=\"RKS\" value=\"0.0\"/>\n"
         "   <property name=\"Virial quotient\" method=\"RKS\" value=\"-1.02319309660453\"/>\n"
         "   <property name=\"Dipole moment\" method=\"RKS\" principal=\"true\" stateSymmetry=\"1\"\n"
         "     stateNumber=\"1\" value=\"0.0 0.0 0.0\"/>\n"
         "   <time start=\"18:15:59\" end=\"18:15:59\" cpu=\"0.04\" system=\"0.04\" real=\"0.1\"/>\n"
         "   <storage units=\"megabyte\" memory=\"0.0\" sf=\"0.0\" df=\"33.05\" eaf=\"0.0\"/>\n"
         "   <summary overall_method=\"RLDA/cc-pVDZ\"/>\n"
         "  </jobstep>\n"
         "  <stm:metadataList>\n"
         "   <stm:metadata name=\"dc:date\" content=\"2021-12-26T18:15:59+00:00\"/>\n"
         "   <stm:metadata name=\"dc:creator\" content=\"peterk\"/>\n"
         "   <stm:metadata name=\"cmlm:insilico\" content=\"Molpro\"/>\n"
         "  </stm:metadataList>\n"
         "  <platform>\n"
         "   <version major=\"2021\" minor=\"4\" SHA=\"af59ab9ef6c61f0e96a8904ef31545f4b3889395\"\n"
         "     integer_bits=\"64\" parallelism=\"serial\">\n"
         "    2021.4\n"
         "    <date year=\"2021\" month=\"12\" day=\"26\" hour=\"18\" minute=\"15\" second=\"58\">\n"
         "     2021-12-26T18:15:58\n"
         "    </date>\n"
         "   </version>\n"
         "   <licence id=\"peterk\"/>\n"
         "   <parallel processes=\"1\" nodes=\"1\" all_processes=\"1\" openmp=\"1\"/>\n"
         "   <dimensions natom=\"400\" nvalence=\"500\" nvalence_pno=\"1000\" nbasis=\"12000\" nstate=\"100\"\n"
         "     nsymm=\"16\" nrec=\"512\" nprim=\"2\" ncol=\"100\"/>\n"
         "  </platform>\n"
         "  <input>\n"
         "   <p>geometry={He};rhf;rks</p>\n"
         "  </input>\n"
         "  <diagnostics warnings=\"0\"/>\n"
         " </job>\n"
         "</molpro>"
      << std::endl;
  EXPECT_EQ(p.xpath_search("/property[@name='Energy']").size(), 0);
  const std::vector<std::string>& energies = p.xpath_search("//property[@name='Energy']", "value");
  ASSERT_EQ(energies.size(), 2);
  EXPECT_NEAR(std::stod(energies[0]), -2.85516047724273, 1e-15);
  EXPECT_NEAR(std::stod(energies[1]), -2.82670655414156, 1e-15);
  //  for (const auto& s : energies)
  //    std::cout << s << std::endl;
  const std::vector<std::string>& input = p.xpath_search("//input/p");
  ASSERT_EQ(input.size(), 1);
  EXPECT_EQ(input.front(), "geometry={He};rhf;rks");
  //  for (const auto& s : input)
  //    std::cout << s << std::endl;
}

TEST_F(test_sjef, corrupt_geometry_include) {
  sjef::Project p(testproject("corrupt_geometry_include"));
  std::ofstream(p.filename("inp")) << "orient,mass;\n"
                                      "geomtyp=xyz;\n"
                                      "geometry=\n"
                                      "nanotube10-0-zigzag.xyz\n"
                                      "\n"
                                      "basis=vdz\n"
                                      "\n"
                                      "df-hf";
  p.change_backend(sjef::Backend::dummy_name);
  p.run(0, true, true);
}

TEST_F(test_sjef, reopen) {
  auto projectname = testproject("cproject");
  auto projectname2 = testproject("cproject2");
  std::string key = "testkey";
  std::string value = "testvalue";
  std::string value2 = "testvalue2";
  sjef::Project project(projectname);
  project.property_set(key, value);
  ASSERT_EQ(std::string{value}, std::string{project.property_get(key)});
  project.property_set(key, value2);
  ASSERT_EQ(std::string{value2}, std::string{project.property_get(key)});
  project.property_delete(key);
  ASSERT_EQ(std::string{}, std::string{project.property_get(key)});
  ASSERT_EQ(std::string{}, std::string{project.property_get("unknown key")});
  project.property_set(key, value);
  project.copy(projectname2, 0);
  ASSERT_EQ(std::string{value}, std::string{project.property_get(key)});
  {
    sjef::Project project2(projectname2);
    ASSERT_EQ(std::string{value}, std::string{project2.property_get(key)});
  }
  fs::remove_all(projectname2);
  ASSERT_EQ(project.move(projectname2), 1);
  sjef::Project project2(projectname2);
  sjef::Project project_reopened(projectname);
  //  sjef_project_open(projectname);
  //  std::cout << "project properties:\n" << std::ifstream(fs::path{projectname} / "Info.plist").rdbuf() << std::endl;
  //  std::cout << "project2 properties:\n" << std::ifstream(fs::path{projectname2} / "Info.plist").rdbuf() <<
  //  std::endl;
  ASSERT_EQ(std::string{}, std::string{project_reopened.property_get(key)});
  //  sjef_project_close(projectname);
  ASSERT_EQ(std::string{value}, std::string{project2.property_get(key)});
}

TEST_F(test_sjef, kill) {
  auto suffix = this->suffix();
  ASSERT_TRUE(fs::is_directory(sjef::expand_path(std::string{m_dot_sjef / suffix})));
  const auto cache = testfile(fs::current_path() / "test-remote-cache");
  if (not fs::create_directories(cache))
    throw std::runtime_error("cannot create " + cache.string());
  const auto run_script = testfile("light.sh").string();
  std::ofstream(sjef::expand_path(std::string{m_dot_sjef / suffix} + "/backends.xml"))
      << "<?xml version=\"1.0\"?>\n<backends>\n <backend name=\"local\" run_command=\"true\"/>"
      << "<backend name=\"test-local\" run_command=\"sh " << run_script << "\" />\n"
      << "<backend name=\"test-remote\" run_command=\"sh " << run_script << "\" host=\"127.0.0.1\" cache=\""
      << cache.string() << "\"/>\n"
      << "</backends>";
  std::ofstream(run_script) << "sleep 120;";
  auto p = sjef::Project(testfile(std::string{"test_kill."} + suffix));
  std::ofstream(p.filename("inp")) << "some input";
  p.run("test-local", 0, true, false);
  p.kill();
  p.wait();
  EXPECT_EQ(p.status(), sjef::killed)<<"Found status: "<<p.status_message();
  p.run("test-remote", 0, true, false);
  p.kill();
  EXPECT_EQ(p.status(), sjef::killed)<<"Found status: "<<p.status_message();
}
