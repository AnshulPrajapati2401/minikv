// skiplist.h
// What it does in one line
// It is a sorted container in memory.
// You give it key-value pairs in any order, and it keeps them sorted and lets you find any key quickly.
// Why we need it first
//
// Remember the write path: Put goes to the log (for safety) and then to the memtable (for speed).
// The memtable needs to do two things:
// Insert and find keys fast.
// Keep keys sorted, because when memory fills up we write the data to disk as a sorted file (SSTable).
// Sorted data makes disk files easy to search and merge.
// The skip list does both, so the memtable is built on top of it. Nothing else in the project works without it.


// skip list : refer to this - https://www.geeksforgeeks.org/dsa/skip-list/

#pragma once

#include <cstddef>   // gives us size_t (an unsigned whole number type used for sizes/counts)
#include <random>
#include <vector>

namespace minikv {

template <typename Key, typename Value>
class SkipList {
    public:
    SkipList()
      : head_(new Node(Key{}, Value{}, kMaxHeight)),
        // new Node(...) builds a Node in memory and returns its address (a Node*).
        // Key{} and Value{} mean "an empty/default key and value" (0 for int, "" for string).
        // The dummy head gets kMaxHeight arrows, so it can start a search on any level

        height_(1),   // currently only level 0 is in use, so height is 1
        size_(0),     // no keys stored yet
        rng_(std::random_device{}()) {}

    ~SkipList() {
        Node* cur = head_;
        while (cur != nullptr) {
            Node* next = cur->next[0];
            delete cur;
            cur = next;
        }
    }

    SkipList(const SkipList&) = delete; // COPY CONSTRUCTOR, deleted: "SkipList b = a;" will not compile.
    SkipList& operator=(const SkipList&) = delete; // COPY ASSIGNMENT OPERATOR, deleted: "b = a;" will not compile

    size_t Size() const { return size_ ; }

    bool Find (const Key &key , Value *out) const{
        Node * cur = head_ ;
        for (int level = height_ - 1 ; level >= 0 ; --level){
            while (cur->next[level] != nullptr && cur->next[level]->key < key){
                cur = cur->next[level] ;
            }
        }
        Node *candidate = cur->next[0] ;

        if (candidate != nullptr && candidate->key == key){
            *out = candidate->value ;
            return true ;
        }
        return false ;
    }

    bool Insert(const Key& key, const Value& value) {
        std::vector<Node*> update(kMaxHeight, head_);
        Node* cur = head_;

        for (int level = height_ - 1; level >= 0; --level) {
             while (cur->next[level] != nullptr && cur->next[level]->key < key) {
               cur = cur->next[level];
             }
             update[level] = cur;
        }
        Node *candidate = cur->next[0];
        if (candidate != nullptr && candidate->key == key) {
           candidate->value = value;
           return false;
        }

        int new_height = RandomHeight();
        if (new_height > height_) {
          height_ = new_height;
        }

        Node *node = new Node(key, value, new_height);
        for (int level = 0; level < new_height; ++level) {
            node->next[level] = update[level]->next[level];  // new node points ahead
            update[level]->next[level] = node;               // predecessor points to new node
        }
        ++size_ ;
        return true ;
    }

    private:
        static constexpr int kMaxHeight = 12;
        // A constant known at compile time. static = shared by all SkipLists.
        // constexpr = computed by the compiler, never changes. No node will ever be taller than 12 levels.

        struct Node {
            Key key;
            Value value;
            std::vector<Node*> next;   // a list of pointers (addresses) to other Nodes.

            Node(const Key& k, const Value& v, int height)
                : key(k), value(v), next(height, nullptr) {}
                // make a vector of `height` entries, all nullptr.
        };

        int RandomHeight(){
            int h = 1 ;
            std::uniform_int_distribution<int> coin (0,1) ;
            while(h < kMaxHeight && coin(rng_) == 1) { ++h ; }
            return h ;
        }

        Node* head_;          // address of the dummy first node (the starting gate)
        int height_;
        size_t size_;
        std::mt19937 rng_;
};

}
