# Technical Handover: LSM-Tree Key-Value Storage Engine

## 1. Project Vision & Core Objectives

**Project:** An embedded, transactional Key-Value Storage Engine based on a Log-Structured Merge-tree (LSM-Tree) architecture. 

**Objective:** To design an infrastructure component that breaks through the random disk I/O bottleneck by converting random writes into high-speed sequential writes, while maintaining fast point-lookups. This engine will run in-process as a library, managing raw binary data files directly on the local file system.

**Exposed API:**
* `Put(Key, Value)`: Writes or updates a key-value pair.
* `Get(Key)`: Retrieves the latest value for a key or returns a "Not Found" error.
* `Delete(Key)`: Logically removes a key via a tombstone record.

---

## 2. Technical Prerequisites & Environment Stack

To build this from first principles and control the exact memory layout, you must operate close to the operating system. High-level, garbage-collected languages will abstract away the mechanics you are trying to learn.

### **Language & Tooling**
* **Primary Language:** **C++** (C++17 or newer) is the industry standard for this tier of infrastructure. It provides precise control over struct padding, byte alignment, and memory lifecycle. 
* **Alternative Language:** **Rust**, if you prefer strict compile-time memory safety while maintaining bare-metal performance.
* **Build System:** **CMake** or a simple **Makefile**. Keep the build pipeline minimal.
* **Testing Framework:** **Google Test (GTest)** or **Catch2**. You will need to write rigorous unit tests for your binary serialization and byte-offset math before touching actual disk files.

### **Target Environment & OS APIs**
Develop this targeting a **Linux / POSIX-compliant environment**. You will need to bypass standard I/O streams (like `std::ofstream`) and interface directly with system calls:
* **`<fcntl.h>` & `<unistd.h>`:** For raw file descriptors (`open`, `close`, `read`, `write`).
* **`fsync()` / `fdatasync()`:** Critical for the Write-Ahead Log (WAL) to flush the OS page cache and force physical disk writes.
* **`<sys/mman.h>`:** For `mmap()` and `munmap()`. Memory-mapping your immutable SSTable files will allow the OS to handle paging data into RAM transparently during lookups.

### **External Dependencies**
Keep dependencies to an absolute zero for the core engine, with one exception:
* **Hashing:** You will need a fast, non-cryptographic hash function for your Bloom Filters. Drop a single-file implementation of **MurmurHash3** or **xxHash** into your source tree. Do not use heavy cryptographic hashes like SHA-256; they are too slow for an infrastructure data path.

---

## 3. Phased Architecture & Implementation Roadmap

Implement the engine in strict, testable increments. Do not move to the next phase until the current phase has passing unit tests for byte-level integrity.

### **Phase 1: The In-Memory Layer & Durability**
Before anything touches long-term disk files, the database must process incoming writes in RAM safely and durably.

1.  **Implement the MemTable:** Build a sorted data structure (such as a custom SkipList or a self-balancing tree). It must automatically keep keys in lexicographical order as they are inserted.
2.  **Implement the Write-Ahead Log (WAL):** * Open a file in append-only mode. 
    * Format: Write the `[Key Length][Value Length][Key Bytes][Value Bytes][Tombstone Flag]`.
    * Execution: Append the bytes and immediately call `fsync()`. Only after `fsync()` returns success should you insert the data into the MemTable.
3.  **Crash Recovery Protocol:** Write a startup routine that sequentially scans the WAL file, decodes the raw bytes, and repopulates the MemTable into memory upon instantiation.

### **Phase 2: On-Disk Serialization (SSTables)**
When the MemTable reaches a predefined size limit (e.g., 4MB), it must freeze, block incoming writes temporarily, and flush its sorted data to disk as an immutable **Sorted String Table (SSTable)**.

1.  **Define the SSTable Binary Layout:** Design a custom, tight binary file format.
    * **Data Block:** Continuous sequence of serialized `[Key Length][Value Length][Key Bytes][Value Bytes]`.
    * **Index Block:** Stored at the end of the file. Contains pairs of `[Key][Byte Offset]`.
    * **Footer:** A fixed-size trailing segment (e.g., last 8 bytes) holding a 64-bit integer pointing to the start of the Index Block.
2.  **Implement the Flush Logic:** Iterate sequentially through the frozen MemTable. Write all elements into the Data Block. Periodically record the byte offset of every $N$-th key (e.g., every 16th key) to build a **Sparse Index**. Write the Sparse Index blocks, followed by the Footer pointer. Finally, truncate the WAL file.
3.  **Implement the SSTable Reader:** Write a component that opens an SSTable file, seeks directly to the last 8 bytes to find the index pointer, loads the Sparse Index into memory, and performs a binary search to locate the exact byte range where a key lives.

### **Phase 3: Lookup Optimization (Indexes & Bloom Filters)**
Optimize the read path to prevent full disk scans for keys that do not exist.

1.  **Implement a Bloom Filter:** For each SSTable flushed to disk, allocate a bit-array. Run every key through your chosen hash function (with different seeds to simulate multiple functions) and set the corresponding bits.
2.  **Serialize the Filter:** Store this bit-array immediately preceding the Index Block in your SSTable file.
3.  **Update the Lookup Path (`Get` Flow):**
    * **Step A:** Search the active MemTable. If found, return the value.
    * **Step B:** If not found, iterate through SSTables from newest to oldest.
    * **Step C:** Check the Bloom Filter first. If `false`, skip the file. If `true`, read the sparse index, seek to the block offset via direct I/O or `mmap`, and scan for the key.

### **Phase 4: Space Reclamation (Compaction)**
SSTables are immutable. Updates and deletions will leave obsolete records spanning multiple files. Compaction is the background process that merges files, reclaims disk space, and restores read performance.

1.  **Implement a K-Way Merge Sort Engine:** Use a **Min-Heap (Priority Queue)**.
2.  **Cursor Initialization:** Open multiple SSTable files concurrently. Assign a byte-cursor to the beginning of each file's Data Block. Push the first key-value pair of each file into the priority queue.
3.  **Consolidation & Purging:** * Pop the smallest key from the queue and write it to a new, larger SSTable file.
    * *Conflict Resolution:* If the same key exists in multiple files, compare their sequence numbers or file ages. Keep the newest version and discard the older bytes.
    * *Tombstone Purging:* If a key is marked as deleted and you are compacting the oldest logical level of the database, drop the record entirely to free up the physical bytes.
4.  **Cursor Advancement:** Advance the cursor of the file that just yielded the smallest key, read its next record, and push it into the queue. Repeat until all source files are exhausted, then execute a safe atomic delete on the old files.
