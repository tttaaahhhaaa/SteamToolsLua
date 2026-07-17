# SteamToolsLua Updater — standalone EXE for self-replace
import os, subprocess as _sp, sys, time
from pathlib import Path

_old = None; _new = None
for i, a in enumerate(sys.argv[1:]):
    if a == '--old-exe' and i + 1 < len(sys.argv) - 1:
        _old = Path(sys.argv[i + 2])
    if a == '--new-exe' and i + 1 < len(sys.argv) - 1:
        _new = Path(sys.argv[i + 2])

if not _old or not _new or not _old.exists():
    sys.exit(1)

# wait for old exe to exit
while True:
    try:
        _sp.run(['taskkill', '/f', '/im', _old.name], capture_output=True, timeout=5)
    except: pass
    try:
        _old.rename(_old.with_suffix('.bak.exe'))
        break
    except:
        time.sleep(0.5)

# move new into place
try:
    _new.rename(_old)
except:
    _new.replace(_old)

# launch
_sp.Popen([str(_old)], close_fds=True)

# clean up bak + self
try:
    (_old.with_suffix('.bak.exe')).unlink(missing_ok=True)
except: pass
try:
    Path(sys.argv[0]).unlink(missing_ok=True)
except: pass
