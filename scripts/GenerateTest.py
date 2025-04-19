import os
import sys
from datetime import datetime

def create_test_file(test_path):
    base_dir = "../Desert/Tests"
    full_path = os.path.join(base_dir, test_path)
    
    if not full_path.endswith("_test.cpp"):
        full_path += "_test.cpp"
    
    os.makedirs(os.path.dirname(full_path), exist_ok=True)
    
    if os.path.exists(full_path):
        print(f"File  {full_path} already exists!")
        return
    
    test_template = f"""// {os.path.basename(full_path)}
// Auto generated {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}

    #include <gtest/gtest.h>

    TEST({os.path.basename(full_path).replace('.cpp', '')}, BasicTest) {{
        EXPECT_TRUE(true);
    }})
"""

    # Записываем файл
    with open(full_path, 'w', encoding='utf-8') as f:
        f.write(test_template)
    
    print(f"Generated test file: {full_path}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python create_test.py <path>")
        print("Example: python create_test.py Math/VectorTest")
        sys.exit(1)
    
    test_path = sys.argv[1]
    create_test_file(test_path)