#pragma once
#include <random>
#include <string>

enum class LookupStatus { Found, NotFound, Deleted };

struct LookupResults {
  LookupStatus status;
  std::string value;
};

class MemTable {
  struct Node;

public:
  // 1. Birth & Death
  explicit MemTable(int max_level = 16, float prob = 0.25f);
  ~MemTable();

  // Prevent copying
  MemTable(const MemTable &) = delete;
  MemTable &operator=(const MemTable &) = delete;

  // 2. Public API - The "Contract"
  void Put(const std::string &key, const std::string &value);
  LookupResults Get(const std::string &key) const;
  void Delete(const std::string &key);

  // 3. Size and Utility Helpers
  size_t ApproximateSize() const;
  void Clear();
  bool Empty() const;

  // 4. Iterator for Sequential Scans (Needed for SSTable flushing & testing)
  class Iterator {
  public:
    explicit Iterator(struct Node *node) : current_(node) {}
    bool Valid() const { return current_ != nullptr; }
    void Next();
    const std::string &key() const;
    const std::string &value() const;
    bool is_tombstone() const;

  private:
    struct Node *current_;
  };

  Iterator Begin() const;

private:
  struct Node {
    std::string key;
    std::string value;
    bool is_tombstone;
    int level;
    Node **forward;

    Node(std::string k, std::string v, bool tombstone, int lvl)
        : key(std::move(k)), value(std::move(v)), is_tombstone(tombstone),
          level(lvl) {
      forward = new Node *[lvl + 1];
      for (int i = 0; i <= lvl; ++i) {
        forward[i] = nullptr;
      }
    }
    ~Node() { delete[] forward; }
  };

  int randomLevel() const;
  void freeNodes();

  const int max_level_;
  const float p_;
  int current_level_;
  Node *head_;
  size_t approximate_size_bytes_;

  mutable std::mt19937 rng_;
  mutable std::uniform_real_distribution<float> dist_;
};
