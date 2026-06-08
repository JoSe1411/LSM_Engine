#include "WAL.h"
#include "MemTable.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

// Can handle files greater than 4GB.
uint32_t CalculateCRC32(const uint8_t *data, size_t len) {
  (void)data;
  (void)len;
  unsigned long crc = crc32(0L, Z_NULL, 0);
  size_t bytes_processed = 0;
  const size_t CHUNK_SIZE = 1024 * 1024 * 1024;
  while (bytes_processed < len) {
    size_t bytes_remaining = len - bytes_processed;
    uInt current_chunk_size =
        static_cast<uInt>(std::min(bytes_remaining, CHUNK_SIZE));
    crc = crc32(crc, reinterpret_cast<const Bytef *>(data + bytes_processed),
                current_chunk_size);
    bytes_processed += current_chunk_size;
  }
  return static_cast<uint32_t>(crc);
}

WAL::WAL(const std::string &filepath)
    : filepath_(filepath), fd_(-1), next_seq_num_(1), current_block_offset_(0) {
  (void)fd_; // Avoid unused field compiler warning in skeleton code
  (void)next_seq_num_;
  (void)current_block_offset_;
  struct stat file_status;
  fd_ = open(filepath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd_ == -1) {
    std::cerr << "Error opening file " << filepath << ": "
              << std::strerror(errno) << "\n";
  }
  if (fstat(fd_, &file_status) == -1) {
    std::cerr << "Error getting file size: " << std::strerror(errno)
              << std::endl;
  }
  current_block_offset_ = file_status.st_size % kBlockSize;
}

WAL::~WAL() {
  if (fd_ >= 0) {
    int success = close(fd_);
    if (success == -1) {
      std::cerr << "Error closing file descriptor: " << std::strerror(errno)
                << "\n";
    }
  }
}

bool WAL::Append(const std::string &key, const std::string &value,
                 bool is_tombstone) {
  uint32_t key_len = static_cast<uint32_t>(key.size());
  uint32_t val_len = static_cast<uint32_t>(value.size());
  uint8_t op_type = is_tombstone ? 0x02 : 0x01;
  size_t total_size = 8 + 1 + 4 + 4 + key_len + val_len;
  size_t offset = 0;
  std::vector<uint8_t> payload(total_size);
  memcpy(payload.data() + offset, &next_seq_num_, sizeof(next_seq_num_));
  offset += sizeof(next_seq_num_);
  memcpy(payload.data() + offset, &op_type, sizeof(op_type));
  offset += sizeof(op_type);
  memcpy(payload.data() + offset, &key_len, sizeof(key_len));
  offset += sizeof(key_len);
  memcpy(payload.data() + offset, &val_len, sizeof(val_len));
  offset += sizeof(val_len);
  memcpy(payload.data() + offset, key.data(), key_len);
  offset += key_len;
  memcpy(payload.data() + offset, value.data(), val_len);
  offset += val_len;
  size_t remaining_bytes = kBlockSize - current_block_offset_;
  if (remaining_bytes < kHeaderSize) {
    std::vector<uint8_t> zeros(remaining_bytes, 0);
    write(fd_, zeros.data(), remaining_bytes);
    current_block_offset_ = 0;
  }
  size_t payload_offset = 0;
  bool is_first_chunk = true;
  while (payload_offset < total_size) {
    uint8_t type = 0;
    size_t remaining_in_block = kBlockSize - current_block_offset_;
    size_t max_payload = remaining_in_block - kHeaderSize;
    size_t bytes_left = total_size - payload_offset;
    size_t chunk_payload_size = std::min(max_payload, bytes_left);
    if (is_first_chunk && chunk_payload_size == bytes_left) {
      type = kChunkTypeFull;
    } else if (is_first_chunk) {
      type = kChunkTypeFirst;
    } else if (chunk_payload_size == bytes_left) {
      type = kChunkTypeLast;
    } else {
      type = kChunkTypeMiddle;
    }
    size_t temp_header_offset = 0;
    std::vector<uint8_t> temp_header(17, 0);
    memset(temp_header.data() + temp_header_offset, type, sizeof(type));
    temp_header_offset += sizeof(type);
    memset(temp_header.data() + temp_header_offset, next_seq_num_,
           sizeof(next_seq_num_));
    temp_header_offset += sizeof(next_seq_num_);
    memset(temp_header.data() + temp_header_offset, chunk_payload_size,
           sizeof(chunk_payload_size));
    temp_header_offset += sizeof(chunk_payload_size);
    memset(temp_header.data() + temp_header_offset, total_size,
           sizeof(total_size));
    temp_header_offset += sizeof(total_size);

    std::vector<uint8_t> crc_payload = temp_header;
    crc_payload.insert(crc_payload.end(), payload.data() + payload_offset,
                       payload.data() + payload_offset + chunk_payload_size);
    uint32_t CRC_checksum = CalculateCRC32(
        crc_payload.data(), crc_payload.size() * sizeof(crc_payload[0]));
    write(fd_, &CRC_checksum, 4);
    write(fd_, temp_header.data(), 17);
    write(fd_, payload.data() + payload_offset, chunk_payload_size);
    payload_offset += chunk_payload_size;
    current_block_offset_ =
        (current_block_offset_ + kHeaderSize + chunk_payload_size) % kBlockSize;
    is_first_chunk = false;
  }
  if (payload_offset == total_size) {
    next_seq_num_++;
    return true;
  }
  return false;
}

