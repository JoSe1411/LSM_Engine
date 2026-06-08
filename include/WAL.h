#pragma once
#include <string>
#include <cstdint>
#include <cstddef>

class MemTable;

class WAL {
public:
  explicit WAL(const std::string &filepath);
  ~WAL();
  WAL(const WAL &) = delete;
  WAL &operator=(const WAL &) = delete;

  // Appends a key-value record (or tombstone) to the log file in binary format.
  // Returns true on success, false on failure.
  bool Append(const std::string &key, const std::string &value,
              bool is_tombstone);

  // Forces all written data to be flushed and written to physical disk.
  // Returns true on success, false on failure.
  bool Sync();

  // Replays the log file sequentially to rebuild the in-memory MemTable state.
  // Returns true on success, false on failure.
  bool Recover(MemTable &memtable);

  // Clears/truncates the WAL file (typically called after flushing MemTable to
  // SSTable). Returns true on success, false on failure.
  bool Reset();

  // physical chunking types for leveldb-style WAL chunking
  static constexpr uint8_t kChunkTypePadding = 0x00;
  static constexpr uint8_t kChunkTypeFull = 0x01;
  static constexpr uint8_t kChunkTypeFirst = 0x02;
  static constexpr uint8_t kChunkTypeMiddle = 0x03;
  static constexpr uint8_t kChunkTypeLast = 0x04;

  static constexpr size_t kBlockSize = 32768; // 32KB
  static constexpr size_t kHeaderSize = 21;    // 21-byte header

private:
  std::string filepath_;
  int fd_; // Raw file descriptor for low-level POSIX systems programming calls
  uint64_t next_seq_num_;
  size_t current_block_offset_ = 0; // Track the write offset within the current 32KB block
};
