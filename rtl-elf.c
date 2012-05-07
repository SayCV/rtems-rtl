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
 * This is the RTL implementation. 
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
#include "rtl-trace.h"

static bool
rtems_rtl_elf_machine_check (Elf_Ehdr* ehdr)
{
  /*
   * This code is determined by the NetBSD machine headers.
   */
  switch (ehdr->e_machine)
  {
    ELFDEFNNAME (MACHDEP_ID_CASES)
    default:
      return false;
  }
  return true;
}

bool
rtems_rtl_elf_find_symbol (rtems_rtl_obj_t* obj,
                           const Elf_Sym*   sym,
                           const char*      symname,
                           Elf_Word*        value)
{
  rtems_rtl_obj_sect_t* sect;

  if (ELF_ST_TYPE(sym->st_info) == STT_NOTYPE)
  {
    rtems_rtl_obj_sym_t* symbol = rtems_rtl_symbol_global_find (symname);
    if (!symbol)
    {
      rtems_rtl_set_error (EINVAL, "global symbol not found: %s", symname);
      return false;
    }

    *value = (Elf_Word) symbol->value;
    return true;
  }

  sect = rtems_rtl_obj_find_section_by_index (obj, sym->st_shndx);
  if (!sect)
  {
    rtems_rtl_set_error (EINVAL, "reloc symbol's section not found");
    return false;
  }

  *value = sym->st_value + (Elf_Word) sect->base;
  return true;
}

static bool
rtems_rtl_elf_relocator (rtems_rtl_obj_t*      obj,
                         int                   fd,
                         rtems_rtl_obj_sect_t* sect,
                         void*                 data)
{
  rtems_rtl_obj_cache_t* symbols;
  rtems_rtl_obj_cache_t* strings;
  rtems_rtl_obj_cache_t* relocs;
  rtems_rtl_obj_sect_t*  targetsect;
  rtems_rtl_obj_sect_t*  symsect;
  rtems_rtl_obj_sect_t*  strtab;
  bool                   is_rela;
  size_t                 reloc_size;
  int                    reloc;
  int                    unresolved;

  /*
   * First check if the section the relocations are for exists. If it does not
   * exist ignore these relocations. They are most probably debug sections.
   */
  targetsect = rtems_rtl_obj_find_section_by_index (obj, sect->info);
  if (!targetsect)
    return true;
  
  rtems_rtl_obj_caches (&symbols, &strings, &relocs);

  if (!symbols || !strings || !relocs)
    return false;
  
  symsect = rtems_rtl_obj_find_section (obj, ".symtab");
  if (!symsect)
  {
    rtems_rtl_set_error (EINVAL, "no .symtab section");
    return false;
  }

  strtab = rtems_rtl_obj_find_section (obj, ".strtab");
  if (!strtab)
  {
    rtems_rtl_set_error (EINVAL, "no .strtab section");
    return false;
  }
  
  if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
    printf ("relocation: %s, syms:%s\n", sect->name, symsect->name);

  /*
   * Handle the different relocation record types.
   */
  is_rela = ((sect->flags & RTEMS_RTL_OBJ_SECT_RELA) ==
             RTEMS_RTL_OBJ_SECT_RELA) ? true : false;
  reloc_size = is_rela ? sizeof (Elf_Rela) : sizeof (Elf_Rel);

  unresolved = 0;
  
  for (reloc = 0; reloc < (sect->size / reloc_size); ++reloc)
  {
    uint8_t         relbuf[reloc_size];
    const Elf_Rela* rela = (const Elf_Rela*) relbuf;
    const Elf_Rel*  rel = (const Elf_Rel*) relbuf;
    Elf_Sym         sym;
    const char*     symname = NULL;
    off_t           off;
    Elf_Word        type;
    Elf_Word        symvalue;
    bool            relocate;

    off = obj->ooffset + sect->offset + (reloc * reloc_size);

    if (!rtems_rtl_obj_cache_read_byval (relocs, fd, off,
                                         &relbuf[0], reloc_size))
      return false;

    if (is_rela)
      off = (obj->ooffset + symsect->offset +
             (ELF_R_SYM (rela->r_info) * sizeof (sym)));
    else
      off = (obj->ooffset + symsect->offset +
             (ELF_R_SYM (rel->r_info) * sizeof (sym)));

    if (!rtems_rtl_obj_cache_read_byval (symbols, fd, off,
                                         &sym, sizeof (sym)))
      return false;

    /*
     * Only need the name of the symbol if global.
     */
    if (ELF_ST_TYPE (sym.st_info) == STT_NOTYPE)
    {
      size_t len;
      off = obj->ooffset + strtab->offset + sym.st_name;
      len = RTEMS_RTL_ELF_STRING_MAX;
    
      if (!rtems_rtl_obj_cache_read (strings, fd, off,
                                     (void**) &symname, &len))
        return false;
    }

    /*
     * See if the record references an external symbol. If it does find the
     * symbol value. If the symbol cannot be found flag the object file as
     * having unresolved externals.
     */
    if (is_rela)
      type = ELF_R_TYPE(rela->r_info);
    else
      type = ELF_R_TYPE(rel->r_info);

    relocate = true;
    
    if (rtems_rtl_elf_rel_resolve_sym (type))
    {
      if (!rtems_rtl_elf_find_symbol (obj, &sym, symname, &symvalue))
      {
        ++unresolved;
        relocate = false;
      }
    }

    if (relocate)
    {
      if (is_rela)
      {
        if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
          printf ("rela: sym:%-2d type:%-2d off:%08lx addend:%d\n",
                  (int) ELF_R_SYM (rela->r_info), (int) ELF_R_TYPE (rela->r_info),
                  rela->r_offset, (int) rela->r_addend);
        if (!rtems_rtl_elf_relocate_rela (obj, rela, targetsect, symvalue))
          return false;
      }
      else
      {
        if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
        printf ("rel: sym:%-2d type:%-2d off:%08lx\n",
                (int) ELF_R_SYM (rel->r_info), (int) ELF_R_TYPE (rel->r_info),
                rel->r_offset);
        if (!rtems_rtl_elf_relocate_rel (obj, rel, targetsect, symvalue))
          return false;
      }
    }
  }

  /*
   * Set the unresolved externals status if there are unresolved externals.
   */
  if (unresolved)
    obj->flags |= RTEMS_RTL_OBJ_UNRESOLVED;

  return true;
}

