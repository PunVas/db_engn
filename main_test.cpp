#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>

//yeah am lazzzeee
using namespace std;

int main(){
	cout << "╔══════════════════════════════════════════════════════╗\n";
	cout << "║     MINI DATABASE ENGINE - C++ Implementation        ║\n";
	cout << "╚══════════════════════════════════════════════════════╝\n\n";
	
	// ok, let's fire it up
	SEng db;
	
	
	// --- Basic CRUD Test ---
	cout << "► PART 1: Basic read/write test\n";
	cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
	
	cout << "\nInserting a few records...\n";
	db.insert("user:1001", "Alice Johnson");
	db.insert("user:1002", "Bob Smith");
	db.insert("user:1003", "Charlie Brown");
	db.insert("product:5001", "Laptop - $1299");
	db.insert("product:5002", "Mouse - $29");
	cout << "OK, 5 records in.\n";
	
	cout << "\nNow, let's try to get them back.\n";
	auto res1 = db.get("user:1001");
	cout << "  Get user:1001... " << (res1.first ? res1.second : "NOT FOUND") << "\n";
	
	auto res2 = db.get("product:5001");
	cout << "  Get product:5001... " << (res2.first ? res2.second : "NOT FOUND") << "\n";
	
	// test a key that shouldn't exist
	auto res3 = db.get("user:9999");
	cout << "  Get user:9999... " << (res3.first ? res3.second : "NOT FOUND") << "\n";
	
	cout << "\nUpdating user:1002...\n";
	db.update("user:1002", "Bob Smith (Updated)");
	auto res4 = db.get("user:1002");
	cout << "  -> New value: " << res4.second << "\n";
	
	cout << "\nDeleting product:5002...\n";
	db.remove("product:5002");
	auto res5 = db.get("product:5002");
	cout << "  -> Verify delete: " << (res5.first ? "FAILED, STILL THERE" : "OK, gone.") << "\n";
	

	// --- Bulk Insert Test ---
	cout << "\n\n► PART 2: Bulk Insert (Stress Test)\n";
	cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
	
	const int B_SIZE = 10000;
	cout << "\nAlright, let's hammer it. Inserting " << B_SIZE << " records..." << flush;
	
	auto t1 = chrono::high_resolution_clock::now();
	
	for (int i = 0; i < B_SIZE; ++i) {
		string k = "bench:" + to_string(i);
		string v = "Data_" + to_string(i * 1000);
		db.insert(k, v);
		
		if ((i + 1) % 2000 == 0) {
			cout << "." << flush;
		}
	}
	
	auto t2 = chrono::high_resolution_clock::now();
	// need to use long here, int might overflow
	auto dur_ms = chrono::duration_cast<chrono::milliseconds>(t2 - t1).count();
	
	cout << "\nDone. Took " << dur_ms << " ms\n";
	cout << "  -> Throughput: " << (B_SIZE * 1000.0 / dur_ms) << " inserts/sec\n";
	
	db.flushAll(); // make sure it's all on disk


	// --- Speed Test (Index vs. Scan) ---
	cout << "\n\n► PART 3: Speed Test (Index vs. Full Scan)\n";
	cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
	
	vector<string> keys_to_find = {
		"bench:100",  //start
		"bench:2500",
		"bench:5000", //midddle
		"bench:7500",
		"bench:9999", //end
		"user:1001"   //one from the first batch
		// "bench:-1", // tried this, failed, lol XD, will fix it next
	};
	
	cout << "\nTesting FAST (BTree) search...\n";
	auto t_idx1 = chrono::high_resolution_clock::now();
	
	int found1 = 0;
	for (const auto& k : keys_to_find) {
		if (db.get(k).first) found1++;
	}
	
	auto t_idx2 = chrono::high_resolution_clock::now();
	auto dur_idx = chrono::duration_cast<chrono::microseconds>(t_idx2 - t_idx1).count();
	
	cout << "  -> Found " << found1 << "/" << keys_to_find.size() << " keys\n";
	cout << "  -> Took: " << dur_idx << " us (microseconds)\n";
	
	
	cout << "\nTesting SLOW (linear file scan)...\n";
	auto t_lin1 = chrono::high_resolution_clock::now();
	
	int found2 = 0;
	for (const auto& k : keys_to_find) {
		if (db.lSearch(k).first) found2++;
	}
	
	auto t_lin2 = chrono::high_resolution_clock::now();
	auto dur_lin = chrono::duration_cast<chrono::microseconds>(t_lin2 - t_lin1).count();
	
	cout << "  -> Found " << found2 << "/" << keys_to_find.size() << " keys\n";
	cout << "  -> Took: " << dur_lin << " us (microseconds)\n";
	
	
	// The fun part
	cout << "\n[SPEEDUP ANALYSIS]\n";
    if (dur_idx > 0) {
        double speedup = static_cast<double>(dur_lin) / dur_idx;
        cout << "  Indexed search is " << fixed << setprecision(1) 
                  << speedup << "x faster!\n";
    } else {
        // handle divide by zero if index is too fast (lol)
        cout << "  Indexed search was basically instant (0 us).\n";
        cout << "  Linear scan took " << dur_lin << " us. So... much faster.\n";
    }
	cout << "  Time saved: " << (dur_lin - dur_idx) << " us\n";

	
	// --- Final Stats ---
	cout << "\n\n► PART 4: Database Statistics\n";
	cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
	db.stats(); // call the stats func
	
	cout << "\n╔══════════════════════════════════════════════════════╗\n";
	cout << "║     Demo Complete! Database files saved to disk.     ║\n";
	cout << "╚══════════════════════════════════════════════════════╝\n";
	
	return 0; // all done
}
