#include "MemTable.h"
#include <string>
#include <vector>

// Constructor and Destructor
MemTable::MemTable(int max_level, float prob)
    : max_level_(max_level), p_(prob), current_level_(0), head_(nullptr),
      approximate_size_bytes_(0), rng_(std::random_device{}()),
      dist_(0.0f, 1.0f) {
  head_ = new Node("", "", false, max_level - 1);
}

MemTable::~MemTable() {
  freeNodes();
  delete (head_);
}

int MemTable::randomLevel() const {
  int lvl = 0;
  while (dist_(rng_) < p_ && lvl < max_level_ - 1) {
    lvl++;
  }
  return lvl;
}

void MemTable::freeNodes() {
  Node *curr = head_->forward[0];
  while (curr != nullptr) {
    Node *temp = curr->forward[0];
    delete (curr);
    curr = temp;
  }
  for (int i = 0; i < max_level_; i++) {
    head_->forward[i] = nullptr;
  }
}
// Core APIs
void MemTable::Put(const std::string &key, const std::string &value) {
  Node *curr = head_;
  std::vector<Node *> update(max_level_, nullptr);
  for (int i = current_level_; i >= 0; i--) {
    while (curr->forward[i] != nullptr && key > curr->forward[i]->key) {
      curr = curr->forward[i];
    }
    update[i] = curr;
  }
  if (curr->forward[0]->key == key) {
    curr = curr->forward[0];
    approximate_size_bytes_ -= curr->value.size();
    approximate_size_bytes_ += value.size();
    curr->value = value;
    curr->is_tombstone = false;
  } else {
    const int lvl = randomLevel();
    Node *new_node = new Node(key, value, false, lvl);
    if (lvl > current_level_) {
      for (int i = current_level_ + 1; i <= lvl; ++i) {
        update[i] = head_;
      }
      current_level_ = lvl;
      head_->forward[lvl] = new_node;
    }
    for (int i = 0; i <= lvl; i++) {
      Node *temp = update[i];
      Node *next_temp = temp->forward[i];
      temp->forward[i] = new_node;
      new_node->forward[i] = next_temp;
    }
    approximate_size_bytes_ += key.size() + value.size();
  }
}

LookupResults MemTable::Get(const std::string &key) const {
  Node *curr = head_;
  for (int i = current_level_; i >= 0; i--) {
    while (curr->forward[i] != nullptr && key > curr->forward[i]->key) {
      curr = curr->forward[i];
    }
  }
  if (curr->forward[0] == nullptr || curr->forward[0]->key != key) {
    return {LookupStatus::NotFound, ""};
  } else if (curr->forward[0] != nullptr && curr->forward[0]->key == key &&
             curr->forward[0]->is_tombstone == false) {
    return {LookupStatus::Found, curr->forward[0]->value};
  } else {
    return {LookupStatus::Deleted, ""};
  }
}

void MemTable::Delete(const std::string &key) {
  Node *curr = head_;
  std::vector<Node *> update(max_level_, nullptr);
  for (int i = current_level_; i >= 0; i--) {
    while (curr->forward[i] != nullptr && key > curr->forward[i]->key) {
      curr = curr->forward[i];
    }
    update[i] = curr;
  }
  if (curr->forward[0] != nullptr && curr->forward[0]->key == key) {
    curr = curr->forward[0];
    approximate_size_bytes_ -= curr->value.size();
    curr->value = "";
    curr->is_tombstone = true;
  } else {
    const int lvl = randomLevel();
    Node *new_node = new Node(key, "", true, lvl);
    if (lvl > current_level_) {
      for (int i = current_level_ + 1; i <= lvl; ++i) {
        update[i] = head_;
      }
      current_level_ = lvl;
    }
    for (int i = 0; i <= lvl; i++) {
      Node *temp = update[i];
      Node *next_temp = temp->forward[i];
      temp->forward[i] = new_node;
      new_node->forward[i] = next_temp;
    }
    approximate_size_bytes_ += key.size();
  }
}

// Iterator class utility functions
size_t MemTable::ApproximateSize() const { return approximate_size_bytes_; }

bool MemTable::Empty() const {
  if (head_->forward[0] != nullptr)
    return false;
  return true;
}

void MemTable::Clear() {
  freeNodes();
  current_level_ = 0;
  approximate_size_bytes_ = 0;
}

// Iterator Methods
void MemTable::Iterator::Next() {
  if (current_ != nullptr) {
    current_ = current_->forward[0];
  }
}

const std::string &MemTable::Iterator::key() const { return current_->key; }

const std::string &MemTable::Iterator::value() const { return current_->key; }

bool MemTable::Iterator::is_tombstone() const { return current_->is_tombstone; }

MemTable::Iterator MemTable::Begin() const {
  return Iterator(head_->forward[0]);
}
