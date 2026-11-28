// # A memtable is the in-memory table where the database keeps its most recent writes.
//
// Think of it as the desk where you do your current work. Writing to the desk is fast,
// but if the power goes out, whatever is only on the desk is lost. That is why every change
// is also written to the WAL (the log on disk) first.
//
// # What it does
// Holds recent Put and Delete operations in RAM, sorted by key.
// Answers Get quickly for recent data.
// Records deletes as tombstones (a "deleted" marker), so older copies on disk stay hidden.
// Tracks its size, so the DB knows when it's full.
//
// How it relates to what we built
// The skip list is the sorted structure inside the memtable.
// MemTable is a thin layer on top that adds the tombstone logic (Delete) and size tracking (ApproximateSize).
// Why it must be sorted
//
// When the memtable is flushed to disk, it becomes a sorted file (an SSTable).
// Sorted data makes those files easy to search and merge during compaction.
//
// One line to remember: the memtable is the fast, sorted,
// in-memory buffer for recent writes, and the WAL is what makes it safe.

#pragma once

#include <cstddef>
#include <string>

#include "minikv/skiplist.h"
#include "minikv/status.h"

namespace minikv {
    class MemTable{
        public:
            MemTable() = default ;

            void Put(const std::string &key, const std::string &value);
            void Delete(const std::string &key);

            GetStatus Get(const std::string &key, std::string *value) const;

            size_t ApproximateSize() const { return approximate_size_; }
            size_t Count() const { return list_.Size(); }

        private:
            enum class EntryType { kValue, kTombstone };

            struct Entry {
              EntryType type = EntryType::kValue;
              std::string value;
            };

            void Apply(const std::string &key, EntryType type, const std::string &value);

            SkipList<std::string, Entry> list_;
            size_t approximate_size_ = 0;
    };
}
