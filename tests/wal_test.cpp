#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "minikv/wal.h"

namespace fs = std::filesystem;
using minikv::ReadStatus;
using minikv::WalReader;
using minikv::WalRecord;
using minikv::WalRecordType;
using minikv::WalWriter;

namespace {

WalRecord Rec(WalRecordType type, const std::string& key,
              const std::string& value) {
  return WalRecord{type, key, value};
}

}  // namespace

class WalTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    path_ = (fs::temp_directory_path() /
             (std::string("minikv_wal_") + info->name() + ".log"))
                .string();
    fs::remove(path_);
  }

  void TearDown() override { fs::remove(path_); }

  std::string path_;
};

TEST_F(WalTest, CrcKnownValue) {
  const std::string s = "123456789";
  EXPECT_EQ(minikv::Crc32(s.data(), s.size()), 0xCBF43926u);
}

TEST_F(WalTest, WriteThenReadBack) {
  {
    auto writer = WalWriter::Open(path_, false);
    ASSERT_TRUE(writer != nullptr);
    EXPECT_TRUE(writer->Append(Rec(WalRecordType::kPut, "a", "1")));
    EXPECT_TRUE(writer->Append(Rec(WalRecordType::kPut, "b", "22")));
    EXPECT_TRUE(writer->Append(Rec(WalRecordType::kDelete, "a", "")));
  }

  auto reader = WalReader::Open(path_);
  ASSERT_TRUE(reader != nullptr);
  WalRecord r;

  ASSERT_EQ(reader->Next(&r), ReadStatus::kOk);
  EXPECT_EQ(r.type, WalRecordType::kPut);
  EXPECT_EQ(r.key, "a");
  EXPECT_EQ(r.value, "1");

  ASSERT_EQ(reader->Next(&r), ReadStatus::kOk);
  EXPECT_EQ(r.type, WalRecordType::kPut);
  EXPECT_EQ(r.key, "b");
  EXPECT_EQ(r.value, "22");

  ASSERT_EQ(reader->Next(&r), ReadStatus::kOk);
  EXPECT_EQ(r.type, WalRecordType::kDelete);
  EXPECT_EQ(r.key, "a");
  EXPECT_EQ(r.value, "");

  EXPECT_EQ(reader->Next(&r), ReadStatus::kEof);
}

TEST_F(WalTest, ReopenAppends) {
  {
    auto writer = WalWriter::Open(path_, false);
    ASSERT_TRUE(writer != nullptr);
    EXPECT_TRUE(writer->Append(Rec(WalRecordType::kPut, "a", "1")));
  }
  {
    auto writer = WalWriter::Open(path_, false);
    ASSERT_TRUE(writer != nullptr);
    EXPECT_TRUE(writer->Append(Rec(WalRecordType::kPut, "b", "2")));
  }

  auto reader = WalReader::Open(path_);
  ASSERT_TRUE(reader != nullptr);
  WalRecord r;
  ASSERT_EQ(reader->Next(&r), ReadStatus::kOk);
  EXPECT_EQ(r.key, "a");
  ASSERT_EQ(reader->Next(&r), ReadStatus::kOk);
  EXPECT_EQ(r.key, "b");
  EXPECT_EQ(reader->Next(&r), ReadStatus::kEof);
}

TEST_F(WalTest, TruncatedTailIsCorrupt) {
  {
    auto writer = WalWriter::Open(path_, false);
    ASSERT_TRUE(writer != nullptr);
    EXPECT_TRUE(writer->Append(Rec(WalRecordType::kPut, "a", "1")));
    EXPECT_TRUE(writer->Append(Rec(WalRecordType::kPut, "b", "2")));
    EXPECT_TRUE(writer->Append(Rec(WalRecordType::kPut, "c", "3")));
  }
  fs::resize_file(path_, fs::file_size(path_) - 3);

  auto reader = WalReader::Open(path_);
  ASSERT_TRUE(reader != nullptr);
  WalRecord r;
  EXPECT_EQ(reader->Next(&r), ReadStatus::kOk);
  EXPECT_EQ(reader->Next(&r), ReadStatus::kOk);
  EXPECT_EQ(reader->Next(&r), ReadStatus::kCorrupt);
  EXPECT_EQ(reader->GoodOffset(), 38u);
}

TEST_F(WalTest, FlippedByteIsCorrupt) {
  {
    auto writer = WalWriter::Open(path_, false);
    ASSERT_TRUE(writer != nullptr);
    EXPECT_TRUE(writer->Append(Rec(WalRecordType::kPut, "a", "first")));
    EXPECT_TRUE(writer->Append(Rec(WalRecordType::kPut, "b", "second")));
  }
  {
    std::fstream f(path_, std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(f.is_open());
    f.seekg(-1, std::ios::end);
    char c = 0;
    f.get(c);
    f.seekp(-1, std::ios::end);
    f.put(static_cast<char>(c ^ 0x01));
  }

  auto reader = WalReader::Open(path_);
  ASSERT_TRUE(reader != nullptr);
  WalRecord r;
  ASSERT_EQ(reader->Next(&r), ReadStatus::kOk);
  EXPECT_EQ(r.key, "a");
  EXPECT_EQ(reader->Next(&r), ReadStatus::kCorrupt);
}

TEST_F(WalTest, EmptyFileIsEof) {
  { std::ofstream create(path_, std::ios::binary); }

  auto reader = WalReader::Open(path_);
  ASSERT_TRUE(reader != nullptr);
  WalRecord r;
  EXPECT_EQ(reader->Next(&r), ReadStatus::kEof);
}

TEST_F(WalTest, SyncEveryWriteWorks) {
  {
    auto writer = WalWriter::Open(path_, true);
    ASSERT_TRUE(writer != nullptr);
    EXPECT_TRUE(writer->Append(Rec(WalRecordType::kPut, "k", "v")));
    EXPECT_TRUE(writer->Sync());
  }

  auto reader = WalReader::Open(path_);
  ASSERT_TRUE(reader != nullptr);
  WalRecord r;
  ASSERT_EQ(reader->Next(&r), ReadStatus::kOk);
  EXPECT_EQ(r.key, "k");
  EXPECT_EQ(r.value, "v");
}

TEST_F(WalTest, OpenFailures) {
  EXPECT_TRUE(WalWriter::Open("/nonexistent_dir_minikv/x.log", false) ==
              nullptr);
  EXPECT_TRUE(WalReader::Open("/nonexistent_dir_minikv/x.log") == nullptr);
}

TEST_F(WalTest, ManyRecords) {
  const int kCount = 10000;
  {
    auto writer = WalWriter::Open(path_, false);
    ASSERT_TRUE(writer != nullptr);
    for (int i = 0; i < kCount; ++i) {
      ASSERT_TRUE(writer->Append(Rec(WalRecordType::kPut,
                                     "key" + std::to_string(i),
                                     "value" + std::to_string(i))));
    }
  }

  auto reader = WalReader::Open(path_);
  ASSERT_TRUE(reader != nullptr);
  WalRecord r;
  for (int i = 0; i < kCount; ++i) {
    ASSERT_EQ(reader->Next(&r), ReadStatus::kOk);
    EXPECT_EQ(r.key, "key" + std::to_string(i));
    EXPECT_EQ(r.value, "value" + std::to_string(i));
  }
  EXPECT_EQ(reader->Next(&r), ReadStatus::kEof);
}
