import os
import subprocess
import sys

import colorama
from colorama import Fore, Back, Style

colorama.init()

project_dir = os.path.dirname(os.path.abspath(__file__)) 
project_dir = os.path.abspath(os.path.join(project_dir, ".."))   

build_dir = os.path.join(project_dir, 'build')

if not os.path.exists(build_dir):
    os.makedirs(build_dir)

os.chdir(build_dir)

if sys.platform == "win32":
    print(f"{Style.BRIGHT}{Back.GREEN}Generating Visual Studio 2022 solution with CMake.{Style.RESET_ALL}")
    result = subprocess.call(["cmake", "-G", "Visual Studio 17 2022", project_dir])
    
    if result != 0:
        print(f"{Fore.RED}Ошибка при генерации решения CMake.{Style.RESET_ALL}")
else:
    print(f"{Fore.RED}Ошибка: Данная операция поддерживается только на платформе Windows.{Style.RESET_ALL}")
