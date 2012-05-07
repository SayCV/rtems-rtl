/*
 * Taken from NetBSD and stripped of the relocations not needed on RTEMS.
 */

/*  $NetBSD: mdreloc.c,v 1.33 2010/01/14 12:12:07 skrll Exp $  */

#include <sys/cdefs.h>

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <rtl.h>
#include "rtl-elf.h"
#include "rtl-error.h"
#include <rtl-trace.h>

/*
 * It is possible for the compiler to emit relocations for unaligned data.
 * We handle this situation with these inlines.
 */
#define  RELOC_ALIGNED_P(x) \
  (((uintptr_t)(x) & (sizeof(void *) - 1)) == 0)

static inline Elf_Addr
load_ptr (void *where)
{
  Elf_Addr res;
  memcpy (&res, where, sizeof(res));
  return (res);
}

static inline void
store_ptr (void *where, Elf_Addr val)
{
  memcpy(where, &val, sizeof(val));
}

bool
rtems_rtl_elf_relocate_rela (rtems_rtl_obj_t*      obj,
                             const Elf_Rela*       rela,
                             rtems_rtl_obj_sect_t* sect,
                             const Elf_Sym*        sym,
                             const char*           symname)
{
  printf ("rtl: rela record not supported; please report\n");
  return false;
}

bool
rtems_rtl_elf_relocate_rel (rtems_rtl_obj_t*      obj,
                            const Elf_Rel*        rel,
                            rtems_rtl_obj_sect_t* sect,
                            const Elf_Sym*        sym,
                            const char*           symname)
{
  Elf_Addr  target = 0;
  Elf_Addr* where;
  Elf_Addr  tmp;
  Elf_Word  symvalue;

  where = (Elf_Addr *)(sect->base + rel->r_offset);

  switch (ELF_R_TYPE(rel->r_info)) {
    case R_TYPE(NONE):
      break;

    case R_TYPE(PC24): {  /* word32 S - P + A */
      Elf32_Sword addend;

      /*
       * Extract addend and sign-extend if needed.
       */
      addend = *where;
      if (addend & 0x00800000)
        addend |= 0xff000000;

      if (!rtems_rtl_elf_find_symbol (obj, sym, symname, &symvalue))
        return false;

      tmp = (Elf_Addr) symvalue - (Elf_Addr) where + (addend << 2);
      if ((tmp & 0xfe000000) != 0xfe000000 &&
          (tmp & 0xfe000000) != 0) {
        rtems_rtl_set_error ("%s: R_ARM_PC24 relocation @ %p to %s failed "
                             "(displacement %ld (%#lx) out of range)",
                             obj->oname, where, symname,
                             (long) tmp, (long) tmp);
        return false;
      }
      tmp >>= 2;
      *where = (*where & 0xff000000) | (tmp & 0x00ffffff);
      if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
        printf ("rtl: reloc PC24 %s in %s --> %p @ %p\n",
                symname, obj->oname, (void *)*where, where);
      break;
    }

    case R_TYPE(ABS32):     /* word32 B + S + A */
    case R_TYPE(GLOB_DAT):  /* word32 B + S */
      if (!rtems_rtl_elf_find_symbol (obj, sym, symname, &symvalue))
        return false;

      if (__predict_true (RELOC_ALIGNED_P (where))) {
        tmp = *where + symvalue;
        /* Set the Thumb bit, if needed.  */
        if (ELF_ST_TYPE(def->st_info) == STT_ARM_TFUNC)
          tmp |= 1;
        *where = tmp;
      } else {
        tmp = load_ptr (where) + symvalue;
        /* Set the Thumb bit, if needed.  */
        if (ELF_ST_TYPE(def->st_info) == STT_ARM_TFUNC)
          tmp |= 1;
        store_ptr (where, tmp);
      }
      if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
        printf ("rtl: reloc ABS32/GLOB_DAT %s in %s --> %p @ %p\n",
                symname, obj->oname, (void *)tmp, where);
      break;

    case R_TYPE(RELATIVE):  /* word32 B + A */
      if (__predict_true (RELOC_ALIGNED_P (where))) {
        tmp = *where + (Elf_Addr) sect->base;
        *where = tmp;
      } else {
        tmp = load_ptr (where) + (Elf_Addr) sect->base;
        store_ptr (where, tmp);
      }
      if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
        printf ("rtl: reloc RELATIVE in %s --> %p", obj->oname, (void *)tmp));
      break;

    case R_TYPE(COPY):
      printf ("rtl: reloc COPY (please report)\n");
      break;

    default:
      printf ("rtl: reloc unknown: sym = %lu, type = %lu, offset = %p, "
              "contents = %p, symbol = %s\n",
              ELF_R_SYM(rel->r_info), (uint32_t) ELF_R_TYPE(rel->r_info),
              (void *)rel->r_offset, (void *)*where, symname);
      rtems_rtl_set_error (EINVAL,
                           "%s: Unsupported relocation type %ld "
                           "in non-PLT relocations",
                           sect->name, (uint32_t) ELF_R_TYPE(rel->r_info));
      return false;
  }
  
  return true;
}
