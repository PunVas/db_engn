/*
 * ============================================================================
 * MINI DATABASE ENGINE (Test & Demo File)
 *
 * This program is the "test drive" for our database engine.
 *
 * It doesn't contain any of the database logic itself. Instead, it
 * #includes the engine and then uses it just like a real application
 * would.
 *
 * The test is broken into 4 parts:
 * 1.  **Basic CRUD**: A "sanity check" to make sure Create, Read, Update,
 * and Delete work correctly.
 * 2.  **Bulk Insert**: A "stress test" to see how fast we can add a lot
 * of data.
 * 3.  **Performance Test**: The "Aha!" moment. We prove *why* the B+ Tree
 * index is so important by comparing its speed to the "slow way."
 * 4.  **Statistics**: A final report on the state of our database files.
 * ============================================================================
 */

// We just include the entire engine file.
// In a "real" project, this would be a .h header file,
// but for a single-file engine, this works perfectly.
#include "database_engine.cpp"

// We also need iostream, chrono, and iomanip for our test output
#include <iostream>
#include <chrono>    // For timing things (benchmarking)
#include <iomanip>   // For formatting our output (e.g., setprecision)

// ============================================================================
// SECTION 1: The Main Program (Our Test Suite)
// ============================================================================
int main() {
    // Print a nice welcome banner
    std::cout << "╔══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║    MINI DATABASE ENGINE - C++ Implementation         ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;

    // 1. "Boot up" the database engine.
    // This creates the StorageEngine object, which opens/creates
    // the .dat and .log files on disk.
    StorageEngine db;

    // ========================================================================
    // PART 1: Basic CRUD Operations (The Sanity Check)
    //
    // Let's just make sure the basic C-R-U-D functions work as expected.
    // ========================================================================
    std::cout << "► PART 1: CRUD Operations Demo" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

    // --- CREATE ---
    std::cout << "\n[CREATE] Inserting records..." << std::endl;
    db.insert("user:1001", "Alice Johnson");
    db.insert("user:1002", "Bob Smith");
    db.insert("user:1003", "Charlie Brown");
    db.insert("product:5001", "Laptop - $1299");
    db.insert("product:5002", "Mouse - $29");
    std::cout << "✓ Inserted 5 records" << std::endl;

    // --- READ ---
    std::cout << "\n[READ] Fetching records..." << std::endl;
    // Test a key we know exists
    auto result1 = db.get("user:1001");
    std::cout << (result1.first ? "✓ " : "✗ ") << "user:1001 = "
              << (result1.first ? result1.second : "NOT FOUND") << std::endl;

    // Test another key we know exists
    auto result2 = db.get("product:5001");
    std::cout << (result2.first ? "✓ " : "✗ ") << "product:5001 = "
              << (result2.first ? result2.second : "NOT FOUND") << std::endl;

    // Test a key that *does not* exist
    auto result3 = db.get("user:9999");
    std::cout << (result3.first ? "✓ " : "✗ ") << "user:9999 = "
              << (result3.first ? result3.second : "NOT FOUND") << std::endl;

    // --- UPDATE ---
    std::cout << "\n[UPDATE] Modifying records..." << std::endl;
    bool updated = db.update("user:1002", "Bob Smith (Updated)");
    std::cout << (updated ? "✓" : "✗") << " Updated user:1002" << std::endl;

    // Let's read it back to be sure
    auto result4 = db.get("user:1002");
    std::cout << "  New value: " << result4.second << std::endl;

    // --- DELETE ---
    std::cout << "\n[DELETE] Removing records..." << std::endl;
    bool deleted = db.remove("product:5002");
    std::cout << (deleted ? "✓" : "✗") << " Deleted product:5002" << std::endl;

    // Now, let's try to get it. It should fail.
    auto result5 = db.get("product:5002");
    std::cout << (result5.first ? "✗" : "✓") << " Verified deletion (should not be found)"
              << std::endl;

    // ========================================================================
    // PART 2: Bulk Insert (The Stress Test)
    //
    // Let's hammer the database with a lot of data and see
    // how long it takes.
    // ========================================================================
    std::cout << "\n\n► PART 2: Bulk Insert for Performance Testing" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

    const int BULK_SIZE = 10000;
    std::cout << "\nInserting " << BULK_SIZE << " records..." << std::flush;

    // Get the time *before* we start
    auto start_insert = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < BULK_SIZE; ++i) {
        std::string key = "bench:" + std::to_string(i);
        std::string value = "Data_" + std::to_string(i * 1000);
        db.insert(key, value);

        // Print a little "progress dot" every 2000 inserts
        if ((i + 1) % 2000 == 0) {
            std::cout << "." << std::flush;
        }
    }

    // Get the time *after* we finish
    auto end_insert = std::chrono::high_resolution_clock::now();
    // Calculate the difference in milliseconds
    auto insert_duration = std::chrono::duration_cast<std::chrono::milliseconds>
                           (end_insert - start_insert).count();

    std::cout << "\n✓ Completed in " << insert_duration << " ms" << std::endl;
    std::cout << "  Throughput: " << (BULK_SIZE * 1000.0 / insert_duration)
              << " inserts/sec" << std::endl;

    // Make sure all data is saved to disk before we continue
    db.flush_all();

    // ========================================================================
    // PART 3: Performance Comparison (The "Aha!" Moment)
    //
    // This is the most important test. We will search for a few keys
    // twice:
    // 1. Using the B+ Tree index (db.get)
    // 2. Using the "slow way" (db.linear_search)
    // ...and then we'll see how much faster the index was.
    // ========================================================================
    std::cout << "\n\n► PART 3: Search Performance Comparison" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

    // We'll search for keys at the start, middle, and end of our benchmark data
    std::vector<std::string> test_keys = {
        "bench:100",  "bench:2500", "bench:5000",
        "bench:7500", "bench:9999", "user:1001" // one from our first test
    };

    // --- A) The FAST Way (Indexed Search) ---
    std::cout << "\n[INDEXED SEARCH] Using B+ Tree index..." << std::endl;
    auto start_indexed = std::chrono::high_resolution_clock::now();

    int indexed_found = 0;
    for (const auto& key : test_keys) {
        auto result = db.get(key);
        if (result.first) indexed_found++;
    }

    auto end_indexed = std::chrono::high_resolution_clock::now();
    // We use *microseconds* here because this will be EXTREMELY fast
    auto indexed_duration = std::chrono::duration_cast<std::chrono::microseconds>
                            (end_indexed - start_indexed).count();

    std::cout << "✓ Found " << indexed_found << "/" << test_keys.size()
              << " keys" << std::endl;
    std::cout << "  Time: " << indexed_duration << " μs (microseconds)" << std::endl;
    std::cout << "  Avg: " << (indexed_duration / test_keys.size())
              << " μs per lookup" << std::endl;

    // --- B) The SLOW Way (Linear Scan) ---
    std::cout << "\n[LINEAR SEARCH] Scanning entire file..." << std::endl;
    auto start_linear = std::chrono::high_resolution_clock::now();

    int linear_found = 0;
    for (const auto& key : test_keys) {
        auto result = db.linear_search(key);
        if (result.first) linear_found++;
    }

    auto end_linear = std::chrono::high_resolution_clock::now();
    auto linear_duration = std::chrono::duration_cast<std::chrono::microseconds>
                           (end_linear - start_linear).count();

    std::cout << "✓ Found " << linear_found << "/" << test_keys.size()
              << " keys" << std::endl;
    std::cout << "  Time: " << linear_duration << " μs (microseconds)" << std::endl;
    std::cout << "  Avg: " << (linear_duration / test_keys.size())
              << " μs per lookup" << std::endl;

    // --- C) The Final Comparison ---
    std::cout << "\n[SPEEDUP ANALYSIS]" << std::endl;
    double speedup = static_cast<double>(linear_duration) / indexed_duration;
    std::cout << "  Indexed search is " << std::fixed << std::setprecision(1)
              << speedup << "x faster!" << std::endl;
    std::cout << "  Time saved: " << (linear_duration - indexed_duration)
              << " μs" << std::endl;

    // ========================================================================
    // PART 4: Database Statistics (The Final Report)
    // ========================================================================
    std::cout << "\n\n► PART 4: Database Statistics" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    db.print_stats();

    // --- All Done! ---
    std::cout << "\n╔══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║    Demo Complete! Database files saved to disk.      ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════╝" << std::endl;

    return 0; // Exit successfully
}