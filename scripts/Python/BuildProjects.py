import os
import subprocess
import sys
import colorama
from colorama import Fore, Back, Style

colorama.init()

# Change from Scripts directory to root
os.chdir('../../')

def main():
    if sys.platform != "win32":
        print("Error: This operation is only supported on Windows.")
        return

    premake_args = ["vendor/bin/premake5.exe"] + ["vs2022"] + sys.argv[1:]

    print(f"{Style.BRIGHT}{Back.GREEN}Generating project files...{Style.RESET_ALL}")
    print(f"Command: {' '.join(premake_args)}")
    
    subprocess.call(premake_args)

if __name__ == "__main__":
    main()