/*
 * DB implementation.
 *
 * Open():    create the directory, build the DB, run Recover().
 * Recover(): if wal.log exists, read it record by record and apply each
 *            Put/Delete to the memtable. Stop at end of file or at the
 *            first corrupt record. If the tail is corrupt, cut the file
 *            back to the last good record so new writes are not hidden
 *            behind the damage. Then open the WAL writer.
 * Put/Delete: WAL append first, memtable second (order matters).
 * Get:       true only if the memtable says kFound.
 */


#include "minikv/db.h"
#include "minikv/wal.h"

#include <filesystem>
#include <system_error>
#include <utility>

namespace minikv {
    int version() { return 1; }

    DB::DB (std::string dir , Options options)
        : dir_(std::move(dir)), options_(options) {}

    std::unique_ptr<DB> DB::Open(const std::string& dir, const Options& options) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        if (ec || !std::filesystem::is_directory(dir, ec)) {
            return nullptr;
        }

        std::unique_ptr<DB> db(new DB(dir, options));
        if (!db->Recover()) {
          return nullptr;
        }
        return db;
    }

    bool DB::Recover(){
        const std::string path = dir_ + "/wal.log";
        std::error_code ec;

        if (std::filesystem::exists(path, ec)) {
            auto reader = WalReader::Open(path);
            if (reader == nullptr) {
                return false;
            }
            WalRecord record ;
            ReadStatus status = ReadStatus::kEof;
            while ((status = reader->Next(&record)) == ReadStatus::kOk) {
                if (record.type == WalRecordType::kPut) {
                    mem_.Put(record.key, record.value);
                } else {
                    mem_.Delete(record.key);
                }
            }

            if (status == ReadStatus::kCorrupt) {
                std::filesystem::resize_file(path, reader->GoodOffset(), ec);
                if (ec) {
                    return false;
                }
            }
        }
        wal_ = WalWriter::Open(path, options_.sync_on_write);
        return wal_ != nullptr ;
    }

    bool DB::Put(const std::string &key, const std::string &value){
        if (!wal_->Append(WalRecord{WalRecordType::kPut, key, value})) {
            return false;
        }
        mem_.Put(key,value) ;
        return true ;
    }

    bool DB::Delete(const std::string& key) {
      if (!wal_->Append(WalRecord{WalRecordType::kDelete, key, std::string()})) {
        return false;
      }
      mem_.Delete(key);
      return true;
    }

    bool DB::Get(const std::string& key, std::string* value) const {
      return mem_.Get(key, value) == GetStatus::kFound;
    }
} // namespace minikv
