#include "filter.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace mousedb::filter;

TEST(filter_BloomFilter, EmptyFilterContainsNothing) {
    BloomFilter bf(1024, 3);
    EXPECT_FALSE(bf.contains(""));
    EXPECT_FALSE(bf.contains("foo"));
    EXPECT_FALSE(bf.contains("bar"));
}

TEST(filter_BloomFilter, InsertAndContainsSingleItem) {
    BloomFilter bf(1024, 3);
    const std::string item = "hello";
    EXPECT_FALSE(bf.contains(item));
    bf.insert(item);
    EXPECT_TRUE(bf.contains(item));
    EXPECT_FALSE(bf.contains("world"));
}

TEST(filter_BloomFilter, InsertMultipleItems) {
    BloomFilter bf(2048, 5);
    std::vector<std::string> items = {"alpha", "beta", "gamma", "delta",
                                      "epsilon"};
    for (const auto &s : items) {
        EXPECT_FALSE(bf.contains(s)) << "Pre‐add: unexpected hit for " << s;
        bf.insert(s);
    }
    for (const auto &s : items) {
        EXPECT_TRUE(bf.contains(s)) << "Post‐add: missing " << s;
    }
    EXPECT_FALSE(bf.contains("zeta"));
}

TEST(filter_BloomFilter, SaveAndLoadPreservesContents) {
    BloomFilter bf1(4096, 4);
    std::vector<std::string> items = {"one", "two", "three"};
    for (const auto &s : items) {
        bf1.insert(s);
    }

    FILE *fp = std::tmpfile();
    ASSERT_NE(fp, nullptr) << "Failed to open temporary file for writing";
    bf1.save(fp);

    std::rewind(fp);
    BloomFilter bf2(fp);
    std::fclose(fp);

    for (const auto &s : items) {
        EXPECT_TRUE(bf2.contains(s)) << "Loaded filter missing " << s;
    }
    EXPECT_FALSE(bf2.contains("four"));
}

TEST(filter_BloomFilter, SupportsEmptyString) {
    BloomFilter bf(128, 2);
    EXPECT_FALSE(bf.contains(""));
    bf.insert("");
    EXPECT_TRUE(bf.contains(""));
}

static CuckooFilter make_cf() {
    return CuckooFilter(16, 4, 8, 500);
}

TEST(filter_CuckooFilter, EmptyFilterContainsNothing) {
    auto cf = make_cf();
    EXPECT_FALSE(cf.contains(""));
    EXPECT_FALSE(cf.contains("foo"));
    EXPECT_FALSE(cf.contains("bar"));
}

TEST(filter_CuckooFilter, InsertAndContainsSingleItem) {
    auto cf = make_cf();
    const std::string item = "hello";
    EXPECT_FALSE(cf.contains(item));
    EXPECT_TRUE(cf.insert(item));
    EXPECT_TRUE(cf.contains(item));
    EXPECT_FALSE(cf.contains("world"));
}

TEST(filter_CuckooFilter, InsertMultipleItems) {
    auto cf = make_cf();
    std::vector<std::string> items = {"alpha", "beta", "gamma", "delta",
                                      "epsilon"};
    for (auto &s : items) {
        EXPECT_FALSE(cf.contains(s)) << "Pre‐insert: unexpected hit for " << s;
        EXPECT_TRUE(cf.insert(s)) << "Failed to insert " << s;
    }
    for (auto &s : items) {
        EXPECT_TRUE(cf.contains(s)) << "Post‐insert: missing " << s;
    }
    EXPECT_FALSE(cf.contains("zeta"));
}

TEST(filter_CuckooFilter, EraseExistingItem) {
    auto cf = make_cf();
    const std::string item = "delete_me";
    EXPECT_TRUE(cf.insert(item));
    EXPECT_TRUE(cf.contains(item));
    EXPECT_TRUE(cf.erase(item));
    EXPECT_FALSE(cf.contains(item));
    // removing again should fail
    EXPECT_FALSE(cf.erase(item));
}

