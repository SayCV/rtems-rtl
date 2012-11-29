/*
 *  COPYRIGHT (c) 2012 Chris Johns <chrisj@rtems.org>
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.com/license/LICENSE.
 */
/**
 * @file
 *
 * @ingroup rtems_rtld
 *
 * @brief RTEMS Run-Time Link Editor
 *
 * This is the RAP format loader support..
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <rtl.h>
#include "rtl-elf.h"
#include "rtl-error.h"
#include "rtl-obj-comp.h"
#include "rtl-rap.h"
#include "rtl-trace.h"
#include "rtl-unresolved.h"

/**
 * The offsets in the unresolved array.
 */
#define REL_R_OFFSET (0)
#define REL_R_INFO   (1)
#define REL_R_ADDEND (2)

/**
 * The section definitions found in a RAP file.
 */
typedef struct rtems_rtl_rap_sectdef_s
{
  const char*    name;    /**< Name of the section. */
  const uint32_t flags;   /**< Section flags. */
} rtems_rtl_rap_sectdef_t;

/**
 * The sections as loaded from a RAP file.
 */
static const rtems_rtl_rap_sectdef_t rap_sections[6] =
{
  { ".text",  RTEMS_RTL_OBJ_SECT_TEXT  | RTEMS_RTL_OBJ_SECT_LOAD },
  { ".const", RTEMS_RTL_OBJ_SECT_CONST | RTEMS_RTL_OBJ_SECT_LOAD },
  { ".ctor",  RTEMS_RTL_OBJ_SECT_CONST | RTEMS_RTL_OBJ_SECT_LOAD | RTEMS_RTL_OBJ_SECT_CTOR },
  { ".dtor",  RTEMS_RTL_OBJ_SECT_CONST | RTEMS_RTL_OBJ_SECT_LOAD | RTEMS_RTL_OBJ_SECT_DTOR },
  { ".data",  RTEMS_RTL_OBJ_SECT_DATA  | RTEMS_RTL_OBJ_SECT_LOAD },
  { ".bss",   RTEMS_RTL_OBJ_SECT_BSS   | RTEMS_RTL_OBJ_SECT_ZERO }
};

/**
 * The section indexes. These are fixed.
 */
#define RTEMS_RTL_RAP_TEXT_SEC  (0)
#define RTEMS_RTL_RAP_CONST_SEC (1)
#define RTEMS_RTL_RAP_CTOR_SEC  (2)
#define RTEMS_RTL_RAP_DTOR_SEC  (3)
#define RTEMS_RTL_RAP_DATA_SEC  (4)
#define RTEMS_RTL_RAP_BSS_SEC   (5)
#define RTEMS_RTL_RAP_SECS      (6)

/**
 * The number of RAP sections to load.
 */
#define RAP_SECTIONS (sizeof (rap_sections) / sizeof (rtems_rtl_rap_section_t))

/**
 * The section definitions found in a RAP file.
 */
typedef struct rtems_rtl_rap_section_s
{
  uint32_t size;       /**< The size of the section. */
  uint32_t alignment;  /**< The alignment of the section. */
  uint32_t offset;     /**< The offset of the section. */
} rtems_rtl_rap_section_t;

/**
 * The RAP loader.
 */
typedef struct rtems_rtl_rap_s
{
  rtems_rtl_obj_cache_t*  file;         /**< The file cache for the RAP file. */
  rtems_rtl_obj_comp_t*   decomp;       /**< The decompression streamer. */
  uint32_t                length;       /**< The file length. */
  uint32_t                version;      /**< The RAP file version. */
  uint32_t                compression;  /**< The type of compression. */
  uint32_t                checksum;     /**< The checksum. */
  uint32_t                machinetype;  /**< The ELF machine type. */
  uint32_t                datatype;     /**< The ELF data type. */
  uint32_t                class;        /**< The ELF class. */
  rtems_rtl_rap_section_t secs[RTEMS_RTL_RAP_SECS]; /**< The sections. */
} rtems_rtl_rap_t;

/**
 * Check the machine type.
 */
static bool
rtems_rtl_rap_machine_check (uint32_t machinetype)
{
  /*
   * This code is determined by the machine headers.
   */
  switch (machinetype)
  {
    ELFDEFNNAME (MACHDEP_ID_CASES)
    default:
      return false;
  }
  return true;
}

/**
 * Check the data type.
 */
