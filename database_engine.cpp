/*
 * ============================================================================
 * MINI DATABASE ENGINE (Implementation File)
 *
 * This file contains the "blueprints" for all the moving parts of our
 * tiny database. It's designed to show how a real database works
 * "under the hood," including:
 *
 * 1.  **Config**: The "settings menu" for our database.
 * 2.  **Record**: The "index card" that holds our data (e.g., key/value).
 * 3.  **Page**: The "piece of paper" on disk (a 4KB block) that holds records.
 * 4.  **JournalManager**: The "safety scribe" that logs all changes first.
 * 5.  **BufferPool**: The "short-term memory" (cache) to avoid slow disk reads.
 * 6.  **BPlusTreeIndex**: The "magic index" for finding data instantly.
 * 7.  **StorageEngine**: The "General Manager" who coordinates everything.
 * ============================================================================
 */

// ============================================================================
// SECTION 1: THE REQUIRED LIBRARIES (Our "Toolkit")
// ============================================================================
#include <iostream>  // For printing to the console (e.g., std::cout)
#include <fstream>   // For reading from and writing to files (e.g., std::fstream)
#include <string>    // For working with text (e.g., std::string)
#include <vector>    // For flexible lists of things (e.g., a node's keys)
#include <map>       // For mapping one thing to another (used for our cache)
#include <memory>    // For smart pointers (e.g., std::shared_ptr) to manage memory
#include <cstring>   // For low-level C-style memory functions (e.g., memcpy, memset)
#include <chrono>    // For timing how fast our operations are
#include <algorithm> // For helpful algorithms (e.g., std::lower_bound for B-Tree)
#include <sstream>   // For building strings
#include <iomanip>   // For formatting our output (e.g., std::setprecision)
#include <sys/stat.h>  // For checking file information (like file size)


// ============================================================================
// SECTION 2: THE "SETTINGS MENU" (Config Namespace)
//
// We define all our "magic numbers" in one place. This makes the database
// easy to tune and understand.
// ============================================================================
namespace Config {
    // How big is one "page" of data on disk? 4KB is a standard size.
    constexpr size_t PAGE_SIZE = 4096;

    // How many pages should we keep in our fast "short-term memory" (cache)?
    constexpr size_t CACHE_SIZE = 100;

    // How "bushy" should our B+ Tree index be? (Max children per node)
    constexpr size_t BTREE_ORDER = 64;

    // What are the file names we'll use?
    const std::string DATA_FILE = "database.dat";
    const std::string INDEX_FILE = "index.dat"; // Note: This index is in-memory for this demo
    const std::string JOURNAL_FILE = "journal.log"; // Our "safety log"

    // How big can a key/value be?
    constexpr size_t MAX_KEY_SIZE = 256;
    constexpr size_t MAX_VALUE_SIZE = 1024;
}


// ============================================================================
// SECTION 3: THE "INDEX CARD" (Record Struct)
//
// This is the actual piece of data we're saving. It's a simple form with
// a "key" (what we look up) and a "value" (what we get back).
// ============================================================================
struct Record {
    // The C-style char arrays are simple, fixed-size, and easy to
    // write to disk just by copying their bytes.
    char key[Config::MAX_KEY_SIZE];
    char value[Config::MAX_VALUE_SIZE];

    uint64_t page_id;   // Where does this record live on disk?
    bool is_deleted;    // A "soft delete" flag. Much faster than actually erasing.

    // Default constructor: create an empty, zeroed-out record
    Record() : page_id(0), is_deleted(false) {
        // `memset` fills a block of memory with a specific value (0 in this case)
        memset(key, 0, Config::MAX_KEY_SIZE);
        memset(value, 0, Config::MAX_VALUE_SIZE);
    }

