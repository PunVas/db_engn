# my db engine

just a toy database engine i wrote in c++.

it's not for production, just shows how a db works under the hood. the core logic is in the headers/server files, and the test/demo is in `db_test.cpp`.

## features

* saves data to `database.dat`
* uses a b+ tree for the index (in memory) so it's fast
* has a simple cache (lru)
* writes to a `journal.log` first so it doesn't break if it crashes

## how to build

you need a c++ compiler. just compile the test file with optimizations so the benchmark is accurate.

```bash
g++ -o db_test db_test.cpp -std=c++17 -O3
````

## running

just run the exe it makes.

```bash
./db_test
```

## test output

here's what i got on my machine. the index is way faster than just reading the whole file.

```text
╔══════════════════════════════════════════════════════╗
║     MINI DATABASE ENGINE - C++ Implementation        ║
╚══════════════════════════════════════════════════════╝

► PART 1: Basic read/write test
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Inserting a few records...
OK, 5 records in.

Now, let's try to get them back.
  Get user:1001... Alice Johnson
  Get product:5001... Laptop - $1299
  Get user:9999... NOT FOUND

Updating user:1002...
  -> New value: Bob Smith (Updated)

Deleting product:5002...
  -> Verify delete: OK, gone.


► PART 2: Bulk Insert (Stress Test)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Alright, let's hammer it. Inserting 10000 records........
Done. Took 163 ms
  -> Throughput: 61349.7 inserts/sec


► PART 3: Speed Test (Index vs. Full Scan)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Testing FAST (BTree) search...
  -> Found 5/6 keys
  -> Took: 45 us (microseconds)

Testing SLOW (linear file scan)...
  -> Found 6/6 keys
  -> Took: 26637 us (microseconds)

[SPEEDUP ANALYSIS]
  Indexed search is 591.9x faster!
  Time saved: 26592 us


► PART 4: Database Statistics
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
=== Database Statistics ===
File size: 40984576 bytes
Number of pages: 10006
Page size: 4096 bytes
Cache size: 100 pages

╔══════════════════════════════════════════════════════╗
║     Demo Complete! Database files saved to disk.     ║
╚══════════════════════════════════════════════════════╝
```
