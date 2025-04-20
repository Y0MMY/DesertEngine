import os
import subprocess
import sys
import argparse

def find_test_executables(bin_dir="../../build/Bin/Tests", config="Debug"):
    tests = []
    search_path = os.path.join(bin_dir, config)
    if not os.path.exists(search_path):
        return tests
    
    for file in os.listdir(search_path):
        if file.endswith("_test") or file.endswith("_test.exe"):
            full_path = os.path.normpath(os.path.join(search_path, file))
            tests.append(full_path)
    return tests

def run_test(test_path):
    try:
        result = subprocess.run(
            [test_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
            check=True
        )
        print(f"[PASS] {os.path.basename(test_path)}")
        return True
    except subprocess.CalledProcessError as e:
        print(f"[FAIL] {os.path.basename(test_path)}")
        if e.stdout:
            print(e.stdout)
        if e.stderr:
            print(e.stderr)
        return False

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default="Debug", help="Build configuration (Debug/Release)")
    args = parser.parse_args()

    tests = find_test_executables(config=args.config)
    if not tests:
        print("No test executables found!")
        sys.exit(1)

    print(f"Running {len(tests)} test(s)...")
    print("-" * 50)

    failed_tests = 0
    for test in tests:
        if not run_test(test):
            failed_tests += 1

    print("-" * 50)
    if failed_tests:
        print(f"Test run failed: {failed_tests} test(s) failed!")
        sys.exit(1)
    else:
        print("All tests passed successfully!")
        sys.exit(0)

if __name__ == "__main__":
    main()