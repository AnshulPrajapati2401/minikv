#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <string_view>
#include <utility>

#include "minikv/sstable.h"
#include "minikv/helpers.h"

namespace minikv{
    namespace {
        constexpr uint8_t kTypeValue = 1;
        constexpr uint8_t kTypeTombstone = 2;
        constexpr size_t kEntryHeader = 13;
        constexpr size_t kFooterSize = 32;
        constexpr uint64_t kMagic = 0x4D494E494B565354ull;
        constexpr uint64_t kIndexInterval = 16;
        constexpr size_t kFlushThreshold = 64 * 1024;

        bool ReadAt(int fd, uint64_t offset, char* buf, size_t n){
            while (n>0){
                ssize_t r = ::pread(fd, buf, n, static_cast<off_t>(offset));
                if (r < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    return false;
                }
                if (r == 0) {
                    return false;
                }

                buf += r;
                n -= static_cast<size_t>(r);
                offset += static_cast<uint64_t>(r);
            }
            return true ;
        }
    } // namespace ends

    // # SSTable writer
    SSTableWriter::SSTableWriter(int fd) : fd_(fd) {}
    SSTableWriter::~SSTableWriter() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    std::unique_ptr<SSTableWriter> SSTableWriter::Create(const std::string& path) {
        int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            return nullptr;
        }
        return std::unique_ptr<SSTableWriter>(new SSTableWriter(fd));
    }

    bool SSTableWriter::Flush() {
      if (!WriteAll(fd_, buffer_.data(), buffer_.size())) {
        failed_ = true;
        return false;
      }
      buffer_.clear();
      return true;
    }

    bool SSTableWriter::Add(const std::string &key, const std::string &value, bool tombstone) {
        if (finished_ || failed_) {
            return false;
        }
        if (count_ > 0 && key <= last_key_) {
            return false;
        }

        if (count_ % kIndexInterval == 0) {
            PutFixed32(&index_, static_cast<uint32_t>(key.size()));
            index_.append(key);
            PutFixed64(&index_, offset_);
        }
        std::string body;
        PutFixed32(&body, static_cast<uint32_t>(key.size()));
        PutFixed32(&body, static_cast<uint32_t>(value.size()));
        body.push_back(static_cast<char>(tombstone ? kTypeTombstone : kTypeValue));
        body.append(key);
        body.append(value);

        PutFixed32(&buffer_, Crc32(body.data(), body.size()));
        buffer_.append(body);
        offset_ += 4 + body.size();

        last_key_ = key;
        ++count_;

        if (buffer_.size() >= kFlushThreshold) {
            return Flush();
        }
        return true ;
    }

    bool SSTableWriter::Finish() {
        if (finished_ || failed_) {
            return false;
        }
        finished_ = true;

        const uint64_t index_offset = offset_;
        const uint64_t index_size = index_.size() + 4;
        const uint32_t index_crc = Crc32(index_.data(), index_.size());

        buffer_.append(index_);
        PutFixed32(&buffer_, index_crc);
        PutFixed64(&buffer_, index_offset);
        PutFixed64(&buffer_, index_size);
        PutFixed64(&buffer_, count_);
        PutFixed64(&buffer_, kMagic);

        if (!Flush()) {
            return false;
        }
        if (::fsync(fd_) != 0) {
            failed_ = true;
            return false;
        }
        const int fd = fd_;
        fd_ = -1;
        return ::close(fd) == 0;
    }

    // # SSTable reader
    SSTableReader::SSTableReader(int fd) : fd_(fd) {}
    SSTableReader::~SSTableReader() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    std::unique_ptr<SSTableReader> SSTableReader::Open(const std::string& path) {
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            return nullptr;
        }
        std::unique_ptr<SSTableReader> reader(new SSTableReader(fd));

        struct stat st;
        if (::fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(kFooterSize)) {
            return nullptr;
        }

        const uint64_t file_size = static_cast<uint64_t>(st.st_size);

        char footer[kFooterSize];
        if (!ReadAt(fd, file_size - kFooterSize, footer, kFooterSize)) {
            return nullptr;
        }
        const uint64_t index_offset = DecodeFixed64(footer);
        const uint64_t index_size = DecodeFixed64(footer + 8);
        const uint64_t entry_count = DecodeFixed64(footer + 16);
        const uint64_t magic = DecodeFixed64(footer + 24);

        if (magic != kMagic) {
            return nullptr;
        }
        if (index_size < 4 || index_offset > file_size || index_size > file_size ||
            index_offset + index_size + kFooterSize != file_size) {
                return nullptr;
        }

        std::string block(index_size, '\0');

        if (!ReadAt(fd, index_offset, &block[0], block.size())) {
            return nullptr;
        }

        const size_t body_size = block.size() - 4;
        if (Crc32(block.data(), body_size) != DecodeFixed32(block.data() + body_size)) {
                return nullptr;
        }

        const char* p = block.data();
        const char* end = p + body_size;
        while (p < end) {
            if (end - p < 4) {
                return nullptr;
            }
            const uint32_t key_len = DecodeFixed32(p);
            p += 4;
            if (static_cast<uint64_t>(end - p) < static_cast<uint64_t>(key_len) + 8) {
                return nullptr;
            }
            IndexEntry entry;
            entry.key.assign(p, key_len);
            p += key_len;
            entry.offset = DecodeFixed64(p);
            p += 8;

            if (entry.offset >= index_offset) {
                return nullptr;
            }
            if (!reader->index_.empty()) {
                const IndexEntry& prev = reader->index_.back();
                if (entry.key <= prev.key || entry.offset <= prev.offset) {
                    return nullptr;
                }
            }
            reader->index_.push_back(std::move(entry));
        }

        if ((entry_count == 0) != reader->index_.empty()) {
            return nullptr;
        }
        if (!reader->index_.empty() && reader->index_.front().offset != 0) {
            return nullptr;
        }

        reader->data_end_ = index_offset;
        reader->entry_count_ = entry_count;
        return reader;
    }

    GetStatus SSTableReader::Get(const std::string &key, std::string *value) const {
        auto it = std::upper_bound( index_.begin(), index_.end(), key,
            [](const std::string &k, const IndexEntry &e)
            { return k < e.key; });

        if (it == index_.begin()) {
            return GetStatus::kNotFound;
        }
        const size_t pos = static_cast<size_t>(it - index_.begin()) - 1;
        const uint64_t start = index_[pos].offset;
        const uint64_t stop = (pos + 1 < index_.size()) ? index_[pos + 1].offset : data_end_;

        std::string block(stop - start, '\0');
        if (!ReadAt(fd_, start, &block[0], block.size())) {
            return GetStatus::kError;
        }

        const char* p = block.data();
        const char* limit = p + block.size();
        while (p < limit) {
            if (static_cast<size_t>(limit - p) < kEntryHeader) {
                return GetStatus::kError;
            }
            const uint32_t crc = DecodeFixed32(p);
            const uint32_t key_len = DecodeFixed32(p + 4);
            const uint32_t value_len = DecodeFixed32(p + 8);
            const uint8_t type = static_cast<uint8_t>(p[12]);
            const uint64_t entry_size = kEntryHeader + static_cast<uint64_t>(key_len) + value_len;

            if (static_cast<uint64_t>(limit - p) < entry_size) {
                return GetStatus::kError;
            }
            if (Crc32(p + 4, static_cast<size_t>(entry_size) - 4) != crc) {
                return GetStatus::kError;
            }
            if (type != kTypeValue && type != kTypeTombstone) {
                return GetStatus::kError;
            }

            const int cmp = std::string_view(p + kEntryHeader, key_len).compare(key);
            if (cmp == 0) {
                if (type == kTypeTombstone) {
                    return GetStatus::kDeleted;
                }
                value->assign(p + kEntryHeader + key_len, value_len);
                return GetStatus::kFound;
            }
            if (cmp > 0) {
                return GetStatus::kNotFound;
            }
            p += entry_size;
        }
        return GetStatus::kNotFound;
    }
} // } namespace minikv
