#include "InMemoryCache.hpp"

#include <chrono>
#include <iostream> // For debug logging

using namespace std::chrono;

// Constructor implementation
InMemoryCache::InMemoryCache(int default_ttl_seconds, size_t max_size)
    : default_ttl_seconds_(default_ttl_seconds), max_size_(max_size) {}

bool InMemoryCache::set(const std::string& key, const std::string& value, int ttl) {
    std::lock_guard<std::mutex> lock(mutex_);

    CacheEntry entry;
    entry.value = value;
    int effective_ttl = (ttl > 0) ? ttl : default_ttl_seconds_;
    entry.expiry = steady_clock::now() + seconds(effective_ttl);

    // --- LRU Logic ---
    // Check if key already exists
    auto lru_it = lru_map_.find(key);
    if (lru_it != lru_map_.end()) {
        // Key exists, remove it from its current position in the list
        lru_list_.erase(lru_it->second);
    } else {
        // Key doesn't exist, check if we need to evict before inserting
        evictIfNeeded();
    }

    // Add/update the entry in the main cache
    cache_[key] = entry;

    // Add key to the front (most recent) of the LRU list
    lru_list_.push_front(key);

    // Store the iterator to the new list element in the lru_map_
    lru_map_[key] = lru_list_.begin();
    // --- End LRU Logic ---

    return true;
}

std::optional<std::string> InMemoryCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_); // Lock should be held for the whole operation
    removeExpired(); // Clean up before getting

    // --- LRU Logic ---
    // Find the key in the LRU map first
    auto lru_it = lru_map_.find(key);
    if (lru_it != lru_map_.end()) {
        // Move accessed item to the front of the LRU list
        // Key found in LRU map, means it's potentially valid
        // Check expiry in the main cache_ map
        auto cache_it = cache_.find(key);
        if (cache_it != cache_.end() && cache_it->second.expiry > steady_clock::now()) {
            // Valid and not expired, move accessed item to the front of the LRU list
            lru_list_.splice(lru_list_.begin(), lru_list_, lru_it->second);
            // Return the value from the main cache
            return cache_it->second.value;
        } else {
            // Found in LRU map but expired or missing in cache_ (shouldn't happen if removeExpired is correct)
            // Clean up inconsistent state if necessary
            if (cache_it != cache_.end()) {
                 cache_.erase(cache_it); // Remove from main cache
            }
            lru_list_.erase(lru_it->second); // Remove from LRU list
            lru_map_.erase(lru_it);          // Remove from LRU map
        }
    }
    // --- End LRU Logic ---

    // Key not found or expired
    return std::nullopt;
}

bool InMemoryCache::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool removed = false;

    // --- LRU Logic ---
    auto lru_it = lru_map_.find(key);
    if (lru_it != lru_map_.end()) {
        lru_list_.erase(lru_it->second);
        lru_map_.erase(lru_it);
        removed = true;
    }
    // --- End LRU Logic ---

    // Also remove from the main cache map
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        cache_.erase(it);
        removed = true; // Ensure we report true if it existed in either map
    }
    // --- End LRU Logic ---
    return removed;
}

bool InMemoryCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    // --- LRU Logic ---
    lru_list_.clear();
    lru_map_.clear();
    // --- End LRU Logic ---
    return true;
}

bool InMemoryCache::exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    removeExpired();
    // Check existence in the main cache map and expiry
    auto it = cache_.find(key);
    return (it != cache_.end() && it->second.expiry > steady_clock::now());
}

// Private helper to remove expired items
void InMemoryCache::removeExpired() {
    // No lock needed here as it's called by public methods which already hold the lock
    auto now = steady_clock::now(); // Use steady_clock

    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (it->second.expiry <= now) {
            const std::string& key_to_remove = it->first;
            // --- LRU Logic ---
            auto lru_it = lru_map_.find(key_to_remove);
            if (lru_it != lru_map_.end()) {
                lru_list_.erase(lru_it->second);
                lru_map_.erase(lru_it);
            }
            // --- End LRU Logic ---
            it = cache_.erase(it);
            // --- End LRU Logic ---
        } else {
            ++it;
        }
    }
}

// Private helper to enforce size limit
void InMemoryCache::evictIfNeeded() {
    // No lock needed here as it's called by set() which already holds the lock
    while (lru_map_.size() >= max_size_ && !lru_list_.empty()) {
        std::string oldest_key = lru_list_.back(); // Get least recently used key
        lru_list_.pop_back();                     // Remove from list
        lru_map_.erase(oldest_key);               // Remove from map
        cache_.erase(oldest_key);                 // Remove from main cache
        // std::cout << "InMemoryCache evicted: " << oldest_key << std::endl; // Optional debug log
    }
}