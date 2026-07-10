$Python314 = "C:\Users\Taha\AppData\Local\Programs\Python\Python314\python.exe"
$PyInstaller = "C:\Users\Taha\AppData\Local\Programs\Python\Python314\Scripts\pyinstaller.exe"
$Source = "C:\Users\Taha\Desktop\SteamToolsLua.py"
$Icon = "C:\Users\Taha\Desktop\SteamToolsLua_Repo\icon.ico"
$Output = "C:\Users\Taha\Desktop\SteamToolsLua_v2.7.1.exe"
$WorkDir = "C:\Users\Taha\Desktop\SteamToolsLua_Repo\build_temp_314"

Remove-Item -LiteralPath $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
$IconDest = "$WorkDir\icon.ico"
Copy-Item -Path $Icon -Destination $IconDest -Force

& $PyInstaller --onefile --windowed --icon=$IconDest --name=SteamToolsLua `
    --hidden-import=html --hidden-import=html.parser --hidden-import=html.entities `
    --hidden-import=requests --hidden-import=urllib3 --hidden-import=certifi `
    --hidden-import=charset_normalizer --hidden-import=idna `
    --hidden-import=PIL --hidden-import=PIL.Image --hidden-import=PIL.ImageTk `
    --hidden-import=PIL.ImageDraw --hidden-import=PIL.ImageFont --hidden-import=PIL.ImageFilter `
    --hidden-import=tkinter.filedialog --hidden-import=tkinter.messagebox `
    --hidden-import=tkinter.simpledialog --hidden-import=tkinter.commondialog `
    --hidden-import=tkinter.scrolledtext --hidden-import=tkinter.ttk `
    --hidden-import=tkinter.colorchooser --hidden-import=tkinter.font `
    --distpath="C:\Users\Taha\Desktop" `
    --workpath=$WorkDir `
    --specpath=$WorkDir `
    --noconfirm $Source

if ($?) {
    Move-Item -LiteralPath "C:\Users\Taha\Desktop\SteamToolsLua.exe" -Destination $Output -Force
    Write-Host "BUILD BASARILI: $Output"
} else {
    Write-Host "BUILD HATASI!"
}
