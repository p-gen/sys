/* ======================================================================== */
/*  CRC-16 routines                                     J. Zbiciak, 2001    */
/* ------------------------------------------------------------------------ */
/*  The contents of this file are hereby released into the public domain.   */
/*                                                                          */
/*  Programs are free to use the CRC-16 functions contained in this file    */
/*  for whatever purpose they desire, with no strings attached.             */
/* ======================================================================== */

#ifndef CRC16_H
#define CRC16_H

/* ======================================================================== */
/*  CRC16_TBL    -- Lookup table used for the CRC-16 code.                  */
/* ======================================================================== */
extern const uint16_t crc16_tbl[256];

/* ======================================================================== */
/*  CRC16_UPDATE -- Updates a 16-bit CRC using the lookup table above.      */
/*                  Note:  The 16-bit CRC is set up as a left-shifting      */
/*                  CRC with no inversions.                                 */
/*                                                                          */
/*                  All-caps version is a macro for stuff that can use it.  */
/* ======================================================================== */
uint16_t
crc16_update(uint16_t crc, uint8_t *data, size_t start, size_t end);

#endif