    // Constructor to create a record from C++ strings
    Record(const std::string& k, const std::string& v, uint64_t pid = 0)
        : page_id(pid), is_deleted(false) {
        // `strncpy` copies the string, but safely (won't overflow)
        strncpy(key, k.c_str(), Config::MAX_KEY_SIZE - 1);
        strncpy(value, v.c_str(), Config::MAX_VALUE_SIZE - 1);
    }

    // Helper functions to get our C-style arrays back as C++ strings
    std::string get_key() const { return std::string(key); }
    std::string get_value() const { return std::string(value); }
};


// ============================================================================
// SECTION 4: THE "FILING CABINET PAGE" (Page Struct)
//
// This represents one 4KB chunk of space on the disk. It's the "container"
// for our "index card" (Record).
// ============================================================================
struct Page {
    uint64_t page_id;               // This page's "address" (e.g., Page #5)
    char data[Config::PAGE_SIZE]; // The raw 4KB of data
    bool is_dirty;                // A "sticky note" that says:
                                  // "This page has been changed but not saved to disk!"

    // Constructor creates an empty, zeroed-out page
    Page(uint64_t id = 0) : page_id(id), is_dirty(false) {
        memset(data, 0, Config::PAGE_SIZE);
    }

    // "Serializes" a Record struct into the page's raw data array
    void write_record(const Record& rec) {
        // `memcpy` copies the bytes of the Record object directly into our `data` array
        memcpy(data, &rec, sizeof(Record));
        is_dirty = true; // Mark it as needing to be saved!
    }

    // "Deserializes" the raw page data back into a Record struct
    Record read_record() const {
        Record rec;
        memcpy(&rec, data, sizeof(Record));
        return rec;
    }
};


// ============================================================================
// SECTION 5: THE "SAFETY SCRIBE" (JournalManager Class)
//
// This is the database's "safety net," providing crash recovery.
// It uses a technique called Write-Ahead Logging (WAL).
//
// RULE: *Never* make a change to the data file until you've *first*
//       written down what you're *about* to do in this log.
//
// If we crash, we just re-read this log to get back to a safe state.
// ============================================================================
class JournalManager {
private:
    std::fstream journal_file; // The log file itself

    // The different types of log entries we can write
    enum Operation { INSERT, UPDATE, DELETE, COMMIT };

    // The structure of a single entry in our log file
    struct JournalEntry {
        Operation op;
        char key[Config::MAX_KEY_SIZE];
        char value[Config::MAX_VALUE_SIZE]; // Only used for INSERT/UPDATE
        uint64_t page_id;                   // Only used for UPDATE/DELETE

        JournalEntry() : op(INSERT), page_id(0) {
            memset(key, 0, Config::MAX_KEY_SIZE);
            memset(value, 0, Config::MAX_VALUE_SIZE);
        }
    };

public:
    // When the manager is created, it opens (or creates) the log file
    JournalManager() {
        // Open the file for reading, writing, in binary mode, and appending
        journal_file.open(Config::JOURNAL_FILE,
            std::ios::in | std::ios::out | std::ios::binary | std::ios::app);

        // If it failed to open (e.g., doesn't exist), create it
        if (!journal_file.is_open()) {
            journal_file.open(Config::JOURNAL_FILE, std::ios::out | std::ios::binary);
            journal_file.close();
            // And then re-open it in the correct mode
            journal_file.open(Config::JOURNAL_FILE,
                std::ios::in | std::ios::out | std::ios::binary);
        }
    }

    // When the manager is destroyed, it closes the file
    ~JournalManager() {
        if (journal_file.is_open()) {
            journal_file.close();
        }
    }

