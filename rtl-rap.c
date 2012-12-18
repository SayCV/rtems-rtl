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
 * The sections as loaded from a RAP file.
 */
static const rtems_rtl_rap_sectdef_t rap_sections[RTEMS_RTL_RAP_SECS] =
{
  { ".text",  RTEMS_RTL_OBJ_SECT_TEXT  | RTEMS_RTL_OBJ_SECT_LOAD },
  { ".const", RTEMS_RTL_OBJ_SECT_CONST | RTEMS_RTL_OBJ_SECT_LOAD },
  { ".ctor",  RTEMS_RTL_OBJ_SECT_CONST | RTEMS_RTL_OBJ_SECT_LOAD | RTEMS_RTL_OBJ_SECT_CTOR },
  { ".dtor",  RTEMS_RTL_OBJ_SECT_CONST | RTEMS_RTL_OBJ_SECT_LOAD | RTEMS_RTL_OBJ_SECT_DTOR },
  { ".data",  RTEMS_RTL_OBJ_SECT_DATA  | RTEMS_RTL_OBJ_SECT_LOAD },
  { ".bss",   RTEMS_RTL_OBJ_SECT_BSS   | RTEMS_RTL_OBJ_SECT_ZERO }
};

/**
 * The section definitions found in a RAP file.
 */
typedef struct rtems_rtl_rap_section_s
{
  uint32_t size;       /**< The size of the section. */
  uint32_t alignment;  /**< The alignment of the section. */
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
  uint32_t                init;         /**< The initialisation strtab offset. */
  uint32_t                fini;         /**< The finish strtab offset. */
  rtems_rtl_rap_section_t secs[RTEMS_RTL_RAP_SECS]; /**< The sections. */
  uint32_t                symtab_size;  /**< The symbol table size. */
  char*                   strtab;       /**< The string table. */
  uint32_t                strtab_size;  /**< The string table size. */
  uint32_t                relocs_size;  /**< The relocation table size. */
  uint32_t                symbols;      /**< The number of symbols. */
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
  uint32_t value = 0;
  int      b;
  for (b = 0; b < sizeof (uint32_t); ++b)
  {
    value <<= 8;
    value |= buffer[b];
  }
  return value;
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
rtems_rtl_rap_loader (rtems_rtl_obj_t*      obj,
                      int                   fd,
                      rtems_rtl_obj_sect_t* sect,
                      void*                 data)
{
  rtems_rtl_rap_t* rap = (rtems_rtl_rap_t*) data;

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_LOAD))
    printf ("rtl: rap: input %s=%lu\n",
            sect->name, rtems_rtl_obj_comp_input (rap->decomp));

  return rtems_rtl_obj_comp_read (rap->decomp, sect->base, sect->size);
}

