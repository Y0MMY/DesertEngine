import os
import shutil
import subprocess
import sys
import colorama
from colorama import Fore, Back, Style

colorama.init()

# Change from Scripts directory to root
os.chdir('../../')

def premake_invocation():
    """Returns (premake executable, generator action) for the current platform."""
    if sys.platform == "win32":
        return "vendor/bin/premake5.exe", "vs2022"
    premake = shutil.which("premake5")
    if premake is None:
        print(f"{Fore.RED}premake5 not found — run scripts/MacOS/Setup.sh first.{Style.RESET_ALL}")
        sys.exit(1)
    return premake, "gmake2"

def main():
    premake, action = premake_invocation()
    premake_args = [premake, action] + sys.argv[1:]

    print(f"{Style.BRIGHT}{Back.GREEN}Generating project files...{Style.RESET_ALL}")
    print(f"Command: {' '.join(premake_args)}")

    subprocess.call(premake_args)

if __name__ == "__main__":
    main()
