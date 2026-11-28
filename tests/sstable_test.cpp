/*
 * SSTable tests: round trip, missing keys, tombstones, empty/single/large
 * tables, key-order enforcement, and rejection of unfinished, truncated
 * and corrupted files. Each test uses its own temp file in /tmp.
 */
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "minikv/sstable.h"

namespace fs = std::filesystem;
using minikv::GetStatus;
using minikv::SSTableReader;
using minikv::SSTableWriter;

namespace {

std::string Key(int i) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "key%06d", i);
  return buf;
}

std::string Val(int i) { return "value" + std::to_string(i); }

void FlipByte(const std::string& path, std::streamoff pos) {
  std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
  f.seekg(pos);
  char c = 0;
  f.get(c);
  f.seekp(pos);
  f.put(static_cast<char>(c ^ 0x01));
}

}  // namespace

class SstTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    path_ = (fs::temp_directory_path() /
             (std::string("minikv_sst_") + info->name() + ".sst"))
                .string();
    fs::remove(path_);
  }

  void TearDown() override { fs::remove(path_); }

  void Build(int count) {
    auto w = SSTableWriter::Create(path_);
    ASSERT_TRUE(w != nullptr);
    for (int i = 0; i < count; ++i) {
      ASSERT_TRUE(w->Add(Key(i), Val(i), false));
    }
    ASSERT_TRUE(w->Finish());
  }

  std::string path_;
};

TEST_F(SstTest, WriteThenGetEverything) {
  Build(100);
  auto r = SSTableReader::Open(path_);
  ASSERT_TRUE(r != nullptr);
  EXPECT_EQ(r->EntryCount(), 100u);
  for (int i = 0; i < 100; ++i) {
    std::string v;
    ASSERT_EQ(r->Get(Key(i), &v), GetStatus::kFound);
    EXPECT_EQ(v, Val(i));
  }
}

TEST_F(SstTest, MissingKeysAreNotFound) {
  Build(100);
  auto r = SSTableReader::Open(path_);
  ASSERT_TRUE(r != nullptr);
  std::string v;
  EXPECT_EQ(r->Get("a", &v), GetStatus::kNotFound);
  EXPECT_EQ(r->Get(Key(50) + "x", &v), GetStatus::kNotFound);
  EXPECT_EQ(r->Get("zzz", &v), GetStatus::kNotFound);
}

TEST_F(SstTest, TombstoneReturnsDeleted) {
  {
    auto w = SSTableWriter::Create(path_);
    ASSERT_TRUE(w != nullptr);
    ASSERT_TRUE(w->Add("a", "1", false));
    ASSERT_TRUE(w->Add("b", "", true));
    ASSERT_TRUE(w->Add("c", "3", false));
    ASSERT_TRUE(w->Finish());
  }
  auto r = SSTableReader::Open(path_);
  ASSERT_TRUE(r != nullptr);
  std::string v;
  EXPECT_EQ(r->Get("a", &v), GetStatus::kFound);
  EXPECT_EQ(v, "1");
  EXPECT_EQ(r->Get("b", &v), GetStatus::kDeleted);
  EXPECT_EQ(r->Get("c", &v), GetStatus::kFound);
  EXPECT_EQ(r->Get("bb", &v), GetStatus::kNotFound);
}

TEST_F(SstTest, EmptyTable) {
  Build(0);
  auto r = SSTableReader::Open(path_);
  ASSERT_TRUE(r != nullptr);
  EXPECT_EQ(r->EntryCount(), 0u);
  std::string v;
  EXPECT_EQ(r->Get("x", &v), GetStatus::kNotFound);
}

TEST_F(SstTest, SingleEntry) {
  Build(1);
  auto r = SSTableReader::Open(path_);
  ASSERT_TRUE(r != nullptr);
  std::string v;
  ASSERT_EQ(r->Get(Key(0), &v), GetStatus::kFound);
  EXPECT_EQ(v, Val(0));
}

