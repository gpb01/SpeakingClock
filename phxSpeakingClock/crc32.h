/*
   CRC32, a standard CRC32 computation function

   (c) 2022 Guglielmo Braguglia
   Phoenix Sistemi & Automazione s.a.g.l. - Muralto - Switzerland

*/

#ifndef _CRC32_
#define _CRC32_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
uint32_t calc_crc32 ( uint8_t*, uint16_t );

#ifdef __cplusplus
}
#endif

#endif
