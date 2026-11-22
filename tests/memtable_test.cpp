#include <gtest/gtest.h>

#include <string>

#include "minikv/memtable.h"

using minikv::GetStatus;
using minikv::MemTable;

TEST(MemTable, PutThenGet) {
  MemTable mt;
  mt.Put("name", "Rahul");
  std::string v;
  EXPECT_EQ(mt.Get("name", &v), GetStatus::kFound);
  EXPECT_EQ(v, "Rahul");
}

TEST(MemTable, MissingKeyIsNotFound) {
  MemTable mt;
  std::string v;
  EXPECT_EQ(mt.Get("nope", &v), GetStatus::kNotFound);
}

TEST(MemTable, OverwriteReturnsNewest) {
  MemTable mt;
  mt.Put("k", "old");
  mt.Put("k", "new");
  std::string v;
  EXPECT_EQ(mt.Get("k", &v), GetStatus::kFound);
  EXPECT_EQ(v, "new");
  EXPECT_EQ(mt.Count(), 1u);
}

TEST(MemTable, DeleteIsDifferentFromNotFound) {
  MemTable mt;
  mt.Put("k", "v");
  mt.Delete("k");
  std::string v;
  EXPECT_EQ(mt.Get("k", &v), GetStatus::kDeleted);
  EXPECT_EQ(mt.Get("other", &v), GetStatus::kNotFound);
}

TEST(MemTable, DeleteOfUnknownKeyStoresTombstone) {
  MemTable mt;
  mt.Delete("ghost");
  std::string v;
  EXPECT_EQ(mt.Get("ghost", &v), GetStatus::kDeleted);
}

TEST(MemTable, PutAfterDeleteRevivesKey) {
  MemTable mt;
  mt.Put("k", "v1");
  mt.Delete("k");
  mt.Put("k", "v2");
  std::string v;
  EXPECT_EQ(mt.Get("k", &v), GetStatus::kFound);
  EXPECT_EQ(v, "v2");
}

TEST(MemTable, ApproximateSizeTracksChanges) {
  MemTable mt;
  EXPECT_EQ(mt.ApproximateSize(), 0u);
  mt.Put("ab", "cde");
  EXPECT_EQ(mt.ApproximateSize(), 5u);
  mt.Put("ab", "x");
  EXPECT_EQ(mt.ApproximateSize(), 3u);
  mt.Delete("ab");
  EXPECT_EQ(mt.ApproximateSize(), 2u);
}