    // This is the most important function: log what you're *about* to do
    void log_operation(const std::string& op_type, const std::string& key,
                       const std::string& value = "", uint64_t page_id = 0) {
        JournalEntry entry;

        if (op_type == "INSERT") entry.op = INSERT;
        else if (op_type == "UPDATE") entry.op = UPDATE;
        else if (op_type == "DELETE") entry.op = DELETE;
        else if (op_type == "COMMIT") entry.op = COMMIT;

        strncpy(entry.key, key.c_str(), Config::MAX_KEY_SIZE - 1);
        strncpy(entry.value, value.c_str(), Config::MAX_VALUE_SIZE - 1);
        entry.page_id = page_id;

        journal_file.seekp(0, std::ios::end); // Go to the end of the file
        journal_file.write(reinterpret_cast<char*>(&entry), sizeof(JournalEntry));
        journal_file.flush(); // This is CRITICAL: force the OS to write to disk NOW
    }

    // A special log entry that says "The last operation was successful."
    void commit() {
        log_operation("COMMIT", "");
    }

    // Wipes the log clean. We do this *after* all changes are safely
    // written to the main data file (this is called a "checkpoint").
    void truncate() {
        journal_file.close();
        std::remove(Config::JOURNAL_FILE.c_str()); // Delete the old log
        // Re-open it, which creates a new, empty file
        journal_file.open(Config::JOURNAL_FILE,
            std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    }
};


// ============================================================================
// SECTION 6: THE "SHORT-TERM MEMORY" (BufferPool Class)
//
// This is our cache. Reading from disk is *painfully* slow.
// Reading from memory (RAM) is *insanely* fast.
// This class keeps the 100 (from Config::CACHE_SIZE) most-used pages
// in RAM to speed things up.
//
// It uses an "LRU" (Least Recently Used) policy:
// When the cache is full, we kick out the page we haven't touched
// in the longest time.
// ============================================================================
class BufferPool {
private:
    // What we store in our cache
    struct CacheEntry {
        std::shared_ptr<Page> page; // A smart pointer to the Page object
        size_t access_time;         // A "timestamp" of when we last used this
    };

    // The cache itself: maps a Page ID (e.g., 5) to its CacheEntry
    std::map<uint64_t, CacheEntry> cache;
    size_t current_time; // A simple counter that acts as our "clock"

public:
    BufferPool() : current_time(0) {}

    // Get a page from the cache
    std::shared_ptr<Page> get(uint64_t page_id) {
        auto it = cache.find(page_id);
        if (it != cache.end()) {
            // FOUND IT!
            // Update its "timestamp" to now.
            it->second.access_time = ++current_time;
            return it->second.page;
        }
        // NOT FOUND. The "boss" (StorageEngine) will have to load it from disk.
        return nullptr;
    }

    // Add a new page to the cache
    void put(uint64_t page_id, std::shared_ptr<Page> page) {
        if (cache.size() >= Config::CACHE_SIZE) {
            // Cache is full! Kick someone out.
            evict_lru();
        }
        // Add the new page with the current time.
        cache[page_id] = {page, ++current_time};
    }

    // Find the "oldest" page and remove it
    void evict_lru() {
        // Start by assuming the first page is the oldest
        auto oldest = cache.begin();
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (it->second.access_time < oldest->second.access_time) {
                oldest = it; // Found one even older
            }
        }
        
        if (oldest != cache.end()) {
            // Note: If this page was "dirty", the StorageEngine is
            // responsible for flushing it to disk *before* evicting.
            // (Our simple demo flushes immediately, so this is safe).
            cache.erase(oldest);
        }
    }

    // Get a list of all pages that have been changed (are "dirty")
    std::vector<std::shared_ptr<Page>> get_dirty_pages() {
        std::vector<std::shared_ptr<Page>> dirty;
        for (auto& pair : cache) {
            if (pair.second.page->is_dirty) {
                dirty.push_back(pair.second.page);
            }
        }
        return dirty;
    }

    // Clears the entire cache
    void clear() {
        cache.clear();
    }
};


