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
#define	RELOC_ALIGNED_P(x) \
	(((uintptr_t)(x) & (sizeof(void *) - 1)) == 0)

static inline Elf_Addr
load_ptr(void *where)
{
	Elf_Addr res;

	memcpy(&res, where, sizeof(res));

	return (res);
}

static inline void
store_ptr(void *where, Elf_Addr val)
{

	memcpy(where, &val, sizeof(val));
}

bool
rtems_rtl_elf_rel_resolve_sym (Elf_Word type)
{
  return true;
}

bool
rtems_rtl_elf_relocate_rela (const rtems_rtl_obj_t*      obj,
                             const Elf_Rela*             rela,
                             const rtems_rtl_obj_sect_t* sect,
                             const char*                 symname,
                             const Elf_Byte              syminfo,
                             const Elf_Word              symvalue)
{
  rtems_rtl_set_error (EINVAL, "rela type record not supported");
  return false;
}

bool
rtems_rtl_elf_relocate_rel (const rtems_rtl_obj_t*      obj,
                            const Elf_Rel*              rel,
                            const rtems_rtl_obj_sect_t* sect,
                            const char*                 symname,
                            const Elf_Byte              syminfo,
                            const Elf_Word              symvalue)
{
  Elf_Addr *where;
  Elf_Addr  tmp;

  where = (Elf_Addr *)(sect->base + rel->r_offset);

  switch (ELF_R_TYPE(rel->r_info)) {
		case R_TYPE(NONE):
			break;

#if 1 /* XXX should not occur */
		case R_TYPE(PC24): {	/* word32 S - P + A */
			Elf32_Sword addend;

			/*
			 * Extract addend and sign-extend if needed.
			 */
			addend = *where;
			if (addend & 0x00800000)
				addend |= 0xff000000;

			tmp = (Elf_Addr)sect->base + symvalue
        - (Elf_Addr)where + (addend << 2);

      if ((tmp & 0xfe000000) != 0xfe000000 &&
			    (tmp & 0xfe000000) != 0) {
        rtems_rtl_set_error (EINVAL,
                             "R_ARM_PC24 in %s relocation @ %p failed " \
                             "(displacement %ld (%#lx) out of range)",
                             rtems_rtl_obj_oname (obj), where, (long) tmp, (long) tmp);
				return false;
			}

			tmp >>= 2;
			*where = (*where & 0xff000000) | (tmp & 0x00ffffff);
      if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
        printf ("rtl: PC24 %p @ %p in %s",
                (void *)*where, where, rtems_rtl_obj_oname (obj));
			break;
		}
#endif

		case R_TYPE(ABS32):	/* word32 B + S + A */
		case R_TYPE(GLOB_DAT):	/* word32 B + S */
			if (__predict_true(RELOC_ALIGNED_P(where))) {
				tmp = *where + (Elf_Addr)sect->base + symvalue;
				/* Set the Thumb bit, if needed.  */
				if (ELF_ST_TYPE(syminfo) == STT_ARM_TFUNC)
          tmp |= 1;
				*where = tmp;
			} else {
				tmp = load_ptr(where) + symvalue;
				/* Set the Thumb bit, if needed.  */
				if (ELF_ST_TYPE(syminfo) == STT_ARM_TFUNC)
				    tmp |= 1;
				store_ptr(where, tmp);
			}
      if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
        printf ("rtl: ABS32/GLOB_DAT %p @ %p in %s",
                (void *)tmp, where, rtems_rtl_obj_oname (obj));
			break;

		case R_TYPE(RELATIVE):	/* word32 B + A */
			if (__predict_true(RELOC_ALIGNED_P(where))) {
				tmp = *where + (Elf_Addr)sect->base;
				*where = tmp;
			} else {
				tmp = load_ptr(where) + (Elf_Addr)sect->base;
				store_ptr(where, tmp);
			}
      if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
        printf ("rtl: RELATIVE in %s --> %p",
                rtems_rtl_obj_oname (obj), (void *)tmp);
			break;

		case R_TYPE(COPY):
			/*
			 * These are deferred until all other relocations have
			 * been done.  All we do here is make sure that the
			 * COPY relocation is not in a shared library.  They
			 * are allowed only in executable files.
			 */
      if (rtems_rtl_trace (RTEMS_RTL_TRACE_RELOC))
        printf ("rtl: COPY (avoid in main)");
			break;

		default:
      printf ("rtl: reloc unknown: sym = %lu, type = %lu, offset = %p, "
              "contents = %p\n",
              ELF_R_SYM(rel->r_info), (uint32_t) ELF_R_TYPE(rel->r_info),
              (void *)rel->r_offset, (void *)*where);
      rtems_rtl_set_error (EINVAL,
                           "%s: Unsupported relocation type %ld "
                           "in non-PLT relocations",
                           sect->name, (uint32_t) ELF_R_TYPE(rel->r_info));
			return false;
  }

	return true;
}

