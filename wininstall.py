#!python3

import glob
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

def copy_tree(*, target_dir: str, tree: str):
    """Wrapper to copy an entire tree from a magic place to target folder

    Args:
        target_dir - Directory containing standalone executable
        tree - folder in /mingw to copy to executable directory
    """
    shutil.copytree(os.path.join(os.environ["MSYSTEM_PREFIX"], tree),
                    os.path.join(target_dir, tree),
                    copy_function=copy_wrapper,
                    ignore=shutil.ignore_patterns("*.a"),
                    dirs_exist_ok=True)

def read_configure():
    dbus = False
    gmime_crypto = False
    spellcheck = False
    gtk_version = 2
# webkit?
    tls_support = False
    popup_notifications = False
    password_storage = False
# yelp-tools ?
    manual = False

    # These are in the order output by configure
    with open("config.h") as config:
        for line in config.readlines():
            line = line.rstrip()
            words = line.split()
            if (len(words) != 3 or words[0] != '#define'
                or words[2] != '1'):
                continue
            if words[1] == 'HAVE_DBUS':
                dbus = True # better to say multiple pans?
            elif words[1] == 'HAVE_GMIME_CRYPTO':
                gmime_crypto = True
            elif words[1] == 'HAVE_GTKSPELL':
                spellcheck = True
            elif words[1] == 'HAVE_GTK':
                gtk_version = 3
            # webkit?
            elif words[1] == 'HAVE_GNUTLS':
                tls_support = True
            elif words[1] == 'HAVE_LIBNOTIFY':
                popup_notifications = True
            elif words[1] == 'HAVE_GKR':
                password_storage = True
            elif words[1] == 'HAVE_MANUAL':
                manual = True

    return {
        "dbus": dbus,
        "gmime_crypto": gmime_crypto,
        "spellcheck": spellcheck,
        "gtk_version": gtk_version,
        # webkit?
        "tls_support": tls_support,
        "popup_notifications": popup_notifications,
        "password_storage": password_storage,
        "manual": manual
    }


def main():
    if len(sys.argv) != 2 or sys.argv[1] == "--help":
        usage()
    target_dir = sys.argv[1]
    if os.path.exists(target_dir) and not os.path.isdir(target_dir):
        raise RuntimeError(f"{target_dir} is not a directory")
    os.makedirs(target_dir, exist_ok=True)

    config = read_configure()
    print(config)
    # TODO Copy appropriate readmes
#EXTRA_DIST = \
# COPYING-DOCS \
# README.org \
# README.windows \
# org.gnome.pan.desktop.in \
# org.gnome.pan.metainfo.xml.in \
# pan-git.version \
# $(man_MANS) \
# $(NULL)

# NEWS

    # Copy executable to target dir
    executable = "pan/gui/pan.exe"
    global dlls
    dlls = copy_executable(executable, target_dir)

    copy_tree(target_dir=target_dir, tree="lib/gdk-pixbuf-2.0")
    # + share/locale/en_GB/LC_MESSAGES/gdk-pixbuf.mo?

    # Deal with magically autoloaded stuff
    #------------------------------------------------------------------------

    # We need this to run shells
    dlls |= copy_executable(
        os.path.join(os.environ["MSYSTEM_PREFIX"], "bin/gspawn-win64-helper.exe"),
        target_dir
    )

    # ------------ gtk2
    if config["gtk_version"] == 2:
        copy_tree(target_dir=target_dir, tree="lib/gtk-2.0/2.10.0/engines")
        # + share/locale/en_GB/LC_MESSAGES/gtk20*?

    # None of these appear to be necessary so why do we have them?
    # However, the release pan version has very slightly nicer fonts, so need
    # further check (though it might be this version or something)
    # add etc/fonts?
    # add etc/gtk-2.0
    # add etc/pango

    # ------------ gtk3
    #if config["gtk_version"] == 3:

    #These aren't necessary if you want a windows theme
    #really? they don't seem gtk specific either
    #
    # Copy Icon themes
    # `cp -r /mingw64/share/icons/* ./share/icons/
    # Copy settins schemas
    # `cp /mingw64/share/glib-2.0/schemas/* ./share/glib-2.0/schemas/`
    # `glib-compile-schemas.exe ./share/glib-2.0/schemas/`

    # See https://www.gtk.org/docs/installations/windows/ also


    # ------------ all gtk versions
    copy_tree(target_dir=target_dir, tree="share/themes")

    if config["spellcheck"]:
        copy_tree(target_dir=target_dir, tree="lib/enchant-2")
        copy_tree(target_dir=target_dir, tree="share/enchant")
        copy_tree(target_dir=target_dir, tree="share/hunspell")
        # also share/iso-codes ?
        # maybe I should use pacman directly for some of this?
        copy_tree(target_dir=target_dir, tree="share/xml/iso-codes")
        # Also possibly share/iso-codes and the whole of share/locale
        # Maybe we should extract the pacman file directly?
        # tar -xvf /var/cache/pacman/pkg/mingw-w64-x86_64-iso-codes-4.9.0-3-any.pkg.tar.zst
        #   -C <target_dir> --strip-components 1

    #------------------------------------------------------------------------

    # Possibly copy the whole of /mingw64share/locale?
    # May be worth while installing the packages installed to build this with the tar
    # command above?

    # Now we copy all the pan <lang>.gmo files in the po directory to the right place in
    # <target>/locale/<dir>/LC_MESSAGES/pan.mo. This may or may not be correct for windows,
    # as the existing install appears to set up registry keys.
    locale_dir = os.path.join(target_dir, 'share', 'locale')
    for gmo in glob.glob("po/*.gmo"):
        name = os.path.basename(gmo)
        lang = os.path.splitext(name)[0]
        dest_dir = os.path.join(locale_dir, lang, "LC_MESSAGES")
        os.makedirs(dest_dir, exist_ok=True)
        shutil.copy2(gmo, os.path.join(dest_dir, 'pan.mo'))


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
