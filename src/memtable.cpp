#include "minikv/memtable.h"

namespace minikv {
    void MemTable::Put(const std::string &key, const std::string &value) {
        Apply(key, EntryType::kValue, value);
    }

    void MemTable::Delete(const std::string &key) {
        Apply(key, EntryType::kTombstone, std::string());
    }

    GetStatus MemTable::Get(const std::string &key, std::string *value) const {
        Entry entry;
        if (!list_.Find(key, &entry)) {
            return GetStatus::kNotFound;
        }

        if (entry.type == EntryType::kTombstone) {
            return GetStatus::kDeleted;
        }
        *value = entry.value;
        return GetStatus::kFound;
    }

    void MemTable::Apply(const std::string &key, EntryType type, const std::string &value) {
        Entry old;
        if (list_.Find(key, &old)) {
            approximate_size_ -= old.value.size();
        } else {
            approximate_size_ += key.size();
        }
        approximate_size_ += value.size();
        list_.Insert(key, Entry{type, value});
    }
}
