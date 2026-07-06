@echo off
REM Build SteamToolsLua EXE
REM Requires: pyinstaller, requests, pillow
REM Usage: build.bat

pyinstaller --onefile --windowed --icon=%~dp0icon.ico --name=SteamToolsLua ^
  --add-data "%~dp0icon.ico;." ^
  --add-data "%~dp0webviewer_module.py;." ^
  --add-data "%~dp0aria2c.exe;." ^
  --add-data "%~dp0CloudRedirect.exe;." ^
  --add-data "%~dp0cloud_redirect.dll;." ^
  --hidden-import=html --hidden-import=html.parser --hidden-import=html.entities ^
  --hidden-import=requests --hidden-import=urllib3 --hidden-import=certifi ^
  --hidden-import=charset_normalizer --hidden-import=idna ^
  --hidden-import=PIL --hidden-import=PIL.Image --hidden-import=PIL.ImageTk ^
  --hidden-import=PIL.ImageDraw --hidden-import=PIL.ImageFont --hidden-import=PIL.ImageFilter ^
  --hidden-import=tkinter.filedialog --hidden-import=tkinter.messagebox ^
  --hidden-import=tkinter.simpledialog --hidden-import=tkinter.commondialog ^
  --hidden-import=tkinter.scrolledtext --hidden-import=tkinter.ttk ^
  --hidden-import=tkinter.colorchooser --hidden-import=tkinter.font ^
  --hidden-import=webview ^
  --distpath=. --workpath=build_temp --specpath=build_temp --noconfirm SteamToolsLua.py

echo.
echo Build complete. EXE is in current directory.
pause
