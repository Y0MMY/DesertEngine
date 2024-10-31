import os
import subprocess
import sys

import colorama

from colorama import Fore
from colorama import Back
from colorama import Style

colorama.init()

# Change from Scripts directory to root
os.chdir('../')

if sys.platform == "win32":

    print(f"{Style.BRIGHT}{Back.GREEN}Generating Visual Studio 2022 solution.{Style.RESET_ALL}")
    subprocess.call(["vendor/bin/premake5.exe", "vs2022"])
else:
    print("Ошибка: Данная операция поддерживается только на платформе Windows.")