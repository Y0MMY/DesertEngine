import os
import subprocess
import sys

# NOTE: yaml-cpp's premake project is now a committed config
# (BuildScripts/ThirdParty/YamlCpp.lua) — this script no longer generates it.

gtest_target_dir = "../ThirdParty/googletest"

def build_gtest():
    """Builds GoogleTest from the submodule sources.

    Windows-only: on macOS gtest comes from Homebrew (scripts/MacOS/Setup.sh),
    and the MSVC runtime flag below has no meaning elsewhere anyway.
    """
    if sys.platform != "win32":
        print("Skipping GoogleTest build: handled by the platform package manager on this OS.")
        return

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
    build_gtest()
