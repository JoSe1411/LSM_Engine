#include "MemTable.h"
#include "WAL.h"
#include <iostream>
#include <cassert>

void TestMemTableBasic() {
  MemTable memtable;
  assert(memtable.Empty());
  assert(memtable.ApproximateSize() == 0);

  // Put some keys
  memtable.Put("key1", "value1");
  assert(!memtable.Empty());
  assert(memtable.ApproximateSize() > 0);

  // Get checks
  LookupResults res = memtable.Get("key1");
  assert(res.status == LookupStatus::Found);
  assert(res.value == "value1");

  // Deletion checks
  memtable.Delete("key1");
  res = memtable.Get("key1");
  assert(res.status == LookupStatus::Deleted);

  // Clear checks
  memtable.Clear();
  assert(memtable.Empty());
  assert(memtable.ApproximateSize() == 0);
  std::cout << "MemTable basic tests passed!" << std::endl;
}

void TestWALCompilation() {
  // Test instantiation and skeleton API compilation
  WAL wal("test_wal.log");
  
  // Call skeleton methods to verify function signatures compile
  bool append_ok = wal.Append("key", "value", false);
  assert(!append_ok); // Skeleton returns false by default

  bool sync_ok = wal.Sync();
  assert(!sync_ok); // Skeleton returns false by default

  MemTable temp_memtable;
  bool recover_ok = wal.Recover(temp_memtable);
  assert(!recover_ok); // Skeleton returns false by default

  bool reset_ok = wal.Reset();
  assert(!reset_ok); // Skeleton returns false by default

  std::cout << "WAL compilation and signature tests passed!" << std::endl;
}

int main() {
  TestMemTableBasic();
  TestWALCompilation();
  std::cout << "All tests passed!" << std::endl;
  return 0;
}
