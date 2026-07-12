$Python311 = "C:\Users\Taha\AppData\Local\Programs\Python\Python314\python.exe"
$PyInstaller = "C:\Users\Taha\AppData\Local\Programs\Python\Python314\Scripts\pyinstaller.exe"
$Source = "MAIN_PROJECT.py"
$Output = "C:\Users\Taha\Desktop\SteamToolsLua_v2.9.0.exe"

$HiddenImports = @(
    "--hidden-import=html", "--hidden-import=html.parser", "--hidden-import=html.entities"
    "--hidden-import=requests", "--hidden-import=urllib3", "--hidden-import=certifi"
    "--hidden-import=charset_normalizer", "--hidden-import=idna"
    "--hidden-import=PIL", "--hidden-import=PIL.Image", "--hidden-import=PIL.ImageTk"
    "--hidden-import=PIL.ImageDraw", "--hidden-import=PIL.ImageFont", "--hidden-import=PIL.ImageFilter"
    "--hidden-import=tkinter.filedialog", "--hidden-import=tkinter.messagebox"
    "--hidden-import=tkinter.simpledialog", "--hidden-import=tkinter.commondialog"
    "--hidden-import=tkinter.scrolledtext", "--hidden-import=tkinter.ttk"
    "--hidden-import=tkinter.colorchooser", "--hidden-import=tkinter.font"
    "--hidden-import=xml.etree.ElementTree", "--hidden-import=http.client"
    "--hidden-import=urllib.parse", "--hidden-import=uuid", "--hidden-import=winreg"
    "--hidden-import=PIL.ImageEnhance", "--hidden-import=PIL.ImageChops", "--hidden-import=PIL.ImageOps"
    "--hidden-import=PIL.PngImagePlugin", "--hidden-import=PIL.JpegImagePlugin", "--hidden-import=PIL.GifImagePlugin"
    "--hidden-import=PIL.IcoImagePlugin", "--hidden-import=PIL.BmpImagePlugin"
)

Remove-Item -LiteralPath "build_temp" -Recurse -Force -ErrorAction SilentlyContinue

$AbsIcon = (Resolve-Path -LiteralPath "icon.ico").Path
& $PyInstaller --onefile --noconsole --icon="$AbsIcon" --name=SteamToolsLua `
    @HiddenImports `
    --distpath="C:\Users\Taha\Desktop" `
    --workpath="build_temp" `
    --specpath="build_temp" `
    --noconfirm $Source

if ($?) {
    Remove-Item -LiteralPath $Output -Force -ErrorAction SilentlyContinue
    Move-Item -LiteralPath "C:\Users\Taha\Desktop\SteamToolsLua.exe" -Destination $Output -Force
    Write-Host "BUILD BASARILI: $Output"
} else {
    Write-Host "BUILD HATASI!"
}
