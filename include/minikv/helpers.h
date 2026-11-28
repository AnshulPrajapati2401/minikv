/*
 * Shared low-level helpers: little-endian integer encoding, a
 * "write every byte" loop, and CRC32. Used by the WAL and SSTable.
 */
#pragma once

#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>

namespace minikv {

inline void PutFixed32(std::string* dst, uint32_t value) {
  char buf[4];
  for (int i = 0; i < 4; ++i) {
    buf[i] = static_cast<char>((value >> (8 * i)) & 0xFF);
  }
  dst->append(buf, 4);
}

inline void PutFixed64(std::string* dst, uint64_t value) {
  char buf[8];
  for (int i = 0; i < 8; ++i) {
    buf[i] = static_cast<char>((value >> (8 * i)) & 0xFF);
  }
  dst->append(buf, 8);
}

inline uint32_t DecodeFixed32(const char* p) {
  const unsigned char* u = reinterpret_cast<const unsigned char*>(p);
  uint32_t result = 0;
  for (int i = 3; i >= 0; --i) {
    result = (result << 8) | u[i];
  }
  return result;
}

inline uint64_t DecodeFixed64(const char* p) {
  const unsigned char* u = reinterpret_cast<const unsigned char*>(p);
  uint64_t result = 0;
  for (int i = 7; i >= 0; --i) {
    result = (result << 8) | u[i];
  }
  return result;
}

inline bool WriteAll(int fd, const char* data, size_t size) {
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

inline uint32_t Crc32(const char* data, size_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= static_cast<unsigned char>(data[i]);
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

}  // namespace minikv