TEST_F(SstTest, ManyEntriesAcrossIndexBlocks) {
  Build(5000);
  auto r = SSTableReader::Open(path_);
  ASSERT_TRUE(r != nullptr);
  EXPECT_EQ(r->EntryCount(), 5000u);
  for (int i = 0; i < 5000; ++i) {
    std::string v;
    ASSERT_EQ(r->Get(Key(i), &v), GetStatus::kFound);
    EXPECT_EQ(v, Val(i));
    if (i % 7 == 0) {
      EXPECT_EQ(r->Get(Key(i) + "x", &v), GetStatus::kNotFound);
    }
  }
}

TEST_F(SstTest, EmptyKeyAndValue) {
  {
    auto w = SSTableWriter::Create(path_);
    ASSERT_TRUE(w != nullptr);
    ASSERT_TRUE(w->Add("", "", false));
    ASSERT_TRUE(w->Finish());
  }
  auto r = SSTableReader::Open(path_);
  ASSERT_TRUE(r != nullptr);
  std::string v = "junk";
  EXPECT_EQ(r->Get("", &v), GetStatus::kFound);
  EXPECT_EQ(v, "");
}

TEST_F(SstTest, LargeValues) {
  {
    auto w = SSTableWriter::Create(path_);
    ASSERT_TRUE(w != nullptr);
    for (int i = 0; i < 20; ++i) {
      ASSERT_TRUE(w->Add(Key(i), std::string(10000, static_cast<char>('a' + i)),
                         false));
    }
    ASSERT_TRUE(w->Finish());
  }
  auto r = SSTableReader::Open(path_);
  ASSERT_TRUE(r != nullptr);
  for (int i = 0; i < 20; ++i) {
    std::string v;
    ASSERT_EQ(r->Get(Key(i), &v), GetStatus::kFound);
    EXPECT_EQ(v, std::string(10000, static_cast<char>('a' + i)));
  }
}

TEST_F(SstTest, AddRejectsUnsortedKeys) {
  auto w = SSTableWriter::Create(path_);
  ASSERT_TRUE(w != nullptr);
  EXPECT_TRUE(w->Add("b", "1", false));
  EXPECT_FALSE(w->Add("a", "2", false));
  EXPECT_FALSE(w->Add("b", "3", false));
  EXPECT_TRUE(w->Add("c", "4", false));
  EXPECT_TRUE(w->Finish());
}

TEST_F(SstTest, OpenFailures) {
  EXPECT_TRUE(SSTableReader::Open("/nonexistent_dir_minikv/x.sst") == nullptr);
  EXPECT_TRUE(SSTableWriter::Create("/nonexistent_dir_minikv/x.sst") ==
              nullptr);

  { std::ofstream f(path_, std::ios::binary); f << std::string(100, 'x'); }
  EXPECT_TRUE(SSTableReader::Open(path_) == nullptr);

  { std::ofstream f(path_, std::ios::binary); f << "abc"; }
  EXPECT_TRUE(SSTableReader::Open(path_) == nullptr);
}

TEST_F(SstTest, TruncatedFileRejected) {
  Build(100);
  fs::resize_file(path_, fs::file_size(path_) - 5);
  EXPECT_TRUE(SSTableReader::Open(path_) == nullptr);
}

TEST_F(SstTest, UnfinishedFileRejected) {
  {
    auto w = SSTableWriter::Create(path_);
    ASSERT_TRUE(w != nullptr);
    EXPECT_TRUE(w->Add("a", "1", false));
  }
  EXPECT_TRUE(SSTableReader::Open(path_) == nullptr);
}

TEST_F(SstTest, CorruptedEntryDetected) {
  Build(100);
  FlipByte(path_, 14);
  auto r = SSTableReader::Open(path_);
  ASSERT_TRUE(r != nullptr);
  std::string v;
  EXPECT_EQ(r->Get(Key(0), &v), GetStatus::kError);
}

TEST_F(SstTest, CorruptedIndexRejected) {
  Build(100);
  const auto size = static_cast<std::streamoff>(fs::file_size(path_));
  FlipByte(path_, size - 32 - 1);
  EXPECT_TRUE(SSTableReader::Open(path_) == nullptr);
}
