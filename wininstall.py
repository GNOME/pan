#!python3

"""
These aren't necessary if you want a windows theme

1. Copy Icon themes
1.1. `cp -r /mingw64/share/icons/* ./share/icons/
1. Copy default themes
1.1. `cp -r /mingw64/share/themes/* ./share/themes/
1. Copy settins schemas
1.1. `cp /mingw64/share/glib-2.0/schemas/* ./share/glib-2.0/schemas/`
1.1. `glib-compile-schemas.exe ./share/glib-2.0/schemas/`

See https://www.gtk.org/docs/installations/windows/ also

"""

import os
import re
import shutil
import subprocess
import sys

def usage():
    print("""wininstall.py [<target dir>|--help]

Installs built pan to specified target directory
""")
    exit(1)

def copy_executable(executable: str, target_dir: str) -> set:
    """Copy executable and work out what dlls we need.

    Args:
        executable: Executable file to copy
        target_dir: Where to copy it!

    Returns:
        Set of dlls required by executable
    """

    print(f"Copying {executable} to {target_dir}")
    shutil.copy2(executable, target_dir)

    dlls=set()

    if os.path.splitext(executable)[1] not in (".dll", ".exe"):
        return dlls
    # Get all the dlls we need in the root
    output = subprocess.run(
        ["ldd", executable], capture_output=True, text=True, check=True
    )
    for line in output.stdout.splitlines():
        dll = re.search(r'(/mingw64.*\.dll)', line)
        if dll:
            dlls.add(dll.group())

    return dlls


dlls = set()

def copy_wrapper(source: str, target: str, *, follow_symlinks: bool = True):
    got_dlls = copy_executable(source, os.path.dirname(target))
    global dlls
    dlls |= got_dlls
    return target

def main():
    if len(sys.argv) != 2 or sys.argv[1] == "--help":
        usage()
    target_dir = sys.argv[1]
    if os.path.exists(target_dir) and not os.path.isdir(target_dir):
        raise RuntimeError(f"{target_dir} is not a directory")
    os.makedirs(target_dir, exist_ok=True)

    # Copy executable to target dir
    executable = "pan/gui/pan.exe"
    global dlls
    dlls = copy_executable(executable, target_dir)

    gdk_pixbuf = "lib/gdk-pixbuf-2.0"
    shutil.copytree(os.path.join(os.environ["MSYSTEM_PREFIX"], gdk_pixbuf),
                    os.path.join(target_dir, gdk_pixbuf),
                    copy_function=copy_wrapper,
                    ignore=shutil.ignore_patterns("*.a"),
                    dirs_exist_ok=True)

    # Deal with magically autoloaded stuff

    # gtk2 def, gtk3 poss.
    #------------------------------------------------------------------------
    # We (apparently) need this to run shells (and its not enough)
    dlls |= copy_executable(
        os.path.join(os.environ["MSYSTEM_PREFIX"], "bin/gspawn-win64-helper.exe"),
        target_dir
    )

    # gtk2
    #------------------------------------------------------------------------
    gtk2_libs="lib/gtk-2.0/2.10.0/engines"
    shutil.copytree(os.path.join(os.environ["MSYSTEM_PREFIX"], gtk2_libs),
                    os.path.join(target_dir, gtk2_libs),
                    copy_function=copy_wrapper,
                    ignore=shutil.ignore_patterns("*.a"),
                    dirs_exist_ok=True)

    # At this point we have a version of pan which runs but looks like it is
    # on windows 95

    # None of these appear to be necessary so why do we have them?
    # However, the release pan version has very slightly nicer fonts, so need
    # further check (though it might be this version or something)
    # add etc/fonts?
    # add etc/gtk-2.0
    # add etc/pango

    # for enchant:
    # lib/enchant
    # share/enchant
    # share/myspell

    # all gtk versions
    #------------------------------------------------------------------------
    themes="share/themes"
    shutil.copytree(os.path.join(os.environ["MSYSTEM_PREFIX"], themes),
                    os.path.join(target_dir, themes),
                    copy_function=copy_wrapper,
                    ignore=shutil.ignore_patterns("*.a"),
                    dirs_exist_ok=True)


    # Now we copy all the dlls we depend on. Unfortunately, they all have
    # unix like names, so we need to replace all of them
    for dll in sorted(dlls):
        dll = dll.replace(
            f'/{os.environ["MSYSTEM"].lower()}',
            os.environ["MSYSTEM_PREFIX"],
            1
        )
        copy_executable(dll, target_dir)


if __name__ == "__main__":
    main()
