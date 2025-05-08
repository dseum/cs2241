#include "mousedb/database/core.hpp"

#include <gtest/gtest.h>

#include "hlce.hpp"

using namespace mousedb::database;
using mousedb::hlc::HLC;

TEST(core, BasicOperations) {
    Options options = {
        .fresh = true,
    };
    Database db("/tmp/mousedb_test", options);
    ASSERT_EQ(db.find("key"), std::nullopt);
    HLC ts1{0, 0, 0};
    db.insert("key", "value", ts1);
    auto value = db.find("key");
    ASSERT_EQ(*value, "value");
    HLC ts2{1, 0, 0};
    db.erase("key", ts2);
    value = db.find("key");
    ASSERT_EQ(value, std::nullopt);
}

TEST(core, BasicOperationsWithWal) {
    {
        Options options = {
            .fresh = true,
        };
        Database db("/tmp/mousedb_test", options);
        HLC ts1{0, 0, 0};
        db.insert("key", "value", ts1);
        HLC ts2{1, 0, 0};
        db.insert("key2", "value2", ts2);
    }
    Options options;
    Database db("/tmp/mousedb_test", options);
    auto value = db.find("key");
    ASSERT_EQ(*value, "value");
    value = db.find("key2");
    ASSERT_EQ(*value, "value2");
}

TEST(core, DifferenceInHLC) {
    Options options = {
        .fresh = true,
    };
    Database db("/tmp/mousedb_test", options);
    HLC ts1{0, 0, 0};
    db.insert("key", "value", ts1);
    HLC ts2{2, 0, 0};
    db.insert("key", "value2", ts2);
    auto value = db.find("key");
    ASSERT_EQ(*value, "value2");
    db.erase("key", ts1);
    value = db.find("key");
    ASSERT_EQ(*value, "value2");
    HLC ts3{3, 0, 0};
    db.erase("key", ts3);
    value = db.find("key");
    ASSERT_EQ(value, std::nullopt);
}

TEST(core, Flush) {
    Options options = {
        .fresh = true,
        .flush_threshold = 0,
    };
    Database db("/tmp/mousedb_test", options);
    HLC ts{0, 0, 0};
    db.insert("key", "value", ts);
    auto value = db.find("key");
    ASSERT_EQ(*value, "value");
    ts = {1, 0, 0};
    db.erase("key", ts);
    value = db.find("key");
    ASSERT_EQ(value, std::nullopt);
}