// ============================================================================
// SECTION 7: THE "LIGHTNING-FAST INDEX" (BPlusTreeIndex Class)
//
// This is the *magic* that makes the database fast.
//
// Analogy: Without this, finding "user:500" in a 1-million-record
// database would mean reading *every single record* (a "linear scan").
//
// This B+ Tree is like the "index" at the back of a giant textbook.
// It lets you find *any* key in a tiny number of steps,
// even if you have billions of them (O(log n) complexity).
// ============================================================================

// A "Node" in the B+ Tree. It can be:
// 1. A "Leaf" Node (at the bottom): Holds the actual keys and data (page_id).
// 2. An "Internal" Node (a branch): Holds keys that point to *other nodes*.
struct BPlusNode {
    bool is_leaf;
    std::vector<std::string> keys;
    std::vector<uint64_t> values;  // Page IDs (if leaf), child pointers (if internal)
    std::vector<std::shared_ptr<BPlusNode>> children; // Pointers to child nodes
    std::shared_ptr<BPlusNode> next; // (Leafs only) points to the *next* leaf node

    BPlusNode(bool leaf = true) : is_leaf(leaf), next(nullptr) {}

    // Finds the correct position for a key in this node's `keys` vector
    // `std::lower_bound` is a fast binary search
    size_t find_position(const std::string& key) const {
        return std::lower_bound(keys.begin(), keys.end(), key) - keys.begin();
    }
};

class BPlusTreeIndex {
private:
    std::shared_ptr<BPlusNode> root;
    size_t order; // Max children/values (from Config::BTREE_ORDER)

    // Splits a node that has become too full
    std::shared_ptr<BPlusNode> split_node(std::shared_ptr<BPlusNode> node) {
        auto new_node = std::make_shared<BPlusNode>(node->is_leaf);
        size_t mid = node->keys.size() / 2;

        if (node->is_leaf) {
            // Split a leaf node:
            // 1. Copy the right half of keys/values to the new node
            new_node->keys.assign(node->keys.begin() + mid, node->keys.end());
            new_node->values.assign(node->values.begin() + mid, node->values.end());
            // 2. Link the leaf nodes together (for range scans)
            new_node->next = node->next;
            node->next = new_node;
            
            // 3. Trim the left half (the original node)
            node->keys.resize(mid);
            node->values.resize(mid);
        } else {
            // Split an internal node:
            // 1. Copy right half of keys/children to new node
            // (Note: The *middle* key gets "promoted" up, so we skip it)
            new_node->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
            new_node->children.assign(node->children.begin() + mid + 1,
                                     node->children.end());

            // 2. Trim the left half
            node->keys.resize(mid);
            node->children.resize(mid + 1);
        }
        return new_node;
    }

    // The recursive "helper" function for inserting a key
    std::pair<std::shared_ptr<BPlusNode>, std::string>
    insert_internal(std::shared_ptr<BPlusNode> node, const std::string& key,
                    uint64_t value) {
        
        if (node->is_leaf) {
            // === We are at the bottom (a leaf node) ===
            size_t pos = node->find_position(key);

            // Check if key already exists (for UPDATE)
            if (pos < node->keys.size() && node->keys[pos] == key) {
                node->values[pos] = value;
                return std::make_pair(nullptr, ""); // No split, no promotion
            }

            // Insert new key-value pair in sorted order
            node->keys.insert(node->keys.begin() + pos, key);
            node->values.insert(node->values.begin() + pos, value);

            // Check if we're now too full
            if (node->keys.size() >= order) {
                // Yes: split this node
                auto new_node = split_node(node);
                // Return the new node and the *first key* in it,
                // which needs to be "promoted" to the parent.
                return std::make_pair(new_node, new_node->keys[0]);
            }
            return std::make_pair(nullptr, ""); // No split
        
        } else {
            // === We are at a branch (an internal node) ===
            // Find which child to dive into
            size_t pos = node->find_position(key);
            if (pos == node->keys.size()) pos = node->keys.size() - 1;
            else if (pos > 0 && key < node->keys[pos]) pos--;

            // Recursively call insert on that child
            auto result = insert_internal(node->children[pos], key, value);
            auto new_child = result.first;
            auto promoted_key = result.second;

            // If the child split...
            if (new_child) {
                // We need to add the "promoted key" and the new child node
                // into *this* internal node
                node->keys.insert(node->keys.begin() + pos + 1, promoted_key);
                node->children.insert(node->children.begin() + pos + 1, new_child);

                // Check if *this* node is now too full
                if (node->keys.size() >= order) {
                    auto new_node = split_node(node);
                    // The middle key of an internal node gets promoted
                    std::string mid_key = node->keys.back();
                    node->keys.pop_back();
                    return std::make_pair(new_node, mid_key);
                }
            }
            return std::make_pair(nullptr, ""); // No split
        }
    }

public:
    // Create an empty index with a single, empty leaf node
    BPlusTreeIndex(size_t tree_order = Config::BTREE_ORDER)
        : root(std::make_shared<BPlusNode>(true)), order(tree_order) {}

