#!python3
""" wininstall

Usage:
    wininstall [--build_dir <build_dir>] <install_dir>
    wininstall (-h|--help)

Options:
    -h, --help                  Show help and exit
    --build_dir=<build_dir>     Build directory [default: std-build]

This script provides a way of creating a standalone pan for windows.

Once you have built and tested pan, run this with a target folder, and all
needed files and quite possibly some unneeded ones will be copied there, and
that can be run standalone or used to generate an installer.

AWFUL WARNINGS:

This isn't going to be very stable and needs checking every new version of pan
or every update of a package.

Unfortunately may packages come with executables and libraries, and there's no
indication of what part of the package is a utility and what part is runtime
that other packages may use (see gettext in particular). This means I can't
actually use the dependencies you can get from pacman as you'd end up installing
way too much.

"""
from __future__ import annotations

import glob
import os
import re
import shutil
import subprocess
import sys
from docopt import docopt

class Copier:
    """A class that copies files and logs the results."""

    def __init__(self, target_dir: str):
        """Construct Copier class

        Args:
            target_dir: place to copy files
        """
        self._copied_files = set()
        self._dlls = set()
        self._installed_packages = set()
        self._target_dir = target_dir

    def copy_and_check_dlls(
        self,
        source: str,
        target_dir: str = None,
        *,
        verbose: bool = True
    ):
        """Copy file and work out what dlls we need to copy later

        The required dlls are stored away for later.

        Args:
            source: File to copy
            target_dir: Where to copy it!
            verbose: print message if set
        """
        if source in self._copied_files:
            return

        if target_dir is None:
            target_dir = self._target_dir

        if verbose:
            print(f"Copying {source} to {target_dir}")

        shutil.copy2(source, target_dir)
        self._copied_files.add(source)

        if source in self._dlls:
            # We've copied a dll someone refers to. We can remove it from
            # the list of dlls to install
            self._dlls.remove(source)
            return

        if os.path.splitext(source)[1] not in (".dll", ".exe"):
            return

        # Get all the dlls we need in the root
        output = subprocess.run(
            [ "ldd", source ], capture_output=True, text=True, check=True
        )
        prefix = os.environ['MSYSTEM'].lower()
        for line in output.stdout.splitlines():
            dll = re.search(r'(/' + prefix + r'/.*\.dll)', line)
            if dll:
                dll = self.convert_name_to_windows(dll.group())
                if dll not in self._copied_files:
                    self._dlls.add(dll)

    @staticmethod
    def convert_name_to_windows(file: str) -> str:
        """Converts /mingw name to windows name.

        Args:
            file: filename starting with /mingw..

        Returns:
            filename starting with c:/....
        """
        return file.replace(
            f'/{os.environ["MSYSTEM"].lower()}',
            os.environ["MSYSTEM_PREFIX"],
            1
        )

    def copy_package(
        self,
        name: str,
        *,
        library: str = None,
        include: list(str) = None,
        exclude: list(str) = None,
        files: list(str) = None,
        verbose: bool = False,
        recopy: bool = False
    ):
        """Copies a package to target directory.

        This gets a list of files installed with the package and copies them to
        the target directory

        Some directories are included/excluded by default. These may be
        overridden by specifiying include and exclude

        It's done like this because all of these packages seem to come with
        loads of extraneous stuff, and it's not awfully clear what is or isn't
        needed for a standalone executable

        NB Those parameters should be any iterable or sequence?

        Args:
            name: Package name
            library: Actual name of library if different to package name
            include: directories to include
            exclude: directories to exclude
            files: files to include
            verbose: print filenames when copied
            recopy: allow the library to be installed a 2nd time.
        """
        if library is None:
            library = name

        package = f"{os.environ['MINGW_PACKAGE_PREFIX']}-{name}"
        print(f"Installing {package} in {self._target_dir}")
        output = subprocess.run(
            [ "pacman", "-Q", "-l", package ],
            capture_output=True,
            text=True,
            check=True
        )

        # Default directories to copy
        # Possibly you could include anything in "lib/{package}*"?
        dirs = set((
            "etc",
            f"lib/{library}",
            f"share/{library}",
            f"share/{name}",
            "share/icons",
            "share/glib-2.0/schemas",
            "share/licenses",
            "share/locale",
            "share/themes",
            "share/xml"
        ))
        # Extras
        if include is not None:
            if isinstance(include, str):
                dirs.add(include)
            else:
                dirs |= set(include)

        # Turn into filenames
        dirs = set(
            os.path.join(os.environ["MSYSTEM_PREFIX"], dir) + "/"
                for dir in dirs
        )

        # List of files to copy
        if files is None:
            files = ()
        elif isinstance(files, str):
            files = (files, )
        # Turn into filenames
        files = set(
            os.path.join(os.environ["MSYSTEM_PREFIX"], file)
                for file in files
        )

        # List of directories to exclude
        if exclude is None:
            exclude=()
        elif isinstance(exclude, str):
            exclude = (exclude, )
        # Turn into filenames
        exclude = set(
            os.path.join(os.environ["MSYSTEM_PREFIX"], dir) + "/"
                for dir in exclude
        )

        for line in output.stdout.splitlines():
            file = self.convert_name_to_windows(line.split()[-1])

            if os.path.isdir(file) or file.endswith(".a"):
                # Ignore directories and .a files for distribution
                continue

            if file in self._dlls:
                # This is a needed dll. This goes direct to target directory
                self.copy_and_check_dlls(
                    file, self._target_dir, verbose=verbose
                )
                continue

            # FIXME This could be simplified
            copy = False

            if file in files:
                copy = True
            else:
                if not any((file.startswith(dir) for dir in exclude)):
                    copy = any((file.startswith(dir) for dir in dirs))

            if copy:
                outdir = os.path.join(
                    self._target_dir,
                    os.path.dirname(
                        file[len(os.environ["MSYSTEM_PREFIX"]) + 1:]
                    )
                )
                os.makedirs(outdir, exist_ok=True)
                self.copy_and_check_dlls(file, outdir, verbose=verbose)
            else:
                if verbose:
                    print("ignored " + file)

        if not recopy:
            self._installed_packages.add(name)


    def _get_packages_containing(self, paths: list(str)) -> set(str):
        """ Get all the packages which lay claim to specified paths.

        Arguments:
            paths: Any iterable or a string

        Returns:
            The packages laying claim to those files or directories.

            Packages already installed are filtered out.
        """
        if isinstance(paths, str):
            paths = (paths, )

        output = subprocess.run(
            [ "pacman", "-Q", "-o" ] + list(paths),
            capture_output=True,
            text=True,
            check=True
        )

        packages = set()
        for line in output.stdout.splitlines():
            # Each line is of form
            # <dir> is owned by <package> <version>
            package = line.split()[-2].replace(
                os.environ["MINGW_PACKAGE_PREFIX"] + "-", ""
            )
            packages.add(package)

        return packages

    def get_packages_containing(self, paths: list(str)) -> set(str):
        """Get all the packages required for the dlls we need to load.

        Returns:
            Packages needed
        """
        packages = self._get_packages_containing(paths)
        return set(
            [
                package for package in packages
                    if not package in self._installed_packages
            ]
        )

    def get_needed_packages(self) -> set(str):
        """Get all the packages required for the dlls we need to load.

        Returns:
            Packages needed

        A Note: This isn't recursive, so on installing packages, it's possible
        that it may be necessary to install other packages.
        """
        if len(self._dlls) == 0:
            return set()

        packages = self._get_packages_containing(self._dlls)
        for package in packages:
            # Umm. Maybe we shouldn't care and reinstall at least the dll
            # from the package.
            if package in self._installed_packages:
                print(f"Need {package} from one of {self._dlls} which is already installed")
                exit(1)
        return packages