static bool
rtems_rtl_elf_symbols (rtems_rtl_obj_t*      obj,
                       int                   fd,
                       rtems_rtl_obj_sect_t* sect,
                       void*                 data)
{
  rtems_rtl_obj_cache_t* symbols;
  rtems_rtl_obj_cache_t* strings;
  rtems_rtl_obj_sect_t*  strtab;
  int                    globals;
  int                    string_space;
  char*                  string;
  int                    sym;

  strtab = rtems_rtl_obj_find_section (obj, ".strtab");
  if (!strtab)
  {
    rtems_rtl_set_error (EINVAL, "no .strtab section");
    return false;
  }

  rtems_rtl_obj_caches (&symbols, &strings, NULL);

  if (!symbols || !strings)
    return false;

  /*
   * Find the number of globals and the amount of string space
   * needed. Also check for duplicate symbols.
   */

  globals      = 0;
  string_space = 0;

  for (sym = 0; sym < (sect->size / sizeof (Elf_Sym)); ++sym)
  {
    Elf_Sym     symbol;
    off_t       off;
    const char* name;
    size_t      len;

    off = obj->ooffset + sect->offset + (sym * sizeof (symbol));

    if (!rtems_rtl_obj_cache_read_byval (symbols, fd, off,
                                         &symbol, sizeof (symbol)))
      return false;
    
    off = obj->ooffset + strtab->offset + symbol.st_name;
    len = RTEMS_RTL_ELF_STRING_MAX;
    
    if (!rtems_rtl_obj_cache_read (strings, fd, off, (void**) &name, &len))
      return false;

    /*
     * Only keep the functions and global or weak symbols.
     */
    if ((ELF_ST_TYPE (symbol.st_info) == STT_OBJECT) ||
        (ELF_ST_TYPE (symbol.st_info) == STT_FUNC))
    {
      if ((ELF_ST_BIND (symbol.st_info) == STB_GLOBAL) ||
          (ELF_ST_BIND (symbol.st_info) == STB_WEAK))
      {
        /*
         * If there is a globally exported symbol already present and this
         * symbol is not weak raise an error. If the symbol is weak and present
         * globally ignore this symbol and use the global one and if it is not
         * present take this symbol global or weak. We accept the first weak
         * symbol we find and make it globally exported.
         */
        if (rtems_rtl_symbol_global_find (name) &&
            (ELF_ST_BIND (symbol.st_info) != STB_WEAK))
        {
          rtems_rtl_set_error (ENOMEM, "duplicate global symbol: %s", name);
          return false;
        }
        else
        {
          ++globals;
          string_space += strlen (name) + 1;
        }
      }
    }
  }

  if (globals)
  {
    rtems_rtl_obj_sym_t* gsym;
    
    obj->global_size = globals * sizeof (rtems_rtl_obj_sym_t) + string_space;
    obj->global_table = calloc (1, obj->global_size);
    if (!obj->global_table)
    {
      obj->global_size = 0;
      rtems_rtl_set_error (ENOMEM, "no memory for obj global syms");
      return false;
    }

    obj->global_syms = globals;

    for (sym = 0,
           gsym = obj->global_table,
           string = (((char*) obj->global_table) +
                     (globals * sizeof (rtems_rtl_obj_sym_t)));
         sym < (sect->size / sizeof (Elf_Sym));
         ++sym)
    {
      Elf_Sym     symbol;
      off_t       off;
      const char* name;
      size_t      len;

      off = obj->ooffset + sect->offset + (sym * sizeof (symbol));

      if (!rtems_rtl_obj_cache_read_byval (symbols, fd, off,
                                           &symbol, sizeof (symbol)))
      {
        free (obj->global_table);
        obj->global_table = NULL;
        obj->global_syms = 0;
        obj->global_size = 0;
        return false;
      }

      off = obj->ooffset + strtab->offset + symbol.st_name;
      len = RTEMS_RTL_ELF_STRING_MAX;
    
      if (!rtems_rtl_obj_cache_read (strings, fd, off, (void**) &name, &len))
        return false;

      if (((ELF_ST_TYPE (symbol.st_info) == STT_OBJECT) ||
           (ELF_ST_TYPE (symbol.st_info) == STT_FUNC)) &&
          ((ELF_ST_BIND (symbol.st_info) == STB_GLOBAL) ||
           (ELF_ST_BIND (symbol.st_info) == STB_WEAK)))
      {
        rtems_rtl_obj_sect_t* symsect;
        symsect = rtems_rtl_obj_find_section_by_index (obj, symbol.st_shndx);
        if (!symsect)
        {
          free (obj->global_table);
          obj->global_table = NULL;
          obj->global_syms = 0;
          obj->global_size = 0;
          rtems_rtl_set_error (EINVAL, "sym section not found");
          return false;
        }
        memcpy (string, name, strlen (name) + 1);
        gsym->name = string;
        string += strlen (name) + 1;
        gsym->value = symbol.st_value + (uint8_t*) symsect->base;

        if (rtems_rtl_trace (RTEMS_RTL_TRACE_SYMBOL))
          printf ("sym:%-2d name:%-2d:%-20s bind:%-2d type:%-2d val:%8p sect:%d size:%d\n",
                  sym, (int) symbol.st_name, gsym->name,
                  (int) ELF_ST_BIND (symbol.st_info),
                  (int) ELF_ST_TYPE (symbol.st_info),
                  gsym->value, symbol.st_shndx,
                  (int) symbol.st_size);
      
        ++gsym;
      }
    }
  }
  
  return true;
}