    // Public "insert" function
    void insert(const std::string& key, uint64_t page_id) {
        // Start the recursive insert from the root
        auto result = insert_internal(root, key, page_id);
        auto new_node = result.first;
        auto promoted_key = result.second;

        // If the *root itself* split...
        if (new_node) {
            // ...we need to create a new root!
            auto new_root = std::make_shared<BPlusNode>(false); // Not a leaf
            new_root->keys.push_back(promoted_key);
            new_root->children.push_back(root); // Old root is now a child
            new_root->children.push_back(new_node); // New node is other child
            root = new_root; // This is the new root
        }
    }

    // Public "search" function
    uint64_t search(const std::string& key) const {
        auto node = root;

        // 1. Traverse down the "branches" (internal nodes)
        while (!node->is_leaf) {
            size_t pos = node->find_position(key);
            if (pos == node->keys.size()) pos = node->keys.size() - 1;
            else if (pos > 0 && key < node->keys[pos]) pos--;
            node = node->children[pos];
        }

        // 2. We're at a leaf. Do a final search.
        size_t pos = node->find_position(key);
        if (pos < node->keys.size() && node->keys[pos] == key) {
            return node->values[pos]; // FOUND! Return the Page ID.
        }

        return 0; // Not found
    }

    // Public "remove" function
    void remove(const std::string& key) {
        // This is a simplified "tombstone" removal.
        // We find the key and just set its value to 0.
        // A full implementation would rebalance/merge nodes.
        auto node = root;

        // 1. Traverse down to the leaf
        while (!node->is_leaf) {
            size_t pos = node->find_position(key);
            if (pos == node->keys.size()) pos = node->keys.size() - 1;
            else if (pos > 0 && key < node->keys[pos]) pos--;
            node = node->children[pos];
        }

        // 2. Find the key in the leaf
        size_t pos = node->find_position(key);
        if (pos < node->keys.size() && node->keys[pos] == key) {
            node->values[pos] = 0; // Mark as deleted
        }
    }

    // Helper to get all keys (for debugging)
    std::vector<std::string> get_all_keys() const {
        std::vector<std::string> result;
        auto node = root;
        
        // Find the "leftmost" leaf (the start of the list)
        while (!node->is_leaf && !node->children.empty()) {
            node = node->children[0];
        }
        
        // "Walk" the linked list of leaf nodes
        while (node) {
            for (const auto& key : node->keys) {
                result.push_back(key);
            }
            node = node->next; // Move to the next leaf
        }
        return result;
    }
};


