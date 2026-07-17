# SteamToolsLua Launcher v3.3.0
import ctypes, webbrowser, tkinter as tk

VERSION = '3.3.0'
_URL = 'https://tttaaahhhaaa.github.io/'

ctypes.windll.shcore.SetProcessDpiAwareness(2)

root = tk.Tk()
root.title(f'SteamToolsLua Launcher v{VERSION}')
root.geometry('460x340+{}+{}'.format(
    (root.winfo_screenwidth()-460)//2, (root.winfo_screenheight()-340)//2))
root.configure(bg='#08080e')
root.resizable(False, False)

c = tk.Canvas(root, bg='#08080e', highlightthickness=0)
c.pack(fill=tk.BOTH, expand=True)

c.create_text(230, 50, text='SteamToolsLua', fill='#e0e0f0',
              font=('Bahnschrift SemiBold', 22))
c.create_text(230, 76, text=f'v{VERSION}', fill='#7c6fff',
              font=('Segoe UI', 11))
c.create_line(70, 95, 390, 95, fill='#2a2a5a44', width=1)

# main text
c.create_text(230, 150, text='Download Launcher', fill='#c0c0ff',
              font=('Segoe UI', 18, 'bold'))
c.create_text(230, 180, text=_URL, fill='#686880',
              font=('Segoe UI', 9))

# button
pts = [110,215, 150,205, 310,205, 350,215, 350,245, 310,255, 150,255, 110,245]
btn = c.create_polygon(pts, fill='#1a1a3e', smooth=True)
txt = c.create_text(230, 230, text='DOWNLOAD', fill='#c0c0ff',
                    font=('Segoe UI', 12, 'bold'))
for item in (btn, txt):
    c.tag_bind(item, '<Enter>', lambda e: c.itemconfig(btn, fill='#2a2a5a'))
    c.tag_bind(item, '<Leave>', lambda e: c.itemconfig(btn, fill='#1a1a3e'))
    c.tag_bind(item, '<ButtonPress-1>', lambda e: webbrowser.open(_URL))

c.create_text(230, 310, text='github.com/tttaaahhhaaa', fill='#585878',
              font=('Segoe UI', 8))

try:
    dpi = ctypes.windll.user32.GetDpiForWindow(root.winfo_id())
    root.tk.call('tk', 'scaling', dpi / 72.0)
except: pass

root.mainloop()
