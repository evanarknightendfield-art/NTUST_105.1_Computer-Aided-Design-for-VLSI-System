"""Event wait (inotify, no periodic polling), then run the serial diagnostics."""
import ctypes,os,subprocess,sys
from pathlib import Path
r=Path('Testcases/results/CodexCapDP')
libc=ctypes.CDLL(None,use_errno=True)
fd=libc.inotify_init1(0)
if fd<0:raise OSError(ctypes.get_errno(),'inotify_init1')
wd=libc.inotify_add_watch(fd,os.fsencode(r),0x100|0x8) # create / close_write
if wd<0:raise OSError(ctypes.get_errno(),'inotify_add_watch')
while not (r/'baseline.done').exists():os.read(fd,16384)
os.close(fd)
sys.exit(subprocess.call([sys.executable,'-u',str(r/'scripts/diagnostic_runs.py')]))