static bool
rtems_rtl_elf_parse_sections (rtems_rtl_obj_t* obj, int fd, Elf_Ehdr* ehdr)
{
  rtems_rtl_obj_cache_t* sects;
  rtems_rtl_obj_cache_t* strings;
  int                    section;
  off_t                  sectstroff;
  off_t                  off;
  Elf_Shdr               shdr;
  
  rtems_rtl_obj_caches (&sects, &strings, NULL);

  if (!sects || !strings)
    return false;
  
  /*
   * Get the offset to the section string table.
   */
  off = obj->ooffset + ehdr->e_shoff + (ehdr->e_shstrndx * ehdr->e_shentsize);
  
  if (!rtems_rtl_obj_cache_read_byval (sects, fd, off, &shdr, sizeof (shdr)))
    return false;

  if (shdr.sh_type != SHT_STRTAB)
  {
    rtems_rtl_set_error (EINVAL, "bad .sectstr section type");
    return false;
  }

  sectstroff = obj->ooffset + shdr.sh_offset;

  for (section = 0; section < ehdr->e_shnum; ++section)
  {
    uint32_t flags;

    off = obj->ooffset + ehdr->e_shoff + (section * ehdr->e_shentsize);

    if (!rtems_rtl_obj_cache_read_byval (sects, fd, off, &shdr, sizeof (shdr)))
      return false;

    flags = 0;
    
    switch (shdr.sh_type)
    {
      case SHT_NULL:
        /*
         * Ignore.
         */
        break;

      case SHT_PROGBITS:
        /*
         * There are 2 program bits sections. One is the program text and the
         * other is the program data. The program text is flagged
         * alloc/executable and the program data is flagged alloc/writable.
         */
        if ((shdr.sh_flags & SHF_ALLOC) == SHF_ALLOC)
        {
          if ((shdr.sh_flags & SHF_EXECINSTR) == SHF_EXECINSTR)
            flags = RTEMS_RTL_OBJ_SECT_TEXT | RTEMS_RTL_OBJ_SECT_LOAD;
          else if ((shdr.sh_flags & SHF_WRITE) == SHF_WRITE)
            flags = RTEMS_RTL_OBJ_SECT_DATA | RTEMS_RTL_OBJ_SECT_LOAD;
          else
            flags = RTEMS_RTL_OBJ_SECT_CONST | RTEMS_RTL_OBJ_SECT_LOAD;
        }
        break;

      case SHT_NOBITS:
        /*
         * There is 1 NOBIT section which is the .bss section. There is nothing
         * but a definition as the .bss is just a clear region of memory.
         */
        if ((shdr.sh_flags & (SHF_ALLOC | SHF_WRITE)) == (SHF_ALLOC | SHF_WRITE))
          flags = RTEMS_RTL_OBJ_SECT_BSS | RTEMS_RTL_OBJ_SECT_ZERO;
        break;

      case SHT_RELA:
        flags = RTEMS_RTL_OBJ_SECT_RELA;
        break;
        
      case SHT_REL:
        /*
         * The sh_link holds the section index for the symbol table. The sh_info
         * holds the section index the relocations apply to.
         */
        flags = RTEMS_RTL_OBJ_SECT_REL;
        break;

      case SHT_SYMTAB:
        flags = RTEMS_RTL_OBJ_SECT_SYM;
        break;

      case SHT_STRTAB:
        flags = RTEMS_RTL_OBJ_SECT_STR;
        break;

      default:
        printf ("unsupported section: %2d: type=%02d flags=%02x\n",
                section, (int) shdr.sh_type, (int) shdr.sh_flags);
        break;
    }
    
    if (flags != 0)
    {
      char*  name;
      size_t len;

      len = RTEMS_RTL_ELF_STRING_MAX;
      if (!rtems_rtl_obj_cache_read (strings, fd,
                                     sectstroff + shdr.sh_name,
                                     (void**) &name, &len))
        return false;

      if (strcmp (".ctors", name) == 0)
        flags |= RTEMS_RTL_OBJ_SECT_CTOR;
      if (strcmp (".dtors", name) == 0)
        flags |= RTEMS_RTL_OBJ_SECT_DTOR;
      
      if (!rtems_rtl_obj_add_section (obj, section, name,
                                      shdr.sh_size, shdr.sh_offset,
                                      shdr.sh_addralign, shdr.sh_link,
                                      shdr.sh_info, flags))
        return false;
    }
  }

  return true;
}

