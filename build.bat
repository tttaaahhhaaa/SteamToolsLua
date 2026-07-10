@echo off
REM Build SteamToolsLua EXE (onedir mode - more reliable)
REM Requires: pyinstaller, requests, pillow
REM Usage: build.bat

pyinstaller --onedir --windowed --icon="%~dp0icon.ico" --name=SteamToolsLua_v2.7.1 ^
  --add-data "%~dp0bypass;./bypass" ^
  --hidden-import=html --hidden-import=html.parser --hidden-import=html.entities ^
  --hidden-import=requests --hidden-import=urllib3 --hidden-import=certifi ^
  --hidden-import=charset_normalizer --hidden-import=idna ^
  --hidden-import=PIL --hidden-import=PIL.Image --hidden-import=PIL.ImageTk ^
  --hidden-import=PIL.ImageDraw --hidden-import=PIL.ImageFont --hidden-import=PIL.ImageFilter ^
  --hidden-import=tkinter.filedialog --hidden-import=tkinter.messagebox ^
  --hidden-import=tkinter.simpledialog --hidden-import=tkinter.commondialog ^
  --hidden-import=tkinter.scrolledtext --hidden-import=tkinter.ttk ^
  --hidden-import=tkinter.colorchooser --hidden-import=tkinter.font ^
  --distpath=. --workpath=build_temp --specpath=build_temp --noconfirm src\SteamToolsLua.py

echo.
echo Build complete. Folder SteamToolsLua_v2.7.1 is in current directory.
pause
