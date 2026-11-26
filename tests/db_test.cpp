#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "minikv/db.h"

namespace fs = std::filesystem;
using minikv::DB;
using minikv::Options;

class DbTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    dir_ = (fs::temp_directory_path() /
            (std::string("minikv_db_") + info->name()))
               .string();
    fs::remove_all(dir_);
  }

  void TearDown() override { fs::remove_all(dir_); }

  std::string LogPath() const { return dir_ + "/wal.log"; }

  std::string dir_;
};

TEST_F(DbTest, PutGetDelete) {
  auto db = DB::Open(dir_);
  ASSERT_TRUE(db != nullptr);

  EXPECT_TRUE(db->Put("name", "Rahul"));
  std::string v;
  EXPECT_TRUE(db->Get("name", &v));
  EXPECT_EQ(v, "Rahul");

  EXPECT_TRUE(db->Delete("name"));
  EXPECT_FALSE(db->Get("name", &v));
}

TEST_F(DbTest, OverwriteReturnsNewest) {
  auto db = DB::Open(dir_);
  ASSERT_TRUE(db != nullptr);

  EXPECT_TRUE(db->Put("k", "old"));
  EXPECT_TRUE(db->Put("k", "new"));
  std::string v;
  EXPECT_TRUE(db->Get("k", &v));
  EXPECT_EQ(v, "new");
}

TEST_F(DbTest, MissingKeyNotFound) {
  auto db = DB::Open(dir_);
  ASSERT_TRUE(db != nullptr);

  std::string v;
  EXPECT_FALSE(db->Get("nope", &v));
}

TEST_F(DbTest, ReopenRecoversData) {
  {
    auto db = DB::Open(dir_);
    ASSERT_TRUE(db != nullptr);
    EXPECT_TRUE(db->Put("a", "1"));
    EXPECT_TRUE(db->Put("b", "2"));
  }

  auto db = DB::Open(dir_);
  ASSERT_TRUE(db != nullptr);
  std::string v;
  EXPECT_TRUE(db->Get("a", &v));
  EXPECT_EQ(v, "1");
  EXPECT_TRUE(db->Get("b", &v));
  EXPECT_EQ(v, "2");
}

TEST_F(DbTest, DeleteSurvivesReopen) {
  {
    auto db = DB::Open(dir_);
    ASSERT_TRUE(db != nullptr);
    EXPECT_TRUE(db->Put("a", "1"));
    EXPECT_TRUE(db->Put("b", "2"));
    EXPECT_TRUE(db->Delete("a"));
  }

  auto db = DB::Open(dir_);
  ASSERT_TRUE(db != nullptr);
  std::string v;
  EXPECT_FALSE(db->Get("a", &v));
  EXPECT_TRUE(db->Get("b", &v));
  EXPECT_EQ(v, "2");
}

TEST_F(DbTest, LatestWriteWinsAfterReopen) {
  {
    auto db = DB::Open(dir_);
    ASSERT_TRUE(db != nullptr);
    EXPECT_TRUE(db->Put("k", "v1"));
    EXPECT_TRUE(db->Put("k", "v2"));
    EXPECT_TRUE(db->Put("k", "v3"));
  }

  auto db = DB::Open(dir_);
  ASSERT_TRUE(db != nullptr);
  std::string v;
  EXPECT_TRUE(db->Get("k", &v));
  EXPECT_EQ(v, "v3");
}

TEST_F(DbTest, HundredThousandKeysSurviveReopen) {
  const int kCount = 100000;
  {
    auto db = DB::Open(dir_);
    ASSERT_TRUE(db != nullptr);
    for (int i = 0; i < kCount; ++i) {
      ASSERT_TRUE(
          db->Put("key" + std::to_string(i), "value" + std::to_string(i)));
    }
    for (int i = 0; i < kCount; i += 10) {
      ASSERT_TRUE(db->Delete("key" + std::to_string(i)));
    }
  }

  auto db = DB::Open(dir_);
  ASSERT_TRUE(db != nullptr);
  for (int i = 0; i < kCount; ++i) {
    std::string v;
    const bool found = db->Get("key" + std::to_string(i), &v);
    if (i % 10 == 0) {
      EXPECT_FALSE(found);
    } else {
      ASSERT_TRUE(found);
      EXPECT_EQ(v, "value" + std::to_string(i));
    }
  }
}

TEST_F(DbTest, TruncatedLogRecoversAndKeepsWorking) {
  {
    auto db = DB::Open(dir_);
    ASSERT_TRUE(db != nullptr);
    EXPECT_TRUE(db->Put("a", "1"));
    EXPECT_TRUE(db->Put("b", "2"));
    EXPECT_TRUE(db->Put("c", "3"));
  }
  fs::resize_file(LogPath(), fs::file_size(LogPath()) - 3);

  {
    auto db = DB::Open(dir_);
    ASSERT_TRUE(db != nullptr);
    std::string v;
    EXPECT_TRUE(db->Get("a", &v));
    EXPECT_TRUE(db->Get("b", &v));
    EXPECT_FALSE(db->Get("c", &v));
    EXPECT_TRUE(db->Put("d", "4"));
  }

  auto db = DB::Open(dir_);
  ASSERT_TRUE(db != nullptr);
  std::string v;
  EXPECT_TRUE(db->Get("a", &v));
  EXPECT_EQ(v, "1");
  EXPECT_TRUE(db->Get("b", &v));
  EXPECT_EQ(v, "2");
  EXPECT_FALSE(db->Get("c", &v));
  EXPECT_TRUE(db->Get("d", &v));
  EXPECT_EQ(v, "4");
}

TEST_F(DbTest, OpenFailsWhenPathIsAFile) {
  { std::ofstream create(dir_); }
  EXPECT_TRUE(DB::Open(dir_) == nullptr);
}

TEST_F(DbTest, SyncOnWriteOption) {
  Options options;
  options.sync_on_write = true;
  {
    auto db = DB::Open(dir_, options);
    ASSERT_TRUE(db != nullptr);
    EXPECT_TRUE(db->Put("k", "v"));
  }

  auto db = DB::Open(dir_, options);
  ASSERT_TRUE(db != nullptr);
  std::string v;
  EXPECT_TRUE(db->Get("k", &v));
  EXPECT_EQ(v, "v");
}

TEST_F(DbTest, EmptyKeyAndValue) {
  {
    auto db = DB::Open(dir_);
    ASSERT_TRUE(db != nullptr);
    EXPECT_TRUE(db->Put("", ""));
  }

  auto db = DB::Open(dir_);
  ASSERT_TRUE(db != nullptr);
  std::string v = "junk";
  EXPECT_TRUE(db->Get("", &v));
  EXPECT_EQ(v, "");
}
