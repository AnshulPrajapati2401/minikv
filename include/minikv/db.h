/*
 * DB: the front door of the engine.
 *
 * Ties the WAL (disk log) and the MemTable (memory) together.
 *   Put/Delete: append to the WAL first, then update the memtable,
 *               so a crash can never lose an acknowledged write.
 *   Get:        read from the memtable.
 *   Open:       creates the folder, replays the WAL into a fresh
 *               memtable (crash recovery), then opens the log for writing.
 *
 * Options holds settings (sync_on_write = fsync after every write).
 * DB cannot be copied; create it only through DB::Open.
 */

#pragma once

#include <memory>
#include <string>

#include "minikv/wal.h"
#include "minikv/memtable.h"

namespace minikv {
    int version() ;

    struct Options {
        bool sync_on_write = false ;
    };

    class DB {
        public :
            static std::unique_ptr<DB> Open(const std::string &dir, const Options &options = Options());

            DB(const DB&) = delete;
            DB& operator=(const DB&) = delete;

            bool Put(const std::string &key, const std::string &value);
            bool Delete(const std::string &key);
            bool Get(const std::string &key, std::string *value) const;

        private :
             DB(std::string dir, Options options);

             bool Recover();

             std::string dir_;
             Options options_;
             MemTable mem_;
             std::unique_ptr<WalWriter> wal_;
    };
}
