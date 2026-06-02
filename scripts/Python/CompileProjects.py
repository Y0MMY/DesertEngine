import os
import subprocess
import sys
import colorama
from colorama import Fore, Back, Style

colorama.init()

# Change from Scripts/Python directory to root
os.chdir('../../')

def find_msbuild():
    # Use vswhere to find the latest MSBuild path
    vswhere_path = os.path.expandvars(r"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe")
    if not os.path.exists(vswhere_path):
        return None
    
    try:
        output = subprocess.check_output([
            vswhere_path, 
            "-latest", 
            "-requires", "Microsoft.Component.MSBuild", 
            "-find", "MSBuild\\**\\Bin\\MSBuild.exe"
        ]).decode().strip()
        return output
    except:
        return None

def main():
    if sys.platform != "win32":
        print(f"{Fore.RED}Error: This script is only supported on Windows.{Style.RESET_ALL}")
        return

    # Check if project files need generation
    sln_path = "Desert.sln"
    if not os.path.exists(sln_path) or "--gen" in sys.argv:
        print(f"{Style.BRIGHT}{Back.GREEN}Generating project files via Premake...{Style.RESET_ALL}")
        # Call the existing BuildProjects logic or just the executable
        premake_path = os.path.abspath("vendor/bin/premake5.exe")
        subprocess.call([premake_path, "vs2022"])

    msbuild_path = find_msbuild()
    if not msbuild_path:
        msbuild_path = "msbuild"

    if not os.path.exists(sln_path):
        print(f"{Fore.RED}Error: {sln_path} still not found after generation.{Style.RESET_ALL}")
        return

    config = "Debug"
    platform = "x64"

    print(f"{Style.BRIGHT}{Back.BLUE}Compiling {sln_path} ({config}|{platform})...{Style.RESET_ALL}")
    print(f"{Fore.YELLOW}Tip: Ensure Editor.exe is closed to avoid LNK1168.{Style.RESET_ALL}")
    
    # Use incremental build flags and force synchronous PDB writes
    compile_args = [
        msbuild_path, 
        sln_path, 
        f"/p:Configuration={config}", 
        f"/p:Platform={platform}", 
        "-m", 
        "-verbosity:minimal",
        "/t:Build",
        "/p:CL_MPcount=8",
        "/p:AdditionalOptions=/FS" 
    ]
    
    print(f"Command: {' '.join(compile_args)}")
    
    result = subprocess.call(compile_args)
    
    if result == 0:
        print(f"{Fore.GREEN}Build Succeeded!{Style.RESET_ALL}")
    else:
        print(f"{Fore.RED}Build Failed with exit code {result}.{Style.RESET_ALL}")
        sys.exit(result)

if __name__ == "__main__":
    main()