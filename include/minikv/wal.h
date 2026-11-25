/*
 * WAL (Write-Ahead Log)
 *
 * Every Put/Delete is appended to this log file on disk BEFORE it is
 * applied to the in-memory memtable. If the program crashes or power is
 * lost, the memtable is gone but the log survives. On restart, we replay
 * the log to rebuild the memtable, so no acknowledged write is lost.
 *
 * Record format:
 *   | crc32 (4) | payload length (4) | type (1) | key len (4) | key | value len (4) | value |
 *
 * The CRC32 checksum detects half-written or damaged records, so replay
 * stops safely at the first bad one.
 *
 * WalWriter appends records (optional fsync to force data onto disk).
 * WalReader reads them back: kOk, kEof (clean end), or kCorrupt.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

namespace minikv {
    uint32_t Crc32(const char *data , size_t size) ;
    enum class WalRecordType : uint8_t { kPut = 1, kDelete = 2 };

    struct WalRecord {
        WalRecordType type = WalRecordType::kPut ;
        std::string key ;
        std::string value ;
    };

    class WalWriter {
        public :
            static std::unique_ptr<WalWriter> Open (const std::string &path , bool sync_on_append) ;
            ~ WalWriter () ;
            WalWriter(const WalWriter&) = delete;
            WalWriter& operator=(const WalWriter&) = delete;
            bool Append(const WalRecord &record);
            bool Sync();
        private :
            WalWriter(int fd , bool sync_on_append) ;
            int fd_ ;
            bool sync_on_append_;
    };

    enum class ReadStatus { kOk, kEof, kCorrupt };

    class WalReader{
        public :
            static std::unique_ptr<WalReader> Open(const std::string& path);
            ReadStatus Next(WalRecord* record);
        private:
            explicit WalReader(const std::string& path);
            std::ifstream file_;
    };
}
