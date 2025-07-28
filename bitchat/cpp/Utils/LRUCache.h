#pragma once

#include <list>
#include <unordered_map>

template <typename Key, typename Value>
class LRUCache {
public:
    LRUCache(size_t capacity) : capacity(capacity) {}

    void put(const Key& key, const Value& value) {
        auto it = map.find(key);
        if (it != map.end()) {
            list.erase(it->second);
        }
        list.push_front({key, value});
        map[key] = list.begin();
        if (map.size() > capacity) {
            map.erase(list.back().first);
            list.pop_back();
        }
    }

    Value get(const Key& key) {
        auto it = map.find(key);
        if (it == map.end()) {
            return Value();
        }
        list.splice(list.begin(), list, it->second);
        return it->second->second;
    }

private:
    size_t capacity;
    std::list<std::pair<Key, Value>> list;
    std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> map;
};
