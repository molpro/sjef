#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <signal.h>

#include "sjef-backend.h"
#include "sjef.h"
#include <boost/process/search_path.hpp>
#include <chrono>
#include <fstream>
#include <libgen.h>
#include <list>
#include <map>
#include <memory>
#include <regex>
#include <unistd.h>

#include "test-sjef.h"
#include <Locker.h>

namespace {

static std::mutex molpro_mutex;
class molpro_test : public ::testing::Test {
protected:
  sjef::Locker locker = sjef::Locker(".molpro_lock");
  virtual void SetUp() override {
    //    molpro_mutex.lock();
    //    locker.add_bolt();
    //    std::cout << "Hello from molpro_test "<<&molpro_mutex<<" pid "<<getpid()<<std::endl;
  }

  virtual void TearDown() override { // molpro_mutex.unlock();
    //    locker.remove_bolt();
  }
};
} // namespace

TEST_F(molpro_test, spawn_many_molpro) {
  savestate state("molpro");
  sjef::Project p(state.testfile("spawn_many.molpro"));
  { std::ofstream(p.filename("inp")) << ""; }
  const auto& backend = sjef::Backend::default_name;
  if (not boost::process::search_path("molpro").empty()) // test the default backend only if it exists
    for (auto i = 0; i < 1; ++i) {
      ASSERT_TRUE(p.run(backend, -1, true, true));
      EXPECT_NE(p.property_get("jobnumber"), "-1");
      EXPECT_EQ(p.status(false), sjef::completed);
    }
}

TEST_F(molpro_test, molpro_workflow) {
#ifdef WIN32
  // TODO get remote launch working for Windows
  constexpr bool test_remote = false;
#else
  constexpr bool test_remote = true;
#endif
  savestate state("molpro");
  const auto cache = state.testfile(fs::current_path() / "molpro_workflow-cache");
  std::ofstream(sjef::expand_path(std::string{"~/.sjef/molpro/backends.xml"}))
      << "<?xml version=\"1.0\"?>\n<backends>\n <backend name=\"test-remote\" run_command=\""
      << boost::process::search_path("molpro").string() << "\" host=\"127.0.0.1\" cache=\"" << cache
      << "\"/>\n</backends>";
  //  ASSERT_EQ(system("cat ~/.sjef/molpro/backends.xml"),0);
  std::map<std::string, double> energies;
  for (int repeat = 0; repeat < 1; ++repeat) {
    auto remotes = std::vector<std::string>{"local"};
    if (test_remote)
      remotes.push_back("test-remote");
    for (const auto& backend : remotes) {
      std::map<std::string, std::unique_ptr<sjef::Project>> projects;
      fs::remove_all(cache);
      for (const auto& id : std::map<std::string, std::string>{{"H", "angstrom; geometry={h};rhf"},
                                                               {"H2", "angstrom; geometry={h;h,h,0.7};rhf"}}) {
        std::cout << "backend: " << backend << ", molecule: " << id.first << ", input: " << id.second << std::endl;
        auto file = id.first + "_" + backend + ".molpro";
        //        fs::remove_all(file);
        //        EXPECT_GE(system((std::string{"ls -laR "}+file).c_str()),-1);
        //        EXPECT_GE(system((std::string{"cat "}+file+"/Info.plist").c_str()),-1);
        //        std::cout << "exists("<<file<<")="<<fs::exists(file)<<std::endl;
        projects.insert({id.first, std::make_unique<sjef::Project>(fs::exists(file) ? file : state.testfile(file))});
        //        std::cout << "created new project, properties "; for (const auto& n :
        //        projects[id.first]->property_names()) std::cout << " "<<n; std::cout << std::endl;
        std::ofstream(projects[id.first]->filename("inp")) << id.second;
        projects[id.first]->change_backend(backend);
      }
      auto start_time = std::chrono::steady_clock::now();
      //      std::cout << "before run loop"<<std::endl;
      for (auto& pp : projects)
        pp.second->run(0, true, false);
      std::cout << "run time "
                << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time)
                       .count()
                << " ms" << std::endl;
      start_time = std::chrono::steady_clock::now();
      for (auto& pp : projects)
        pp.second->wait();
      std::cout << "wait time "
                << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time)
                       .count()
                << " ms" << std::endl;
      for (auto& pp : projects) {
        auto& p = *(pp.second);
        std::cout << "Project " << p.name() << std::endl;
        p.wait();
        //            std::cout << "_private_sjef_project_backend_inactive
        //            "<<p.property_get("_private_sjef_project_backend_inactive") << ",
        //            _private_sjef_project_backend_inactive_synced
        //            "<<p.property_get("_private_sjef_project_backend_inactive_synced")<<std::endl;
        p.synchronize();
        //        std::cout <<"after synchronize():\n"<<p.xml()<<std::endl;
        //            std::cout << "_private_sjef_project_backend_inactive
        //            "<<p.property_get("_private_sjef_project_backend_inactive") << ",
        //            _private_sjef_project_backend_inactive_synced
        //            "<<p.property_get("_private_sjef_project_backend_inactive_synced")<<std::endl;
        //        std::cout << p.status_message() << std::endl;
        pugi::xml_document xmldoc;
        //      std::cout << p.xml() << std::endl;
        xmldoc.load_string(p.xml().c_str());
        auto results = xmldoc.select_nodes("//jobstep//property[@name='Energy']");
        //        if (results.empty()) {
        //          std::cerr << getpid() << std::endl;
        //          EXPECT_EQ(system((std::string{"ls -lR "} + cache).c_str()), 0);
        //          EXPECT_EQ(system((std::string{"ls -lR "} + p.filename()).c_str()), 0);
        //          kill(getpid(), SIGSTOP);
        //                            goto end;
        //        }
        ASSERT_GE(results.size(), 1) << "\n xml()=" << p.xml() << "\nxml file contents: " << p.file_contents("xml")
                                     << std::endl;
        //  for (const auto& result:results)
        //    std::cout << result.node().attribute("value").value() <<std::endl;
        //  std::cout << (results.end()-1)->node().attribute("value").value()<<std::endl;
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
TEST_F(molpro_test, input_from_output) {
  savestate state("molpro");
  sjef::Project He("He.molpro");
  std::string input = He.file_contents("inp");
  input = std::regex_replace(input, std::regex{"\r"}, "");
  input = std::regex_replace(input, std::regex{" *\n\n*"}, "\n");
  input = std::regex_replace(input, std::regex{"\n$"}, "");
  EXPECT_EQ(input, He.input_from_output());
  auto copy = state.testfile("Hecopy.molpro");
  He.copy(copy);
  sjef::Project Hecopy(copy);
  EXPECT_EQ(He.file_contents("inp"), Hecopy.file_contents("inp"));
  EXPECT_EQ(input, He.input_from_output());
  EXPECT_EQ(Hecopy.input_from_output(), "");
}

TEST_F(molpro_test, run_needed) {
  savestate x("molpro");
  sjef::Project He("He.molpro");
  EXPECT_FALSE(He.run_needed());
}
