# Mini C++ Key-Value Database Engine

## Introduction

This is a simple, educational key-value database engine written entirely in C++. It is designed as a learning tool to demonstrate the core components and principles of a modern database system from the ground up.

This project implements persistent storage, efficient indexing, memory caching, and crash safety (atomicity) in a minimal, understandable package. It is intended for educational purposes to show how data can be persistently stored, indexed for speed, and safely managed, rather than for production use.

## Core Features

This engine is built from several key components working together:

  * **Persistent Storage:** Data is saved to a binary file on disk (`database.dat`), ensuring that data persists even after the program terminates.
  * **B+ Tree Index:** An in-memory B+ Tree is used to index all keys, providing logarithmic-time $O(\log n)$ lookups, insertions, and deletions. This is what makes searching for data extremely fast.
  * **Buffer Pool (LRU Cache):** A "short-term memory" cache that keeps the most-used data pages in RAM. It uses a Least Recently Used (LRU) eviction policy to minimize slow disk I/O operations.
  * **Write-Ahead Logging (WAL):** A "safety journal" (`journal.log`) that logs every change *before* it is written to the main data file. This ensures atomicity and allows the database to recover from a crash without data corruption.
  * **CRUD API:** The main `StorageEngine` class provides the four essential database operations:
      * `insert(key, value)`
      * `get(key)`
      * `update(key, value)`
      * `remove(key)` (implemented as a "soft delete")

## How to Build and Run

### Prerequisites

You will need a C++ compiler that supports C++17 (e.g., G++, Clang, or MSVC).

### 1\. Compilation

This project is designed to be compiled as a single unit. The `main_test.cpp` file includes the `database_engine.cpp` file.

To compile the test program with optimizations (recommended for the benchmark):

```bash
# On Linux/macOS or with MinGW on Windows
g++ -o main_test main_test.cpp -std=c++17 -O2
```

### 2\. Running the Demo

Run the compiled executable from your terminal.

```bash
# On Windows (in MinGW/Git Bash or PowerShell)
./main_test.exe

# On Linux / macOS
./main_test
```

### 3\. Cleaning Up

The program will create three files in the same directory:

  * `main_test.exe` (or `main_test`): The compiled program.
  * `database.dat`: The persistent data file.
  * `journal.log`: The write-ahead log for crash recovery.

## Demo and Benchmark Output

Running the test program will perform a four-part demonstration. Part 3 is the most significant, as it directly compares the speed of the B+ Tree index (`db.get()`) against a full, "slow way" scan of the entire database file (`db.linear_search()`).

The output below shows the B+ Tree search is so fast (0 microseconds) that it is infinitely faster than the linear scan, which took over 90,000 microseconds.

```
$ ./main_test.exe
╔══════════════════════════════════════════════════════╗
║    MINI DATABASE ENGINE - C++ Implementation         ║
╚══════════════════════════════════════════════════════╝

► PART 1: CRUD Operations Demo
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

[CREATE] Inserting records...
✓ Inserted 5 records

[READ] Fetching records...
✓ user:1001 = Alice Johnson
✓ product:5001 = Laptop - $1299
✗ user:9999 = NOT FOUND

[UPDATE] Modifying records...
✓ Updated user:1002
  New value: Bob Smith (Updated)

[DELETE] Removing records...
✓ Deleted product:5002
✓ Verified deletion (should not be found)


► PART 2: Bulk Insert for Performance Testing
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Inserting 10000 records........
✓ Completed in 412 ms
  Throughput: 24271.8 inserts/sec


► PART 3: Search Performance Comparison
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

[INDEXED SEARCH] Using B+ Tree index...
✓ Found 5/6 keys
  Time: 0 μs (microseconds)
  Avg: 0 μs per lookup

[LINEAR SEARCH] Scanning entire file...
✓ Found 6/6 keys
  Time: 91518 μs (microseconds)
  Avg: 15253 μs per lookup

[SPEEDUP ANALYSIS]
  Indexed search is infx faster!
  Time saved: 91518 μs


► PART 4: Database Statistics
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
=== Database Statistics ===
File size: 122953728 bytes
Number of pages: 30018
Page size: 4096 bytes
Cache size: 100 pages

╔══════════════════════════════════════════════════════╗
║    Demo Complete! Database files saved to disk.      ║
╚══════════════════════════════════════════════════════╝
```

## Project Structure

  * `database_engine.cpp`: The core "engine" file. This contains all the class and struct definitions for the `StorageEngine`, `BPlusTreeIndex`, `BufferPool`, `JournalManager`, `Page`, and `Record`.
  * `main_test.cpp`: The "test drive" and benchmark program. This file includes the engine and acts as a client, running commands, timing performance, and printing results to the console.