def read_configure(build_dir: str) -> dict[str, str]:
    dbus = False
    gmime_crypto = False
    spellcheck = False
    gtk_version = 3
# webkit?
    tls_support = False
    popup_notifications = False
    password_storage = False
# yelp-tools ?
    manual = False

    # These are in the order output by configure
    with open(os.path.join(build_dir, "config.h")) as config:
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
    args = docopt(__doc__, options_first=True)
    print(args)

    target_dir = args["<install_dir>"]
    if os.path.exists(target_dir) and not os.path.isdir(target_dir):
        raise RuntimeError(f"{target_dir} is not a directory")
    os.makedirs(target_dir, exist_ok=True)

    build_dir = args["--build_dir"]
    config = read_configure(build_dir)
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
    copier = Copier(target_dir)

    copier.copy_and_check_dlls(os.path.join(build_dir, "pan/gui/pan.exe"))

    # Now we copy all the pan <lang>.gmo files in the po directory to the right
    # place in <target>/locale/<lang>/LC_MESSAGES/pan.mo.
    # This may or may not be correct for windows, as the existing install
    # appears to set up registry keys.
    print("Copying pan locale files")
    locale_dir = os.path.join(target_dir, 'share', 'locale')
    for gmo in glob.glob("po/*.gmo"):
        name = os.path.basename(gmo)
        lang = os.path.splitext(name)[0]
        dest_dir = os.path.join(locale_dir, lang, "LC_MESSAGES")
        os.makedirs(dest_dir, exist_ok=True)
        shutil.copy2(gmo, os.path.join(dest_dir, 'pan.mo'))

    # We need this to run shells
    copier.copy_and_check_dlls(
        os.path.join(
            os.environ["MSYSTEM_PREFIX"], "bin/gspawn-win64-helper.exe"
        )
    )

    # We also need to copy the icons. Just moving the .png ones for now
    print("Copying icons")
    dest_dir = os.path.join(target_dir, 'pan', 'icons')
    os.makedirs(dest_dir, exist_ok=True)
    for file in glob.iglob(os.path.join("pan", "icons", "*.png")):
        if os.path.isfile(file):
            shutil.copy2(file, dest_dir)

    # Arguably we could look at the dlls we now have and load up all the
    # packages they come from and the dependent packages. However, the actual
    # packages for most of these contain a bunch of executables and libraries,
    # which might drag in other packages and we don't really want to install
    # those.
    pixbuf_lib = "gdk-pixbuf-2.0"

    while len(packages := copier.get_needed_packages()) != 0:

        for package in sorted(packages):
            if package == "aspell":
                copier.copy_package(package, library="aspell-0.60")
            elif package == "enchant":
                copier.copy_package(package, library="enchant-2")
                copier.copy_package(
                    "hunspell-en", library="hunspell", include="share/doc"
                )
                # Should we include myspell in the above? seems to work OK
                # without it
                copier.copy_package("iso-codes")
            elif package == "gdk-pixbuf2":
                copier.copy_package(package, library=pixbuf_lib)
                # Copy any other packages that install in the pixbuf library
                for package in copier.get_packages_containing(
                    os.path.join(
                        os.environ["MSYSTEM_PREFIX"], "lib", pixbuf_lib
                    )
                ):
                    copier.copy_package(
                        package, library=pixbuf_lib, recopy=True
                    )
            elif package == "gettext":
                # FIXME don't copy the gettext-tools locale files
                # This would be a whole lot easier if gettext tools and gettext
                # runtime were separate packages.
                copier.copy_package(
                    package,
                    library="None",
                    exclude=(f"share/{package}", "share/licenses"),
                    files=f"share/licenses/{package}/gettext-runtime/intl/COPYING.LIB"
                )
            elif package == "gtk2":
                copier.copy_package(package, library="gtk-2.0/2.10.0")
            elif package == "gtk3":
                copier.copy_package(package, library="gtk-3.0")
                copier.copy_package("adwaita-icon-theme")

                with open(
                    os.path.join(target_dir, 'etc', 'gtk-3.0', 'settings.ini'),
                    'w'
                ) as settings:
                    print("""
[Settings]
gtk-theme-name=win32

gtk-menu-images = true
gtk-overlay-scrolling = false
""", file=settings)

            elif package == "graphite2":
                copier.copy_package(package, exclude=f"share/{package}")
            elif package == "icu":
                copier.copy_package(
                    package,
                    library="None",
                    exclude=f"share/{package}",
                    files=f"share/{package}/69.1/LICENSE"
                )
            elif package == "libgpg-error":
                copier.copy_package(package, exclude=f"share/{package}")
            elif package == "libjpeg-turbo":
                # This includes source code in some pretty odd places
                copier.copy_package(
                    package,
                    exclude=f"share/licenses/{package}/simd"
                )
            else:
                copier.copy_package(package)

    # ---- gdk-pixbuf2 cleanup
    # Knowing where the loader cache file exists is not ideal, but it needs to
    # exist and I'm not entirely convinced about copying the generated one.
    loader_path = os.path.join(target_dir, 'lib', pixbuf_lib, '2.10.0')
    output = subprocess.run(
        [ "gdk-pixbuf-query-loaders.exe" ],
        env=dict(
            os.environ,
            GDK_PIXBUF_MODULEDIR=os.path.join(loader_path, "loaders")
        ),
        capture_output=True,
        text=True,
        check=True
    )

    # replace all the paths with windows paths relative to target
    with open(os.path.join(loader_path, 'loaders.cache'), 'w') as cache:
        for line in output.stdout.splitlines():
            if target_dir in line:
                line = line.replace(target_dir + "/", "")
                line = line.replace("/", r"\\")
            print(line, file=cache)

    # ---- glib2 cleanup
    # setting schemas - must be run after all packages installed.
    output = subprocess.run(
        [
            "glib-compile-schemas.exe",
            os.path.join(target_dir, 'share', 'glib-2.0', 'schemas')
        ],
        check=True
    )

if __name__ == "__main__":
    main()
