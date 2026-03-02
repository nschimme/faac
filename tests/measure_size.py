import os
import sys
import json

def measure(lib_path, bin_path, output_json):
    results = {
        "lib_size": os.path.getsize(lib_path) if os.path.exists(lib_path) else 0,
        "bin_size": os.path.getsize(bin_path) if os.path.exists(bin_path) else 0
    }

    with open(output_json, "w") as f:
        json.dump(results, f, indent=2)

    print(f"Library Size: {results['lib_size']} bytes")
    print(f"Binary Size:  {results['bin_size']} bytes")

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python3 tests/measure_size.py <lib_path> <bin_path> <output_json>")
        sys.exit(1)
    measure(sys.argv[1], sys.argv[2], sys.argv[3])
