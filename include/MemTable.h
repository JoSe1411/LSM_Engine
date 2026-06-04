#pragma once
#include<string>
#include<random>

enum class LookupStatus{
    Found,
    NotFound,
    Deleted
};

struct LookupResults{
    LookupStatus status;
    std::string value;
};

class MemTable{
    private:
    struct Node{
        std::string key;
        std::string value;
        bool is_tombstone;
        int level;
        Node** array_next_node;

        Node(std::string key, std::string value, bool is_tombstone, int level) : key(key), value(value), is_tombstone(is_tombstone), level(level){
             array_next_node = new Node*[level+1];
             for(int i =0 ; i<level; i++){
                array_next_node[i] = nullptr;
             }
        }   
        ~Node(){
            delete array_next_node;
        }    
    };
    int randomLevel() const;
    void freeNodes();
    const int max_level_;
    const float p_;
    int current_level_;
    Node* head_;
    size_t approximate_size_bytes_;

    // Thread-safe Random Number Generator
    mutable std::mt19937 rng_;
    mutable std::uniform_real_distribution<float> dist_;


    public:
    MemTable(int max_level = 16,float prob = 0.25f);
    ~MemTable();
    MemTable(const MemTable&) = delete;
    MemTable& operator = (const MemTable&) = delete;
    void Put(std::string& key, const std::string& value);
    void Get(std::string& key);
    void Delete(std::string& key);
    size_t ApproximateSize() const;
    void Clear();
    bool Empty() const;
};