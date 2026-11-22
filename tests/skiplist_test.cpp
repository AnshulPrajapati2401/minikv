#include <gtest/gtest.h>
#include <string>
#include "minikv/skiplist.h"

using List = minikv::SkipList<std::string, std::string>;  // a short alias for the long type

TEST(SkipList, StartsEmpty) {
  List list;
  EXPECT_EQ(list.Size(), 0u);
}

TEST(SkipList, InsertThenFind) {
  List list;
  list.Insert("name", "Rahul");
  std::string v;                       // variable that Find will fill in
  EXPECT_TRUE(list.Find("name", &v));  // &v = the address of v
  EXPECT_EQ(v, "Rahul");
}

TEST(SkipList, MissingKeyNotFound) {
  List list;
  list.Insert("a", "1");
  std::string v;
  EXPECT_FALSE(list.Find("zzz", &v));
}

TEST(SkipList, InsertExistingKeyUpdates) {
  List list;
  EXPECT_TRUE(list.Insert("k", "old"));    // new key
  EXPECT_FALSE(list.Insert("k", "new"));   // existing key
  EXPECT_EQ(list.Size(), 1u);              // size did not grow
  std::string v;
  list.Find("k", &v);
  EXPECT_EQ(v, "new");
}

TEST(SkipList, ManyKeys) {
  List list;
  for (int i = 0; i < 1000; ++i) {
    list.Insert("key" + std::to_string(i), "val" + std::to_string(i));
  }
  EXPECT_EQ(list.Size(), 1000u);
  for (int i = 0; i < 1000; ++i) {
    std::string v;
    ASSERT_TRUE(list.Find("key" + std::to_string(i), &v));
    EXPECT_EQ(v, "val" + std::to_string(i));
  }
}
