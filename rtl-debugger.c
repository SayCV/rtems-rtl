/*
 *  COPYRIGHT (c) 2010 Chris Johns <chrisj@rtems.org>
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.com/license/LICENSE.
 *
 *  $Id$
 */
/**
 * @file
 *
 * @ingroup rtl
 *
 * @brief RTEMS Module Loading Debugger Interface.
 *
 * Inspection of run-time linkers in NetBSD and Android show a common type of
 * structure that is used to interface to GDB. The NetBSD definition of this
 * interface is being used and is defined in <link.h>. It defines a protocol
 * that is used by GDB to inspect the state of dynamic libraries. I have not
 * checked GDB code at when writing this comment but I suspect GDB sets a break
 * point on the r_brk field of _rtld_debug and it has code that detects this
 * break point being hit. When this happens it reads the state and performs the
 * operation based on the r_state field.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <link.h>

struct r_debug  _rtld_debug;

void
_rtld_debug_state (void)
{
  /*
   * Empty. GDB only needs to hit this location.
   */
}
