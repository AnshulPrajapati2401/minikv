#include "minikv/wal.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

namespace minikv {

    namespace {
        constexpr size_t kHeaderSize = 8;
        constexpr uint32_t kMinPayload = 9;
        constexpr uint32_t kMaxPayload = 1u << 26;

        void PutFixed32(std::string* dst, uint32_t value) {
            char buf[4];
            buf[0] = static_cast<char>(value & 0xFF);
            buf[1] = static_cast<char>((value >> 8) & 0xFF);
            buf[2] = static_cast<char>((value >> 16) & 0xFF);
            buf[3] = static_cast<char>((value >> 24) & 0xFF);
            dst->append(buf, 4);
        }

        uint32_t DecodeFixed32(const char* p) {
            const unsigned char* u = reinterpret_cast<const unsigned char*>(p);
            return static_cast<uint32_t>(u[0]) | (static_cast<uint32_t>(u[1]) << 8) |
                    (static_cast<uint32_t>(u[2]) << 16) |
                    (static_cast<uint32_t>(u[3]) << 24);
        }

        bool WriteAll(int fd, const char* data, size_t size) {
            while (size > 0) {
                ssize_t n = ::write(fd, data, size);
                if (n < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    return false;
                }
                data += n;
                size -= static_cast<size_t>(n);
            }
            return true;
        }
    }  // namespace

    uint32_t Crc32(const char* data, size_t size) {
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < size; ++i) {
            crc ^= static_cast<unsigned char>(data[i]);
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
            }
        }
        return ~crc;
    }

    WalWriter::WalWriter(int fd, bool sync_on_append)
        : fd_(fd), sync_on_append_(sync_on_append) {}

    WalWriter::~WalWriter() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    std::unique_ptr<WalWriter> WalWriter::Open(const std::string& path, bool sync_on_append) {
        int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) {
            return nullptr;
        }
        return std::unique_ptr<WalWriter>(new WalWriter(fd, sync_on_append));
    }

    bool WalWriter::Append(const WalRecord& record) {
        std::string payload;

        payload.push_back(static_cast<char>(record.type));
        PutFixed32(&payload, static_cast<uint32_t>(record.key.size()));
        payload.append(record.key);

        PutFixed32(&payload, static_cast<uint32_t>(record.value.size()));
        payload.append(record.value);

        std::string buffer;
        PutFixed32(&buffer, Crc32(payload.data(), payload.size()));
        PutFixed32(&buffer, static_cast<uint32_t>(payload.size()));
        buffer.append(payload);

        if (!WriteAll(fd_, buffer.data(), buffer.size())) {
            return false;
        }
        if (sync_on_append_) {
            return Sync();
        }
        return true;
    }

    bool WalWriter::Sync() { return ::fsync(fd_) == 0; }

    WalReader::WalReader(const std::string& path)
        : file_(path, std::ios::binary) {}

    std::unique_ptr<WalReader> WalReader::Open(const std::string& path) {
        std::unique_ptr<WalReader> reader(new WalReader(path));
        if (!reader->file_.is_open()) {
            return nullptr;
        }
        return reader;
    }

    ReadStatus WalReader::Next(WalRecord* record) {
        char header[kHeaderSize];
        file_.read(header, static_cast<std::streamsize>(kHeaderSize));
        const size_t header_got = static_cast<size_t>(file_.gcount());
        if (header_got == 0) {
            return ReadStatus::kEof;
        }
        if (header_got < kHeaderSize) {
            return ReadStatus::kCorrupt;
        }

        const uint32_t expected_crc = DecodeFixed32(header);
        const uint32_t length = DecodeFixed32(header + 4);
        if (length < kMinPayload || length > kMaxPayload) {
            return ReadStatus::kCorrupt;
        }

        std::string payload(length, '\0');
        file_.read(&payload[0], static_cast<std::streamsize>(length));

        if (static_cast<size_t>(file_.gcount()) < length) {
            return ReadStatus::kCorrupt;
        }

        if (Crc32(payload.data(), payload.size()) != expected_crc) {
           return ReadStatus::kCorrupt;
        }

        const char* p = payload.data();
        const char* end = p + payload.size();

        const uint8_t type = static_cast<uint8_t>(*p);
        ++p;

        if (type != static_cast<uint8_t>(WalRecordType::kPut) &&
            type != static_cast<uint8_t>(WalRecordType::kDelete)) {
                return ReadStatus::kCorrupt;
        }

        if (end - p < 4) {
           return ReadStatus::kCorrupt;
        }
        const uint32_t key_len = DecodeFixed32(p);
        p += 4;

        if (static_cast<size_t>(end - p) < key_len) {
           return ReadStatus::kCorrupt;
        }
        std::string key(p, key_len);
        p += key_len;

        if (end - p < 4) {
           return ReadStatus::kCorrupt;
        }

        const uint32_t value_len = DecodeFixed32(p);
        p += 4;

        if (static_cast<size_t>(end - p) != value_len) {
            return ReadStatus::kCorrupt;
        }

        record->type = static_cast<WalRecordType>(type);
        record->key = std::move(key);
        record->value.assign(p, value_len);
        good_offset_ += kHeaderSize + length;
        return ReadStatus::kOk;
    }
}  // namespace minikv
