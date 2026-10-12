import os
import subprocess
import sys

from common import BUILD_DIR, LAB2, VERSIONS, binary


def main():
    os.makedirs(BUILD_DIR, exist_ok=True)
    ok = True
    for tag, label, folder in VERSIONS:
        src = os.path.join(LAB2, folder)
        out = os.path.join(BUILD_DIR, tag)
        print(f'[build] {tag} {label:<20} <- {folder}', flush=True)
        r1 = subprocess.run(['cmake', '-S', src, '-B', out], capture_output=True, text=True)
        r2 = subprocess.run(['cmake', '--build', out], capture_output=True, text=True) if r1.returncode == 0 else r1
        if r2.returncode != 0 or not os.path.exists(binary(tag)):
            ok = False
            print(r1.stdout + r1.stderr + r2.stdout + r2.stderr)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
