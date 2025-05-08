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

TEST(filter_BloomFilter, AddAndContainsSingleItem) {
    BloomFilter bf(1024, 3);
    const std::string item = "hello";
    EXPECT_FALSE(bf.contains(item));
    bf.add(item);
    EXPECT_TRUE(bf.contains(item));
    EXPECT_FALSE(bf.contains("world"));
}

// -----------------------------------------------------------------------------
// Add multiple distinct items, check each is found, and a non‐inserted one
// still returns false.
// -----------------------------------------------------------------------------
TEST(filter_BloomFilter, AddMultipleItems) {
    BloomFilter bf(2048, 5);
    std::vector<std::string> items = {"alpha", "beta", "gamma", "delta",
                                      "epsilon"};
    for (const auto &s : items) {
        EXPECT_FALSE(bf.contains(s)) << "Pre‐add: unexpected hit for " << s;
        bf.add(s);
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
        bf1.add(s);
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
    bf.add("");
    EXPECT_TRUE(bf.contains(""));
}