// ============================================================================
// SECTION 8: THE "GENERAL MANAGER" (StorageEngine Class)
//
// This is the "boss" who ties everything together. It's the public
// "face" of the database.
//
// When you call `insert()`, the StorageEngine coordinates:
// 1. The `JournalManager` (to log the operation)
// 2. The `BufferPool` (to get a page in memory)
// 3. The `Page`/`Record` (to write the data)
// 4. The `BPlusTreeIndex` (to index the new key)
// 5. The file system (to flush the page to disk)
// ============================================================================
class StorageEngine {
private:
    std::fstream data_file;     // The main "database.dat" file
    BufferPool buffer_pool;   // Our fast memory cache
    BPlusTreeIndex index;     // Our fast B+ Tree index
    JournalManager journal;   // Our safety log
    uint64_t next_page_id;    // A counter for new pages

    // Get a page: Checks cache first, if not, loads from disk.
    std::shared_ptr<Page> load_page(uint64_t page_id) {
        // 1. Check cache first (the fast path)
        auto cached = buffer_pool.get(page_id);
        if (cached) {
            return cached;
        }

        // 2. Not in cache. Load from disk (the slow path).
        auto page = std::make_shared<Page>(page_id);
        data_file.seekg(page_id * Config::PAGE_SIZE); // Seek to the right spot
        data_file.read(page->data, Config::PAGE_SIZE); // Read 4KB of data

        // 3. Put it in the cache for next time
        buffer_pool.put(page_id, page);
        return page;
    }

    // Writes a page's data to the disk
    void flush_page(std::shared_ptr<Page> page) {
        data_file.seekp(page->page_id * Config::PAGE_SIZE); // Seek to the spot
        data_file.write(page->data, Config::PAGE_SIZE); // Write 4KB
        data_file.flush(); // Force OS to write
        page->is_dirty = false; // The "sticky note" is removed
    }

    // Gets the ID for the next available page
    uint64_t allocate_page() {
        return next_page_id++;
    }

public:
    // Constructor: Open the data file and see how big it is
    StorageEngine() : next_page_id(1) {
        // Open (or create) the main data file
        data_file.open(Config::DATA_FILE,
            std::ios::in | std::ios::out | std::ios::binary);
        
        if (!data_file.is_open()) {
            data_file.open(Config::DATA_FILE, std::ios::out | std::ios::binary);
            data_file.close();
            data_file.open(Config::DATA_FILE,
                std::ios::in | std::ios::out | std::ios::binary);
        }

        // Get file size to figure out where the "end" is
        data_file.seekg(0, std::ios::end);
        size_t file_size = data_file.tellg();
        // If file is 12KB, we have 3 pages (4KB each).
        // The next page to allocate is Page #4 (index 3).
        // But we start IDs from 1, so...
        next_page_id = (file_size / Config::PAGE_SIZE) + 1;
        if (next_page_id == 0) next_page_id = 1;
    }

    // Destructor: Flush everything and close files
    ~StorageEngine() {
        flush_all();
        if (data_file.is_open()) {
            data_file.close();
        }
    }

    // === The 4 Core "CRUD" Operations ===

    // CREATE: Insert a new record
    bool insert(const std::string& key, const std::string& value) {
        // 1. Check if key already exists in the index
        if (index.search(key) != 0) {
            return false; // Key already exists, fail the insert
        }

        // 2. Log what we are *about* to do
        journal.log_operation("INSERT", key, value);

        // 3. Get a new page for this record
        uint64_t page_id = allocate_page();
        Record rec(key, value, page_id);
        
        auto page = std::make_shared<Page>(page_id);
        page->write_record(rec); // This marks the page as "dirty"

        // 4. Put page in cache and save it to disk
        buffer_pool.put(page_id, page);
        flush_page(page); // In our simple DB, we flush *immediately*

        // 5. Update the B+ Tree index to point to this new page
        index.insert(key, page_id);

        // 6. Log that the operation was successful
        journal.commit();
        return true;
    }