TEST(filter_CuckooFilter, EraseNonexistentItem) {
    auto cf = make_cf();
    EXPECT_FALSE(cf.erase("nothing_here"));
}

TEST(filter_CuckooFilter, SaveAndLoadPreservesContents) {
    auto cf1 = make_cf();
    std::vector<std::string> items = {"one", "two", "three"};
    for (auto &s : items) {
        ASSERT_TRUE(cf1.insert(s)) << "Setup insert failed for " << s;
    }

    FILE *fp = std::tmpfile();
    ASSERT_NE(fp, nullptr) << "Failed to open temporary file for writing";
    cf1.save(fp);

    std::rewind(fp);
    CuckooFilter cf2(fp);
    std::fclose(fp);

    for (auto &s : items) {
        EXPECT_TRUE(cf2.contains(s)) << "Loaded filter missing " << s;
    }
    EXPECT_FALSE(cf2.contains("four"));
    // also test that removal still works on loaded filter
    EXPECT_TRUE(cf2.erase("two"));
    EXPECT_FALSE(cf2.contains("two"));
}

static CuckooMap make_cm() {
    return CuckooMap(16, 4, 8, 500);
}

TEST(filter_CuckooMap, EmptyMapContainsNothing) {
    auto cm = make_cm();
    EXPECT_FALSE(cm.contains(""));
    EXPECT_FALSE(cm.contains("foo"));
    EXPECT_FALSE(cm.contains("bar"));
}

TEST(filter_CuckooMap, InsertAndContainsSingleItem) {
    auto cm = make_cm();
    const std::string item = "hello";
    EXPECT_FALSE(cm.contains(item));
    EXPECT_TRUE(cm.insert(item));
    EXPECT_TRUE(cm.contains(item));
    EXPECT_FALSE(cm.contains("world"));
}

TEST(filter_CuckooMap, InsertMultipleItems) {
    auto cm = make_cm();
    std::vector<std::string> items = {"alpha", "beta", "gamma", "delta",
                                      "epsilon"};
    for (const auto &s : items) {
        EXPECT_FALSE(cm.contains(s)) << "Pre‐insert: unexpected hit for " << s;
        EXPECT_TRUE(cm.insert(s)) << "Failed to insert " << s;
    }
    for (const auto &s : items) {
        EXPECT_TRUE(cm.contains(s)) << "Post‐insert: missing " << s;
    }
    EXPECT_FALSE(cm.contains("zeta"));
}

TEST(filter_CuckooMap, ChainFallback) {
    CuckooMap cm(1, 1, 8, 1);
    EXPECT_TRUE(cm.insert("first"));
    EXPECT_TRUE(cm.insert("second"));
    EXPECT_TRUE(cm.contains("first"));
    EXPECT_TRUE(cm.contains("second"));
}

TEST(filter_CuckooMap, EraseExistingItem) {
    auto cm = make_cm();
    EXPECT_TRUE(cm.insert("to_delete"));
    EXPECT_TRUE(cm.contains("to_delete"));
    EXPECT_TRUE(cm.erase("to_delete"));
    EXPECT_FALSE(cm.contains("to_delete"));
    EXPECT_FALSE(cm.erase("to_delete"));
}

TEST(filter_CuckooMap, EraseNonexistentItem) {
    auto cm = make_cm();
    EXPECT_FALSE(cm.erase("nothing_here"));
}

TEST(filter_CuckooMap, SaveAndLoadPreservesContents) {
    auto cm1 = make_cm();
    std::vector<std::string> items = {"one", "two", "three", "four", "five"};
    for (const auto &s : items) {
        ASSERT_TRUE(cm1.insert(s)) << "Setup insert failed for " << s;
    }

    FILE *fp = std::tmpfile();
    ASSERT_NE(fp, nullptr) << "Failed to open temporary file for writing";
    cm1.save(fp);

    std::rewind(fp);
    CuckooMap cm2(fp);
    std::fclose(fp);

    for (const auto &s : items) {
        EXPECT_TRUE(cm2.contains(s)) << "Loaded map missing " << s;
    }
    EXPECT_FALSE(cm2.contains("bob"));
}
