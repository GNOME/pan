/* crc32.c -- compute the CRC-32 of a data stream
 * using 8-slicing algorithm by INTEL
 */

#include <stddef.h> /* for NULL */
#include "crc32.h"

static unsigned long crc32Lookup[8][256];
static const unsigned long Polynomial = 0xEDB88320;

void
init_crc()
{
  unsigned int i,j;

  for (i = 0; i <= 0xFF; i++)
  {
    unsigned long crc = i;
    for (j = 0; j < 8; j++)
      crc = (crc >> 1) ^ ((crc & 1) * Polynomial);
    crc32Lookup[0][i] = crc;
  }
  for (i = 0; i <= 0xFF; i++)
  {
    crc32Lookup[1][i] = (crc32Lookup[0][i] >> 8)
        ^ crc32Lookup[0][crc32Lookup[0][i] & 0xFF];
    crc32Lookup[2][i] = (crc32Lookup[1][i] >> 8)
        ^ crc32Lookup[0][crc32Lookup[1][i] & 0xFF];
    crc32Lookup[3][i] = (crc32Lookup[2][i] >> 8)
        ^ crc32Lookup[0][crc32Lookup[2][i] & 0xFF];
    crc32Lookup[4][i] = (crc32Lookup[3][i] >> 8)
        ^ crc32Lookup[0][crc32Lookup[3][i] & 0xFF];
    crc32Lookup[5][i] = (crc32Lookup[4][i] >> 8)
        ^ crc32Lookup[0][crc32Lookup[4][i] & 0xFF];
    crc32Lookup[6][i] = (crc32Lookup[5][i] >> 8)
        ^ crc32Lookup[0][crc32Lookup[5][i] & 0xFF];
    crc32Lookup[7][i] = (crc32Lookup[6][i] >> 8)
        ^ crc32Lookup[0][crc32Lookup[6][i] & 0xFF];
  }
}

unsigned long
_crc32(const void* data, size_t length, unsigned long previousCrc32)
{

  unsigned long* current = (unsigned long*) data;
  unsigned long crc = ~previousCrc32;
  while (length >= 8)
  {
    unsigned long one = *current++ ^ crc;
    unsigned long two = *current++;
    crc = crc32Lookup[7][one & 0xFF] ^ crc32Lookup[6][(one >> 8) & 0xFF]
        ^ crc32Lookup[5][(one >> 16) & 0xFF] ^ crc32Lookup[4][one >> 24]
        ^ crc32Lookup[3][two & 0xFF] ^ crc32Lookup[2][(two >> 8) & 0xFF]
        ^ crc32Lookup[1][(two >> 16) & 0xFF] ^ crc32Lookup[0][two >> 24];
    length -= 8;
  }
  unsigned char* currentChar = (unsigned char*) current;
  while (length--)
    crc = (crc >> 8) ^ crc32Lookup[0][(crc & 0xFF) ^ *currentChar++];
  return ~crc;
}

unsigned long
crc32(unsigned long crc, const unsigned char * buf, unsigned int len)
{
  if (buf == NULL )
    return 0UL;
  return _crc32(buf, len, crc);
}