    // READ: Fetch a record by its key
    std::pair<bool, std::string> get(const std::string& key) {
        // 1. Ask the index where this key lives
        uint64_t page_id = index.search(key);
        if (page_id == 0) {
            return {false, ""}; // Not found (or was deleted)
        }

        // 2. Load the page (from cache or disk)
        auto page = load_page(page_id);
        Record rec = page->read_record();

        // 3. Check for "soft delete"
        if (rec.is_deleted) {
            return {false, ""}; // Found, but it's "deleted"
        }

        // 4. Success!
        return {true, rec.get_value()};
    }

    // UPDATE: Modify an existing record
    bool update(const std::string& key, const std::string& new_value) {
        // 1. Ask the index where this key lives
        uint64_t page_id = index.search(key);
        if (page_id == 0) {
            return false; // Not found
        }

        // 2. Log the "update" operation
        journal.log_operation("UPDATE", key, new_value, page_id);

        // 3. Load the page
        auto page = load_page(page_id);
        Record rec = page->read_record();

        // 4. Check if it's "deleted"
        if (rec.is_deleted) {
            return false; // Can't update a deleted record
        }

        // 5. Modify the record in memory
        strncpy(rec.value, new_value.c_str(), Config::MAX_VALUE_SIZE - 1);
        page->write_record(rec); // This marks page as "dirty"

        // 6. Save (flush) the page to disk
        flush_page(page);
        
        // 7. Log success
        journal.commit();
        return true;
    }

    // DELETE: Remove a record (by "soft delete")
    bool remove(const std::string& key) {
        // 1. Ask the index where this key lives
        uint64_t page_id = index.search(key);
        if (page_id == 0) {
            return false; // Not found
        }

        // 2. Log the "delete" operation
        journal.log_operation("DELETE", key, "", page_id);

        // 3. Load the page
        auto page = load_page(page_id);
        Record rec = page->read_record();
        rec.is_deleted = true; // The "soft delete"
        
        page->write_record(rec); // Mark as dirty

        // 4. Save the page
        flush_page(page);

        // 5. Remove the key from the index so we can't find it anymore
        index.remove(key); // This sets its page_id in the index to 0

        // 6. Log success
        journal.commit();
        return true;
    }

    // === Utility Functions ===

    // Saves all "dirty" pages to disk (a "checkpoint")
    void flush_all() {
        auto dirty_pages = buffer_pool.get_dirty_pages();
        for (auto& page : dirty_pages) {
            flush_page(page);
        }
        // *After* everything is safe on disk, we can clear the log
        journal.truncate();
    }

    // The "slow way" to search, for our benchmark.
    // This reads *every single page* from disk.
    std::pair<bool, std::string> linear_search(const std::string& key) {
        // Go to the end of the file to see how big it is
        data_file.seekg(0, std::ios::end);
        size_t file_size = data_file.tellg();
        size_t num_pages = file_size / Config::PAGE_SIZE;

        // Loop from the first page to the last
        for (uint64_t pid = 1; pid <= num_pages; ++pid) {
            // NOTE: We are *bypassing* the cache here!
            // We read directly from the file.
            data_file.seekg(pid * Config::PAGE_SIZE);
            
            char buffer[Config::PAGE_SIZE];
            data_file.read(buffer, Config::PAGE_SIZE);
            
            Record rec;
            memcpy(&rec, buffer, sizeof(Record));
            
            if (!rec.is_deleted && rec.get_key() == key) {
                return {true, rec.get_value()}; // Found it!
            }
        }
        return {false, ""}; // Not found
    }

    // Print out some statistics
    void print_stats() {
        data_file.seekg(0, std::ios::end);
        size_t file_size = data_file.tellg();
        size_t num_pages = file_size / Config::PAGE_SIZE;
        
        std::cout << "=== Database Statistics ===" << std::endl;
        std::cout << "File size: " << file_size << " bytes" << std::endl;
        std::cout << "Number of pages: " << num_pages << std::endl;
        std::cout << "Page size: " << Config::PAGE_SIZE << " bytes" << std::endl;
        std::cout << "Cache size: " << Config::CACHE_SIZE << " pages" << std::endl;
    }
};