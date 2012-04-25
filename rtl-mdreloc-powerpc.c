/*
 * Taken from NetBSD and stripped of the relocations not needed on RTEMS.
 *
 *  $Id$
 */

/*  $NetBSD: ppc_reloc.c,v 1.44 2010/01/13 20:17:22 christos Exp $  */

/*-
 * Copyright (C) 1998  Tsubai Masanari
 * Portions copyright 2002 Charles M. Hannum <root@ihack.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <rtl.h>
#include "rtl-elf.h"
#include "rtl-error.h"
#include <rtl-trace.h>

#define ha(x) ((((u_int32_t)(x) & 0x8000) ? \
                 ((u_int32_t)(x) + 0x10000) : (u_int32_t)(x)) >> 16)
#define l(x) ((u_int32_t)(x) & 0xffff)

/*
 * The PPC PLT format consists of three sections:
 * (1) The "pltcall" and "pltresolve" glue code.  This is always 18 words.
 * (2) The code part of the PLT entries.  There are 2 words per entry for
 *     up to 8192 entries, then 4 words per entry for any additional entries.
 * (3) The data part of the PLT entries, comprising a jump table.
 *     This section is half the size of the second section (ie. 1 or 2 words
 *     per entry).
 */

/*
 * Setup the plt glue routines.
 */
#define PLTCALL_SIZE    20
#define PLTRESOLVE_SIZE 24

bool
rtems_rtl_elf_relocate_rela (rtems_rtl_obj_t*      obj,
                             const Elf_Rela*       rela,
                             rtems_rtl_obj_sect_t* sect,
                             const Elf_Sym*        sym,
                             const char*           symname)
{
  Elf_Addr  target = 0;
  Elf_Addr* where;
  Elf_Word  symvalue;

  where = (Elf_Addr *)(sect->base + rela->r_offset);

  switch (ELF_R_TYPE(rela->r_info)) {
    case R_TYPE(JMP_SLOT):
    case R_TYPE(NONE):
      break;

    case R_TYPE(PC32):
      if (!rtems_rtl_elf_find_symbol (obj, sym, symname, &symvalue))
        return false;

      target = (Elf_Addr) symvalue + rela->r_addend;
      *where += target - (Elf_Addr)where;

      if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
        printf ("rtl: reloc PC32 %s in %s --> %p (%p) in %s\n",
                symname, sect->name, (void*) (symvalue + rela->r_addend),
                (void *)*where, obj->oname);
      break;

    case R_TYPE(32):  /* word32 S + A */
    case R_TYPE(GLOB_DAT):  /* word32 S + A */
      if (!rtems_rtl_elf_find_symbol (obj, sym, symname, &symvalue))
        return false;

      target = (Elf_Addr) symvalue + rela->r_addend;

      if (*where != target)
        *where = target;

      if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
        printf ("rtl: reloc 32/GLOB_DAT %s in %s --> %p in %s\n",
                symname, sect->name, (void *)*where, obj->oname);
      break;

    case R_TYPE(RELATIVE):  /* word32 B + A */
      *where += (Elf_Addr) sect->base + rela->r_addend;
      if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
        printf ("rtl: reloc RELATIVE in %s --> %p\n", obj->oname, (void *)*where);
      break;

    case R_TYPE(COPY):
      /*
       * These are deferred until all other relocations have
       * been done.  All we do here is make sure that the
       * COPY relocation is not in a shared library.  They
       * are allowed only in executable files.
       */
      printf ("rtl: reloc COPY (please report)\n");
      break;

    default:
      printf ("rtl: reloc unknown: sym = %lu, type = %lu, offset = %p, "
              "contents = %p, symbol = %s\n",
              ELF_R_SYM(rela->r_info), (uint32_t) ELF_R_TYPE(rela->r_info),
              (void *)rela->r_offset, (void *)*where, symname);
      rtems_rtl_set_error (EINVAL,
                           "%s: Unsupported relocation type %ld "
                           "in non-PLT relocations",
                           sect->name, (uint32_t) ELF_R_TYPE(rela->r_info));
      return false;
  }

  return false;
}

bool
rtems_rtl_elf_relocate_rel (rtems_rtl_obj_t*      obj,
                            const Elf_Rel*        rel,
                            rtems_rtl_obj_sect_t* sect,
                            const Elf_Sym*        sym,
                            const char*           symname)
{
  printf ("rtl: rel type record not supported; please report\n");
  return false;
}
