"""Windows compatibility fixes for PlatformIO + embeddable Python setups.

1. SCons response-file encoding (non-ASCII toolchain paths).

   PlatformIO moves overlength compile/link command lines into an
   @response file. SCons (>=4.4) writes it as UTF-8 by default, but
   MinGW-based cross compilers (xtensa-esp32s3-elf-gcc etc.) read raw
   bytes and resolve paths with the ANSI code page (GBK on Chinese
   Windows). Non-ASCII paths (e.g. C:\\Users\\<中文名>\\...) then turn
   into garbage and the compiler reports
   "fatal error: <header>: No such file or directory" for headers that
   exist. Fix: encode the response file with the system ANSI code page
   ("mbcs"), matching how the toolchain reads it.

2. esptool import in isolated Python.

   The embeddable CPython distribution (python3._pth present) runs in
   isolated mode: sys.path[0] is not populated with the script's
   directory, so "python .../tool-esptoolpy/esptool.py" cannot import
   the local "esptool" package ("ModuleNotFoundError: No module named
   'esptool'"). Fix: put the tool-esptoolpy package dir on PYTHONPATH
   for every process spawned by the build.
"""
from platformio.compat import IS_WINDOWS

Import("env")  # noqa: F405  SCons global provided by PIO

if IS_WINDOWS:
    env.Replace(TEMPFILEENCODING="mbcs")  # noqa: F405
    print("[compat] TEMPFILEENCODING=mbcs (system ANSI code page)")

esptool_dir = env.PioPlatform().get_package_dir("tool-esptoolpy")  # noqa: F405
if esptool_dir:
    env.PrependENVPath("PYTHONPATH", esptool_dir)  # noqa: F405
    print("[compat] PYTHONPATH += %s" % esptool_dir)
