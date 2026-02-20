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

namespace fs = std::filesystem;

TEST_F(test_sjef, default_file_suffix) {
    ASSERT_EQ(sjef::backend_config_file_suffix(), "xml");
}

TEST_F(test_sjef, default_backend_config) {
    for (const auto &create_type: {"xml", "yaml"}) {
        auto backends = sjef::read_backend_config_file(suffix(), create_type);
        ASSERT_EQ(backends.size(), 1);
        EXPECT_EQ(backends.begin()->first, "local");
        EXPECT_EQ(backends.begin()->second.name, "local");
        EXPECT_EQ(backends.begin()->second.host, "localhost");
    }
}

TEST_F(test_sjef, sync_backend_config_file) {
    for (const auto pre_existing_read_type: {true, false})
        for (const auto &create_type: {"xml", "yaml"})
            for (const auto &read_type: {"xml", "yaml"}) {
                sjef::Backend be_local{sjef::Backend::local()};
                // std::cout << "create type " << create_type << " read type " << read_type << std::endl;
                be_local.run_command = "echo hello";
                be_local.name = "made";
                sjef::Backend be_bespoke{sjef::Backend::Linux()};
                be_bespoke.name = "bespoke";
                if (pre_existing_read_type)
                    sjef::write_backend_config_file({{"bespoke", be_bespoke}}, suffix(), read_type);
                sjef::write_backend_config_file({{"made", be_local}}, suffix(), create_type);
                sjef::sync_backend_config_file(suffix());
                auto be_read = sjef::read_backend_config_file(suffix(), read_type).at("made");
                ASSERT_EQ(be_read.str(), be_local.str());
            }
}
