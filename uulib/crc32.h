#ifndef __CRC32_H__
#define __CRC32_H__

#ifndef _ANSI_ARGS_
#ifdef PROTOTYPES
#define _ANSI_ARGS_(c)	c
#else
#define _ANSI_ARGS_(c)	()
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long crc32_t;
#undef Z_NULL
#define Z_NULL nullptr

#ifdef __cplusplus
}
#endif
#endif