bool WAL::Sync() {
  if (this->fd_ < 0) {
    return false;
  }
  int result = ::fsync(this->fd_);
  return (result == 0);
}

bool WAL::Recover(MemTable &memtable) {
  (void)memtable;

  // EXPLANATION & TODO: Implement the recovery state machine:
  //
  // 1. Open the WAL file descriptor for reading and potentially
  // writing/truncating:
  //    - Open filepath_ using POSIX open() with O_RDWR.
  //    - Keep track of the current physical read offset in the file.
  //
  // 2. Iterate block-by-block (32KB boundaries):
  //    - For each block, read until EOF or error.
  //    - Within a block, check for padding:
  //      * If the remaining space in the block is less than kHeaderSize (21
  //      bytes),
  //        it must be zero-padding. Skip these bytes and align read offset to
  //        the next block.
  //      * If the first byte read is 0x00, it's padding. Skip it and any
  //      subsequent padding bytes in the block.
  //
  // 3. Read the 21-byte header:
  //    Format: [CRC32 Checksum (4B)][Chunk Type (1B)][Sequence Number
  //    (8B)][Chunk Payload Length (4B)][Overall Record Payload Length (4B)]
  //    - If a read is incomplete (partial chunk header at EOF), this is a
  //    torn write.
  //      * Truncate the file at the start of this chunk header using
  //      ftruncate() and stop recovery.
  //
  // 4. Validate the Chunk:
  //    - Read the chunk payload (specified by Chunk Payload Length). If the
  //    read is partial, truncate the file and stop.
  //    - Compute the CRC32 checksum over the header suffix and the chunk
  //    payload.
  //    - Compare with the CRC32 Checksum from the header.
  //    - If the checksum is invalid, this indicates disk corruption:
  //      * Truncate the file at the start of this chunk header using
  //      ftruncate() to drop corrupted records.
  //      * Stop recovery and return true (recovering up to the point of
  //      corruption).
  //
  // 5. Recovery State Machine:
  //    Maintain a buffer to accumulate record fragments, and a boolean flag
  //    `in_fragmented_stream` (initially false).
  //    - If Chunk Type is kChunkTypeFull (0x01):
  //      * Sanity check: in_fragmented_stream must be false.
  //      * Directly decode the payload and apply to the MemTable.
  //    - If Chunk Type is kChunkTypeFirst (0x02):
  //      * Sanity check: in_fragmented_stream must be false.
  //      * Set in_fragmented_stream = true.
  //      * Clear fragment buffer and append this chunk's payload.
  //      * Record the Sequence Number and Overall Length for validation.
  //    - If Chunk Type is kChunkTypeMiddle (0x03):
  //      * Sanity check: in_fragmented_stream must be true.
  //      * Verify Sequence Number matches.
  //      * Append chunk payload to the fragment buffer.
  //    - If Chunk Type is kChunkTypeLast (0x04):
  //      * Sanity check: in_fragmented_stream must be true.
  //      * Verify Sequence Number matches.
  //      * Append chunk payload to the fragment buffer.
  //      * Verify total accumulated size matches Overall Record Payload
  //      Length.
  //      * Decode the fully assembled buffer and apply to the MemTable.
  //      * Reset in_fragmented_stream = false and clear the buffer.
  //    - If any sanity check or sequence number match fails:
  //      * Treat this as corruption: truncate at the start of the current
  //      chunk, and stop recovery.
  //
  // 6. Decode and apply complete records:
  //    - Record layout: [Sequence Number (8B)][Op Type (1B)][Key Length
  //    (4B)][Value Length (4B)][Key Bytes][Value Bytes]
  //    - Extract Key and Value.
  //    - If Op Type is 0x01 (PUT), call memtable.Put(key, value).
  //    - If Op Type is 0x02 (DELETE), call memtable.Delete(key).
  //    - Update next_seq_num_ = max(next_seq_num_, record_seq_num + 1).
  //
  // TODO: Implement the recovery state machine loop described above.

  return false;
}

bool WAL::Reset() {
  // EXPLANATION: Clear the WAL. Close the current fd_ if open. Then reopen
  // the file at 'filepath_' with the truncation flag (O_WRONLY | O_CREAT |
  // O_TRUNC) to truncate its length to 0, effectively clearing the log.
  // Ensure fd_ is updated with the new write descriptor and reset
  // current_block_offset_ = 0.
  //
  // TODO: Truncate or recreate the WAL file and reset offset.
  return false;
}
