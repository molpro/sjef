#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <signal.h>

#if __has_include(<boost/process/v1/search_path.hpp>)
#include <boost/process/v1/search_path.hpp>
#else
#include <boost/process/search_path.hpp>
#endif
#include <chrono>
#include <fstream>
#ifndef WIN32
#include <libgen.h>
#endif
#include <list>
#include <map>
#include <memory>
#include <regex>
#include <sjef/sjef-backend.h>
#include <sjef/sjef.h>
#ifndef WIN32
#include <unistd.h>
#endif

#include "test-sjef.h"
#include <sjef/util/Locker.h>
#include <sjef/util/Shell.h>

namespace {

static std::mutex molpro_mutex;
class test_sjef_molpro : public test_sjef {
protected:
  sjef::Locker locker = sjef::Locker(".molpro_lock");
  virtual void SetUp() override {
    //    this->m_default_suffix = "molpro";
    test_sjef::_SetUp({"molpro"});
    //    molpro_mutex.lock();
    //    locker.add_bolt();
    //    std::cout << "Hello from molpro_test "<<&molpro_mutex<<" pid "<<getpid()<<std::endl;
  }

  //  virtual void TearDown() override { // molpro_mutex.unlock();
  //        locker.remove_bolt();
  //  }
};
} // namespace

TEST_F(test_sjef_molpro, spawn_many_molpro) {
  if (sjef::util::Shell::local_asynchronous_supported()) {
    sjef::Project p(testfile("spawn_many.molpro"));
    { std::ofstream(p.filename("inp")) << ""; }
    const auto& backend = sjef::Backend::default_name;
    if (not boost::process::search_path("molpro").empty()) // test the default backend only if it exists
      for (auto i = 0; i < 1; ++i) {
        ASSERT_TRUE(p.run(backend, 0, true, true));
        EXPECT_NE(p.property_get("jobnumber"), "-1");
        EXPECT_EQ(p.status(), sjef::completed);
      }
  }
}

TEST_F(test_sjef_molpro, molpro_workflow) {
  if (sjef::util::Shell::local_asynchronous_supported()) {
#ifdef WIN32
    // TODO get remote launch working for Windows
    constexpr bool test_remote = false;
    constexpr bool test_really_remote = false; // this can be used to test in a bespoke environment
#else
    constexpr bool test_remote = true;
    constexpr bool test_really_remote = false;
#endif
    constexpr bool timing = false;
    const auto cache = testfile(fs::current_path() / "molpro_workflow-cache");
    std::ofstream(sjef::expand_path((m_dot_sjef / "molpro" / "backends.xml").string()))
        << "<?xml version=\"1.0\"?>\n<backends>\n"
        << "<backend name=\"test-remote\" run_command=\"" << boost::process::search_path("molpro").string() << "\" host=\"127.0.0.1\" cache=\"" << cache.string() << "\"/>\n"
        << "<backend name=\"test-really-remote\" run_command=\"/usr/local/bin/molpro\" host=\"peterk@pjk2022.local\" />\n"
        << "</backends>";
    std::map<std::string, double> energies;
    for (int repeat = 0; repeat < 1; ++repeat) {
      auto remotes = std::vector<std::string>{"local"};
      if (test_remote)
        remotes.emplace_back("test-remote");
      if (test_really_remote)
        remotes.emplace_back("test-really-remote");
      for (const auto& backend : remotes) {
        std::map<std::string, std::unique_ptr<sjef::Project>> projects;
        fs::remove_all(cache);
        for (const auto& id : std::map<std::string, std::string>{//{"H", "angstrom; geometry={h};rhf"},
                                                                 {"H2", "angstrom; geometry={h;h,h,0.7};rhf"}}) {
          std::cout << "backend: " << backend << ", molecule: " << id.first << ", input: " << id.second << std::endl;
          auto file = testfile(id.first + "_" + backend + ".molpro");
          projects.insert({id.first, std::make_unique<sjef::Project>(fs::exists(file) ? file : testfile(file))});
          std::ofstream(projects[id.first]->filename("inp")) << id.second;
          std::ofstream(projects[id.first]->filename("out", "another")) << "another";
          projects[id.first]->change_backend(backend);
        }
        auto start_time = std::chrono::steady_clock::now();
        for (auto& pp : projects)
          pp.second->run(0, true, false);
        if (timing) {

          std::cout << "run time "
                    << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                             start_time)
                           .count()
                    << " ms" << std::endl;
          start_time = std::chrono::steady_clock::now();
          for (auto& pp : projects) {
            pp.second->wait();
          }
          std::cout << "wait time "
                    << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                             start_time)
                           .count()
                    << " ms" << std::endl;
        }
        for (auto& pp : projects) {
          auto& p = *(pp.second);
          p.wait();
          pugi::xml_document xmldoc;
          xmldoc.load_string(p.xml().c_str());
          auto results = xmldoc.select_nodes("//jobstep//property[@name='Energy']");
          ASSERT_GE(results.size(), 1) << "\n xml()=" << p.xml() << "\nxml file contents: " << p.file_contents("xml")
                                       << std::endl;
          auto energy = std::stod((results.end() - 1)->node().attribute("value").value());
          std::cout << "Energy " << pp.first << " : " << energy << std::endl;
          if (energies.count(pp.first) > 0) {
            EXPECT_NEAR(energy, energies[pp.first], 1e-12);
          }
          energies[pp.first] = energy;
        }
      }
    }
    //  end:;
  }
}
TEST_F(test_sjef_molpro, input_from_output) {
  sjef::Project He("He.molpro");
  std::string input = He.file_contents("inp");
  input = std::regex_replace(input, std::regex{"\r"}, "");
  input = std::regex_replace(input, std::regex{" *\n\n*"}, "\n");
  input = std::regex_replace(input, std::regex{"\n$"}, "");
  EXPECT_EQ(input, He.input_from_output());
  auto copy = testfile("Hecopy.molpro");
  He.copy(copy,false,false,false,0);
  sjef::Project Hecopy(copy);
  EXPECT_EQ(He.file_contents("inp"), Hecopy.file_contents("inp"));
  EXPECT_EQ(input, He.input_from_output());
  EXPECT_EQ(Hecopy.input_from_output(), "");
  EXPECT_NE(He.local_pid_from_output(),-1);
}

TEST_F(test_sjef_molpro, run_needed) {
  sjef::Project He("He.molpro");
  EXPECT_FALSE(He.run_needed());
  auto copy = testfile("Hecopy.molpro");
  He.copy(copy,false,false,false,999);
  sjef::Project Hecopy(copy);
  EXPECT_FALSE(Hecopy.run_needed());
  { std::ofstream(Hecopy.filename("inp")) << "geometry={He};crazy_command"; }
  EXPECT_TRUE(Hecopy.run_needed());

}

TEST_F(test_sjef_molpro, failure) {
  if (sjef::util::Shell::local_asynchronous_supported()) {
    sjef::Project p(testfile("test.molpro"));
    { std::ofstream(p.filename("inp")) << "geometry={He};rhf"; }
    p.run(0, true, true);
    EXPECT_EQ(p.status(), sjef::status::completed);
    { std::ofstream(p.filename("inp")) << "geometry={He};crazy_command"; }
    p.run(0, true, true);
    EXPECT_EQ(p.status(), sjef::status::failed)
        << "status: " << p.status_message() << "\nxml:\n"
        << p.xml() << "\noutput file:\n"
        << p.file_contents("out") << "\nxml file:\n"
        << p.file_contents("xml") << sjef::util::Shell()("ls -lRa " + p.filename().string()) << std::endl;
  }
}
