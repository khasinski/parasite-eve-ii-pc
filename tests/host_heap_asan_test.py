"""Negative controls: the production native heap must expose each block to ASan."""
from pathlib import Path
import os
import subprocess
import sys

def main():
    binary = Path(sys.argv[1]).resolve()
    for mode, diagnostic in (("overrun", "heap-buffer-overflow"), ("retired", "heap-use-after-free")):
        result = subprocess.run([str(binary), mode], env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if result.returncode == 0 or diagnostic not in result.stdout:
            raise SystemExit(f"{mode}: missing expected {diagnostic}; exit={result.returncode}\n{result.stdout}")
    print("Native heap ASan controls: per-allocation bounds and retirement detected")


if __name__ == "__main__":
    main()