bool
rtems_rtl_obj_file_load (rtems_rtl_obj_t* obj, int fd)
{
  rtems_rtl_obj_cache_t* header;
  Elf_Ehdr               ehdr;
  
  rtems_rtl_obj_caches (&header, NULL, NULL);
  
  if (!rtems_rtl_obj_cache_read_byval (header, fd, obj->ooffset,
                                       &ehdr, sizeof (ehdr)))
    return false;

  /*
   * Check we have a valid ELF file.
   */
  if ((memcmp (ELFMAG, ehdr.e_ident, SELFMAG) != 0) 
      || ehdr.e_ident[EI_CLASS] != ELFCLASS)
  {
    rtems_rtl_set_error (EINVAL, "invalid ELF file format");
    return false;
  }

  if ((ehdr.e_ident[EI_VERSION] != EV_CURRENT)
      || (ehdr.e_version != EV_CURRENT)
      || (ehdr.e_ident[EI_DATA] != ELFDEFNNAME (MACHDEP_ENDIANNESS)))
  {
    rtems_rtl_set_error (EINVAL, "unsupported ELF file version");
    return false;
  }
      
  if (!rtems_rtl_elf_machine_check (&ehdr))
  {
    rtems_rtl_set_error (EINVAL, "unsupported machine type");
    return false;
  }

  if (ehdr.e_type == ET_DYN)
  {
    rtems_rtl_set_error (EINVAL, "unsupported ELF file type");
    return false;
  }

  if (ehdr.e_phentsize != 0)
  {
    rtems_rtl_set_error (EINVAL, "ELF file contains program headers");
    return false;
  }
  
  if (ehdr.e_shentsize != sizeof (Elf_Shdr))
  {
    rtems_rtl_set_error (EINVAL, "invalid ELF section header size");
    return false;
  }

  /*
   * Parse the section information first so we have the memory map of the object
   * file and the memory allocated. Any further allocations we make to complete
   * the load will not fragment the memory.
   */ 
  if (!rtems_rtl_elf_parse_sections (obj, fd, &ehdr))
    return false;
  
  obj->entry = (void*)(uintptr_t) ehdr.e_entry;

  if (!rtems_rtl_obj_load_sections (obj, fd))
    return false;

  if (!rtems_rtl_obj_load_symbols (obj, fd, rtems_rtl_elf_symbols, &ehdr))
    return false;
  
  if (!rtems_rtl_obj_relocate (obj, fd, rtems_rtl_elf_relocator, &ehdr))
    return false;
  
  return true;
} 