static bool
rtems_rtl_rap_relocate (rtems_rtl_rap_t* rap, rtems_rtl_obj_t* obj)
{
  #define SYMNAME_BUFFER_SIZE (1024)
  char*    symname_buffer = NULL;
  int      section;

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
    printf ("rtl: relocation\n");

  symname_buffer = malloc (SYMNAME_BUFFER_SIZE);
  if (!symname_buffer)
  {
    rtems_rtl_set_error (ENOMEM, "no memory for local symbol name buffer");
    return false;
  }

  for (section = 0; section < RTEMS_RTL_RAP_SECS; ++section)
  {
    rtems_rtl_obj_sect_t*  targetsect;
    uint32_t               header = 0;
    int                    relocs;
    bool                   is_rela;
    int                    r;

    targetsect = rtems_rtl_obj_find_section (obj, rap_sections[section].name);

    if (!targetsect)
    {
      rtems_rtl_set_error (EINVAL, "no target section found");
      free (symname_buffer);
      return false;
    }

    if (!rtems_rtl_rap_read_uint32 (rap->decomp, &header))
    {
      free (symname_buffer);
      return false;
    }

    /*
     * Bit 31 of the header indicates if the relocations for this section
     * have a valid addend field.
     */

    is_rela = (header & (1 << 31)) != 0 ? true : false;
    relocs = header & ~(1 << 31);

    if (relocs && rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
      printf ("rtl: relocation: %s: header: %08lx relocs: %d %s\n",
              rap_sections[section].name,
              header, relocs, is_rela ? "rela" : "rel");

    for (r = 0; r < relocs; ++r)
    {
      uint32_t    info = 0;
      uint32_t    offset = 0;
      uint32_t    addend = 0;
      Elf_Word    type;
      const char* symname = NULL;
      uint32_t    symname_size;
      Elf_Word    symtype = 0;
      Elf_Word    symvalue = 0;

      if (!rtems_rtl_rap_read_uint32 (rap->decomp, &info))
      {
        free (symname_buffer);
        return false;
      }

      if (!rtems_rtl_rap_read_uint32 (rap->decomp, &offset))
      {
        free (symname_buffer);
        return false;
      }

      /*
       * The types are:
       *
       *  0  Section symbol offset in addend.
       *  1  Symbol appended to the relocation record.
       *  2  Symbol is in the strtabl.
       *
       * If type 2 bits 30:8 is the offset in the strtab. If type 1 the bits
       * are the size of the string. The lower 8 bits of the info field if the
       * ELF relocation type field.
       */

      if (((info & (1 << 31)) == 0) || is_rela)
      {
        if (!rtems_rtl_rap_read_uint32 (rap->decomp, &addend))
        {
          free (symname_buffer);
          return false;
        }
      }

      if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
        printf (" %2d: info=%08lx offset=%lu addend=%lu\n",
                r, info, offset, addend);

      type = info & 0xff;

      if ((info & (1 << 31)) == 0)
      {
        rtems_rtl_obj_sect_t* symsect;

        symsect = rtems_rtl_obj_find_section_by_index (obj, info >> 8);
        if (!symsect)
        {
          free (symname_buffer);
          return false;
        }

        symvalue = (Elf_Word) symsect->base + addend;
      }
      else if (rtems_rtl_elf_rel_resolve_sym (type))
      {
        rtems_rtl_obj_sym_t* symbol;

        symname_size = (info & ~(3 << 30)) >> 8;

        if ((info & (1 << 30)) != 0)
        {
          symname = rap->strtab + symname_size;
        }
        else
        {
          if (symname_size > (SYMNAME_BUFFER_SIZE - 1))
          {
            free (symname_buffer);
            rtems_rtl_set_error (EINVAL, "reloc symbol too big");
            return false;
          }

          if (!rtems_rtl_obj_comp_read (rap->decomp, symname_buffer, symname_size))
          {
            free (symname_buffer);
            return false;
          }

          symname_buffer[symname_size] = '\0';
          symname = symname_buffer;
        }

        symbol = rtems_rtl_symbol_obj_find (obj, symname);

        if (!symbol)
        {
          free (symname_buffer);
          rtems_rtl_set_error (EINVAL, "global symbol not found: %s", symname);
          return false;
        }

        symvalue = (Elf_Word) symbol->value;
      }

      if (is_rela)
      {
        Elf_Rela rela;

        rela.r_offset = offset;
        rela.r_info = type;
        rela.r_addend = addend;

        if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
          printf (" %2d: rela: type:%-2d off:%lu addend:%d" \
                  " symname=%s symtype=%lu symvalue=0x%08lx\n",
                  r, (int) type, offset, (int) addend,
                  symname, symtype, symvalue);

        if (!rtems_rtl_elf_relocate_rela (obj, &rela, targetsect,
                                          symname, symtype, symvalue))
        {
          free (symname_buffer);
          return false;
        }
      }
      else
      {
        Elf_Rel rel;

        rel.r_offset = offset;
        rel.r_info = type;

        if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
          printf (" %2d: rel: type:%-2d off:%lu" \
                  " symname=%s symtype=%lu symvalue=0x%08lx\n",
                  r, (int) type, offset,
                  symname, symtype, symvalue);

        if (!rtems_rtl_elf_relocate_rel (obj, &rel, targetsect,
                                         symname, symtype, symvalue))
        {
          free (symname_buffer);
          return false;
        }
      }
    }
  }

  free (symname_buffer);

  return true;
}

static bool
rtems_rtl_rap_load_symbols (rtems_rtl_rap_t* rap, rtems_rtl_obj_t* obj)
{
  rtems_rtl_obj_sym_t* gsym;
  int                  sym;

  obj->global_size =
    rap->symbols * sizeof (rtems_rtl_obj_sym_t) + rap->strtab_size;

  obj->global_table = rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_SYMBOL,
                                           obj->global_size, true);
  if (!obj->global_table)
  {
    obj->global_size = 0;
    rtems_rtl_set_error (ENOMEM, "no memory for obj global syms");
    return false;
  }

  obj->global_syms = rap->symbols;

  rap->strtab = (((char*) obj->global_table) +
                 (rap->symbols * sizeof (rtems_rtl_obj_sym_t)));

  if (!rtems_rtl_obj_comp_read (rap->decomp, rap->strtab, rap->strtab_size))
    return false;

  for (sym = 0, gsym = obj->global_table; sym < rap->symbols; ++sym)
  {
    rtems_rtl_obj_sect_t* symsect;
    uint32_t              data;
    uint32_t              name;
    uint32_t              value;

    if (!rtems_rtl_rap_read_uint32 (rap->decomp, &data) ||
        !rtems_rtl_rap_read_uint32 (rap->decomp, &name) ||
        !rtems_rtl_rap_read_uint32 (rap->decomp, &value))
    {
      free (obj->global_table);
      obj->global_table = NULL;
      obj->global_syms = 0;
      obj->global_size = 0;
      return false;
    }

    if (rtems_rtl_trace (RTEMS_RTL_TRACE_SYMBOL))
      printf ("rtl: sym:load: data=0x%08lx name=0x%08lx value=0x%08lx\n",
              data, name, value);

    /*
     * If there is a globally exported symbol already present and this
     * symbol is not weak raise an error. If the symbol is weak and present
     * globally ignore this symbol and use the global one and if it is not
     * present take this symbol global or weak. We accept the first weak
     * symbol we find and make it globally exported.
     */
    if (rtems_rtl_symbol_global_find (rap->strtab + name) &&
        (ELF_ST_BIND (data & 0xffff) != STB_WEAK))
    {
      free (obj->global_table);
      obj->global_table = NULL;
      obj->global_syms = 0;
      obj->global_size = 0;
      rtems_rtl_set_error (EINVAL,
                           "duplicate global symbol: %s", rap->strtab + name);
      return false;
    }

    symsect = rtems_rtl_obj_find_section_by_index (obj, data >> 16);
    if (!symsect)
    {
      free (obj->global_table);
      obj->global_table = NULL;
      obj->global_syms = 0;
      obj->global_size = 0;
      rtems_rtl_set_error (EINVAL, "section index not found: %lu", data >> 16);
      return false;
    }

    rtems_chain_set_off_chain (&gsym->node);
    gsym->name = rap->strtab + name;
    gsym->value = (uint8_t*) (value + symsect->base);
    gsym->data = data & 0xffff;

    if (rtems_rtl_trace (RTEMS_RTL_TRACE_SYMBOL))
      printf ("rtl: sym:add:%-2d name:%-20s bind:%-2d type:%-2d val:%8p sect:%d\n",
              sym, gsym->name,
              (int) ELF_ST_BIND (data & 0xffff),
              (int) ELF_ST_TYPE (data & 0xffff),
              gsym->value, (int) (data >> 16));

    ++gsym;
  }

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

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_LOAD))
    printf ("rtl: rap: input machine=%lu\n",
            rtems_rtl_obj_comp_input (rap.decomp));

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
   * uint32_t: init
   * uint32_t: fini
   * uint32_t: symtab_size
   * uint32_t: strtab_size
   * uint32_t: relocs_size
   */

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_LOAD))
    printf ("rtl: rap: input header=%lu\n",
            rtems_rtl_obj_comp_input (rap.decomp));

  if (!rtems_rtl_rap_read_uint32 (rap.decomp, &rap.init))
    return false;

  if (!rtems_rtl_rap_read_uint32 (rap.decomp, &rap.fini))
    return false;

  if (!rtems_rtl_rap_read_uint32 (rap.decomp, &rap.symtab_size))
    return false;

  if (!rtems_rtl_rap_read_uint32 (rap.decomp, &rap.strtab_size))
    return false;

  if (!rtems_rtl_rap_read_uint32 (rap.decomp, &rap.relocs_size))
    return false;

  rap.symbols = rap.symtab_size / (3 * sizeof (uint32_t));

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_LOAD))
    printf ("rtl: rap: load: symtab=%lu (%lu) strtab=%lu relocs=%lu\n",
            rap.symtab_size, rap.symbols,
            rap.strtab_size, rap.relocs_size);

  /*
   * uint32_t: text_size
   * uint32_t: text_alignment
   * uint32_t: const_size
   * uint32_t: const_alignment
   * uint32_t: ctor_size
   * uint32_t: ctor_alignment
   * uint32_t: dtor_size
   * uint32_t: dtor_alignment
   * uint32_t: data_size
   * uint32_t: data_alignment
   * uint32_t: bss_size
   * uint32_t: bss_alignment
   */

  for (section = 0; section < RTEMS_RTL_RAP_SECS; ++section)
  {
    if (!rtems_rtl_rap_read_uint32 (rap.decomp, &rap.secs[section].size))
      return false;

    if (!rtems_rtl_rap_read_uint32 (rap.decomp, &rap.secs[section].alignment))
      return false;

    if (rtems_rtl_trace (RTEMS_RTL_TRACE_LOAD_SECT))
      printf ("rtl: rap: %s: size=%lu align=%lu\n",
              rap_sections[section].name,
              rap.secs[section].size,
              rap.secs[section].alignment);

    if (!rtems_rtl_obj_add_section (obj,
                                    section,
                                    rap_sections[section].name,
                                    rap.secs[section].size,
                                    0,
                                    rap.secs[section].alignment,
                                    0, 0,
                                    rap_sections[section].flags))
      return false;
  }

  /** obj->entry = (void*)(uintptr_t) ehdr.e_entry; */

  if (!rtems_rtl_obj_load_sections (obj, fd, rtems_rtl_rap_loader, &rap))
    return false;

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_LOAD))
    printf ("rtl: rap: input symbols=%lu\n",
            rtems_rtl_obj_comp_input (rap.decomp));

  if (!rtems_rtl_rap_load_symbols (&rap, obj))
    return false;

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_LOAD))
    printf ("rtl: rap: input relocs=%lu\n",
            rtems_rtl_obj_comp_input (rap.decomp));

  if (!rtems_rtl_rap_relocate (&rap, obj))
    return false;

  return true;
}
