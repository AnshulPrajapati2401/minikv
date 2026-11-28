/*
 * GetStatus: the answer of a lookup, shared by MemTable and SSTable.
 * kFound = value returned, kDeleted = tombstone (stop searching older data),
 * kNotFound = keep searching, kError = I/O error or corrupt data.
 */
#pragma once
namespace minikv {
enum class GetStatus { kFound, kDeleted, kNotFound, kError };
}  // namespace minikv
