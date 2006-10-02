#/bin/sh
export G_SLICE=always-malloc
export G_DEBUG=gc-friendly
export GLIBCXX_FORCE_NEW=1
valgrind --tool=massif --depth=8 --num-callers=8 --alloc-fn=g_malloc --alloc-fn=g_realloc --alloc-fn=g_try_malloc --alloc-fn=g_malloc0 --alloc-fn=g_mem_chunk_alloc ./pan
