import os
import subprocess

# Define the directory paths
yaml_target_dir = "../ThirdParty/yaml-cpp"
gtest_target_dir = "../ThirdParty/googletest"
premake5_file_name = "premake5.lua"

# Ensure the target directory exists
os.makedirs(yaml_target_dir, exist_ok=True)

def build_yaml():
   # Content of the premake5.lua file
   file_content = """
   project "yaml-cpp"
      kind "StaticLib"
      language "C++"
      
      files { "src/**.cpp", "include/**.h", "src/**.h", }

      includedirs { "include" }

      filter "system:windows"
         defines { "YAML_CPP_STATIC_DEFINE" }
         systemversion "latest"
         
      filter "system:not windows"
         pic "On"  -- Enable PIC for non-Windows platforms by default
   """

   # Full path to the premake5.lua file
   file_path = os.path.join(yaml_target_dir, premake5_file_name)

   # Write the content to the file
   with open(file_path, 'w') as file:
      file.write(file_content)

   print(f"File {premake5_file_name} has been created successfully in {yaml_target_dir}.")

def build_gtest():
    gtest_build_dir = gtest_target_dir + '/build'
    if not os.path.exists(gtest_build_dir):
        os.makedirs(gtest_build_dir)
    
    os.chdir(gtest_build_dir)

    try:
        subprocess.check_call(["cmake", "..", "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebug"])
        
        subprocess.check_call(["cmake", "--build", ".", "--config", "Debug"])
    except subprocess.CalledProcessError as e:
        print(f"Error while building GoogleTest: {e}")
    finally:
        os.chdir("../../..")

if __name__ == "__main__":
    build_yaml()
    build_gtest()