/*
 * SSTable: an immutable, sorted file of key/value entries.
 *
 * File layout: [entries][index block][32-byte footer].
 * Each entry has a CRC. The sparse index (every 16th key) lives in memory
 * after Open, so a Get reads one small block with a single pread.
 *
 * SSTableWriter: Add() keys in strictly increasing order, then Finish()
 *                (writes index + footer, fsync). Without Finish the file
 *                is rejected by the reader.
 * SSTableReader: Open() validates the footer and index; Get() returns
 *                kFound / kDeleted / kNotFound / kError.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "minikv/status.h"

namespace minikv {
    class SSTableWriter {
        public:
            static std::unique_ptr<SSTableWriter> Create(const std::string &path);
            ~SSTableWriter();

            SSTableWriter(const SSTableWriter&) = delete;
            SSTableWriter& operator=(const SSTableWriter&) = delete;

            bool Add(const std::string &key, const std::string &value, bool tombstone);
            bool Finish();

            private:
                explicit SSTableWriter(int fd);
                bool Flush();

                int fd_;
                std::string buffer_;
                std::string index_;
                std::string last_key_;
                uint64_t offset_ = 0;
                uint64_t count_ = 0;
                bool finished_ = false;
                bool failed_ = false;
    };

    class SSTableReader {
        public:
            static std::unique_ptr<SSTableReader> Open(const std::string& path);
            ~SSTableReader();

            SSTableReader(const SSTableReader&) = delete;
            SSTableReader& operator=(const SSTableReader&) = delete;

            GetStatus Get(const std::string& key, std::string* value) const;
            uint64_t EntryCount() const { return entry_count_; }

        private:
            struct IndexEntry {
                std::string key;
                uint64_t offset = 0;
            };

            explicit SSTableReader(int fd);

            int fd_;
            std::vector<IndexEntry> index_;
            uint64_t data_end_ = 0;
            uint64_t entry_count_ = 0;
    };
}  // namespace minikv
