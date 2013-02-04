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
#define Z_NULL  0

#ifdef __cplusplus
}
#endif
#endif
