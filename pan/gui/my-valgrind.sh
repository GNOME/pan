#/bin/sh
export G_SLICE=always-malloc
export G_DEBUG=gc-friendly
export GLIBCXX_FORCE_NEW=1
valgrind --tool=memcheck --leak-check=full --leak-resolution=high --num-callers=42 --log-file=pan-valgrind --show-reachable=yes ./pan
