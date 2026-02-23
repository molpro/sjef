#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test-sjef.h"
#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <regex>
#include <sjef/sjef-backend.h>
#include <sjef/sjef.h>
#include <sjef/util/Locker.h>
#include <sjef/util/Shell.h>
#include <stdlib.h>
#include <sjef/util/Job.h>
#include <pugixml.hpp>
#ifndef WIN32
#include <unistd.h>
#endif
#include <sjef/backend-config.h>
#include <time.h>

namespace fs = std::filesystem;

TEST_F(test_sjef, default_backend_config_file_suffix) {
    ASSERT_EQ(sjef::backend_config_file_suffix(), "xml");
}

TEST_F(test_sjef, default_backend_config) {
    auto backends = sjef::load_backend_config(suffix());
    ASSERT_EQ(backends.size(), 1);
    EXPECT_EQ(backends.begin()->first, "local");
    EXPECT_EQ(backends.begin()->second.name, "local");
    EXPECT_EQ(backends.begin()->second.host, "localhost");
}

std::string other_type(const std::string &type) {
    return type == "xml" ? "yaml" : "xml";
}

TEST_F(test_sjef, sync_backend_config_file) {
    std::map<std::string, sjef::Backend> stale_backends, fresh_backends;
    stale_backends["local"] = sjef::Backend::local();
    fresh_backends["local"] = sjef::Backend::local();
    stale_backends["stale"] = sjef::Backend::Linux();
    stale_backends["stale"].name = "stale";
    fresh_backends["fresh"] = sjef::Backend::Linux();
    fresh_backends["fresh"].name = "fresh";
    for (const auto identical_contents: {true, false})
        for (const auto pre_existing_read_type: {true, false})
            for (const auto &create_type: {"xml", "yaml"})
                for (const auto &read_type: {"xml", "yaml"}) {
                    sjef::Backend be_local{sjef::Backend::local()};
                    // std::cout << "@@@ identical contents " << identical_contents << " preexisting_read_type " <<
                    // pre_existing_read_type << " create type " << create_type << " read type " << read_type <<
                    // std::endl;
                    be_local.run_command = "echo hello";
                    be_local.name = "made";
                    sjef::Backend be_bespoke{sjef::Backend::Linux()};
                    be_bespoke.name = "bespoke";
                    std::string expected_synced_type{""};
                    if (pre_existing_read_type) {
                        if (identical_contents)
                            sjef::write_backend_config_file(fresh_backends, suffix(), read_type);
                        else {
                            sjef::write_backend_config_file(stale_backends, suffix(), read_type);
                        }
                        nanosleep((const struct timespec[]){{0, 100000000L}}, NULL);

                        expected_synced_type =
                                (identical_contents && pre_existing_read_type && read_type != create_type)
                                    ? ""
                                    : read_type != create_type
                                          ? read_type
                                          : other_type(read_type);
                        // std::cout << "created read_type " << read_type << std::endl;
                    } else if (read_type != create_type)
                        expected_synced_type = read_type;
                    else
                        expected_synced_type = other_type(read_type);
                    // std::cout << "@ write fresh_backends" << std::endl;
                    sjef::write_backend_config_file(fresh_backends, suffix(), create_type);
                    EXPECT_EQ(sjef::sync_backend_config_file(suffix()), expected_synced_type);
                    auto backends_read = sjef::read_backend_config_file(suffix(), read_type);
                    // auto be_read = backends_read.at(be_local.name);
                    // ASSERT_EQ(be_read.str(), be_local.str());
                    // std::cout << "backends_read\n";
                    // for (const auto &[name, backend]: backends_read)
                    // std::cout << name << " " << backend.str() << std::endl;
                    // std::cout << std::endl;
                    // std::cout << "fresh_backends\n";
                    // for (const auto &[name, backend]: fresh_backends)
                    // std::cout << name << " " << backend.str() << std::endl;
                    for (const auto &[name, backend]: fresh_backends)
                        EXPECT_EQ(backends_read[name], fresh_backends[name]) << "backends_read:\n" << backends_read[
                                                                                name].str() << "\nfresh_backends:\n" <<
 fresh_backends[name].str();
                    for (const auto &s: {"xml", "yaml"})
                        std::filesystem::remove(sjef::backend_config_file_path(suffix(), s));
                }
}
TEST_F(test_sjef, yaml_parse) {
    std::map<std::string, sjef::Backend> reference_backends;
    reference_backends["bespoke"] = sjef::Backend::Linux();
    reference_backends["bespoke"].name = "bespoke";
    reference_backends["bespoke"].run_command = "molpro {-n %n!MPI size}";
    sjef::write_backend_config_file(reference_backends, suffix(), "yaml");
    auto backends = sjef::read_backend_config_file(suffix(), "yaml");
    EXPECT_EQ(backends["bespoke"], reference_backends["bespoke"])<<"\nReturned:\n"<<backends["bespoke"].str()<<"\nReference:\n"<<reference_backends["bespoke"].str();
}

TEST(test_backends, my_backends) {
auto backends = sjef::read_backend_config_file("molpro", "yaml");
}