static bool
rtems_rtl_rap_datatype_check (uint32_t datatype)
{
  /*
   * This code is determined by the machine headers.
   */
  if (datatype != ELFDEFNNAME (MACHDEP_ENDIANNESS))
    return false;
  return true;
}

/**
 * Check the class of executable.
 */
static bool
rtems_rtl_rap_class_check (uint32_t class)
{
  /*
   * This code is determined by the machine headers.
   */
  switch (class)
  {
    case ELFCLASS32:
      if (ARCH_ELFSIZE == 32)
        return true;
      break;
    case ELFCLASS64:
      if (ARCH_ELFSIZE == 64)
        return true;
      break;
    default:
      break;
  }
  return false;
}

static uint32_t
rtems_rtl_rap_get_uint32 (const uint8_t* buffer)
{
  return (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
}

static bool
rtems_rtl_rap_read_uint32 (rtems_rtl_obj_comp_t* comp, uint32_t* value)
{
  uint8_t buffer[sizeof (uint32_t)];

  if (!rtems_rtl_obj_comp_read (comp, buffer, sizeof (uint32_t)))
    return false;

  *value = rtems_rtl_rap_get_uint32 (buffer);

  return true;
}

static bool
rtems_rtl_rap_symbols (rtems_rtl_obj_t*      obj,
                       int                   fd,
                       rtems_rtl_obj_sect_t* sect,
                       void*                 data)
{
  return true;
}

static bool
rtems_rtl_rap_relocator (rtems_rtl_obj_t*      obj,
                         int                   fd,
                         rtems_rtl_obj_sect_t* sect,
                         void*                 data)
{
  return true;
}

static bool
rtems_rtl_rap_parse_header (uint8_t*  rhdr,
                            size_t*   rhdr_len,
                            uint32_t* length,
                            uint32_t* version,
                            uint32_t* compression,
                            uint32_t* checksum)
{
  char* sptr = (char*) rhdr;
  char* eptr;

  *rhdr_len = 0;

  /*
   * "RAP," = 4 bytes, total 4
   */

  if ((rhdr[0] != 'R') || (rhdr[1] != 'A') || (rhdr[2] != 'P') || (rhdr[3] != ','))
    return false;

  sptr = sptr + 4;

  /*
   * "00000000," = 9 bytes, total 13
   */

  *length = strtoul (sptr, &eptr, 10);

  if (*eptr != ',')
    return false;

  sptr = eptr + 1;

  /*
   * "0000," = 5 bytes, total 18
   */

  *version = strtoul (sptr, &eptr, 10);

  if (*eptr != ',')
    return false;

  sptr = eptr + 1;

  /*
   * "NONE," and "LZ77," = 5 bytes, total 23
   */

  if ((sptr[0] == 'N') &&
      (sptr[1] == 'O') &&
      (sptr[2] == 'N') &&
      (sptr[3] == 'E'))
  {
    *compression = RTEMS_RTL_COMP_NONE;
    eptr = sptr + 4;
  }
  else if ((sptr[0] == 'L') &&
           (sptr[1] == 'Z') &&
           (sptr[2] == '7') &&
           (sptr[3] == '7'))
  {
    *compression = RTEMS_RTL_COMP_LZ77;
    eptr = sptr + 4;
  }
  else
    return false;

  if (*eptr != ',')
    return false;

  sptr = eptr + 1;

  /*
   * "00000000," = 9 bytes, total 32
   */
  *checksum = strtoul (sptr, &eptr, 16);

  /*
   * "\n" = 1 byte, total 33
   */
  if (*eptr != '\n')
    return false;

  *rhdr_len = ((uint8_t*) eptr) - rhdr + 1;

  return true;
}

bool
rtems_rtl_rap_file_check (rtems_rtl_obj_t* obj, int fd)
{
  rtems_rtl_obj_cache_t* header;
  uint8_t*               rhdr = NULL;
  size_t                 rlen = 64;
  uint32_t               length = 0;
  uint32_t               version = 0;
  uint32_t               compression = 0;
  uint32_t               checksum = 0;

  rtems_rtl_obj_caches (&header, NULL, NULL);

  if (!rtems_rtl_obj_cache_read (header, fd, obj->ooffset,
                                 (void**) &rhdr, &rlen))
    return false;

  if (!rtems_rtl_rap_parse_header (rhdr,
                                   &rlen,
                                   &length,
                                   &version,
                                   &compression,
                                   &checksum))
    return false;

  return true;
}

bool
rtems_rtl_rap_file_load (rtems_rtl_obj_t* obj, int fd)
{
  rtems_rtl_rap_t rap = { 0 };
  uint8_t*        rhdr = NULL;
  size_t          rlen = 64;
  int             section;

  rtems_rtl_obj_caches (&rap.file, NULL, NULL);

  if (!rtems_rtl_obj_cache_read (rap.file, fd, obj->ooffset,
                                 (void**) &rhdr, &rlen))
    return false;

  if (!rtems_rtl_rap_parse_header (rhdr,
                                   &rlen,
                                   &rap.length,
                                   &rap.version,
                                   &rap.compression,
                                   &rap.checksum))
  {
    rtems_rtl_set_error (EINVAL, "invalid RAP file format");
    return false;
  }

  /*
   * Set up the decompressor.
   */
  rtems_rtl_obj_comp (&rap.decomp, rap.file, fd, rap.compression, rlen);

  /*
   * uint32_t: machinetype
   * uint32_t: datatype
   * uint32_t: class
   */

  if (!rtems_rtl_rap_read_uint32 (rap.decomp, &rap.machinetype))
    return false;

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_LOAD))
    printf ("rtl: rap: machinetype=%lu\n", rap.machinetype);

  if (!rtems_rtl_rap_machine_check (rap.machinetype))
  {
    rtems_rtl_set_error (EINVAL, "invalid machinetype");
    return false;
  }

  if (!rtems_rtl_rap_read_uint32 (rap.decomp, &rap.datatype))
    return false;

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_LOAD))
    printf ("rtl: rap: datatype=%lu\n", rap.datatype);

  if (!rtems_rtl_rap_datatype_check (rap.datatype))
  {
    rtems_rtl_set_error (EINVAL, "invalid datatype");
    return false;
  }

  if (!rtems_rtl_rap_read_uint32 (rap.decomp, &rap.class))
    return false;

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_LOAD))
    printf ("rtl: rap: class=%lu\n", rap.class);

  if (!rtems_rtl_rap_class_check (rap.class))
  {
    rtems_rtl_set_error (EINVAL, "invalid class");
    return false;
  }

  /*
   * uint32_t: text_size
   * uint32_t: text_alignment
   * uint32_t: text_offset
   * uint32_t: const_size
   * uint32_t: const_alignment
   * uint32_t: const_offset
   * uint32_t: ctor_size
   * uint32_t: ctor_alignment
   * uint32_t: ctor_offset
   * uint32_t: dtor_size
   * uint32_t: dtor_alignment
   * uint32_t: dtor_offset
   * uint32_t: data_size
   * uint32_t: data_alignment
   * uint32_t: data_offset
   * uint32_t: bss_size
   * uint32_t: bss_alignment
   * uint32_t: bss_offset
   */

  for (section = 0; section < RAP_SECTIONS; ++section)
  {
    if (!rtems_rtl_rap_read_uint32 (rap.decomp, &rap.secs[section].size))
      return false;

    if (!rtems_rtl_rap_read_uint32 (rap.decomp, &rap.secs[section].alignment))
      return false;

    if (!rtems_rtl_rap_read_uint32 (rap.decomp, &rap.secs[section].offset))
      return false;

    if (rtems_rtl_trace (RTEMS_RTL_TRACE_LOAD))
      printf ("rtl: rap: %s: size=%lu align=%lu off=%lu\n",
              rap_sections[section].name,
              rap.secs[section].size,
              rap.secs[section].alignment,
              rap.secs[section].offset);

    if (!rtems_rtl_obj_add_section (obj,
                                    section,
                                    rap_sections[section].name,
                                    rap.secs[section].size,
                                    rap.secs[section].offset,
                                    rap.secs[section].alignment,
                                    0, 0,
                                    rap_sections[section].flags))
      return false;
  }

  /** obj->entry = (void*)(uintptr_t) ehdr.e_entry; */

  if (!rtems_rtl_obj_load_sections (obj, fd))
    return false;

  if (!rtems_rtl_obj_load_symbols (obj, fd, rtems_rtl_rap_symbols, &rap))
    return false;

  if (!rtems_rtl_obj_relocate (obj, fd, rtems_rtl_rap_relocator, &rap))
    return false;

  return true;
}
