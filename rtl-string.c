/*
 *  COPYRIGHT (c) 2012 Chris Johns <chrisj@rtems.org>
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */
/**
 * @file
 *
 * @ingroup rtems_rtl
 *
 * @brief RTEMS Run-Time Linker String managment.
 */

#include <string.h>

#include "rtl-allocator.h"
#include "rtl-string.h"

char*
rtems_rtl_strdup (const char *s1)
{
  size_t len = strlen (s1);
  char*  s2 = rtems_rtl_alloc_new (RTEMS_RTL_ALLOC_STRING, len + 1);
  if (s2)
  {
    memcpy (s2, s1, len);
    s2[len] = '\0';
  }
  return s2;
}

void
rtems_rtl_str_copy (rtems_rtl_ptr_t* dst, const char* str)
{
  size_t len = strlen (str);
  rtems_rtl_alloc_indirect_new (RTEMS_RTL_ALLOC_STRING, dst, len + 1);
  if (!rtems_rtl_ptr_null (dst))
  {
    char* p = rtems_rtl_ptr_get (dst);
    memcpy (p, str, len);
    p[len] = '\0';
  }
}
