/*
 *  mmap() - POSIX 1003.1b 6.3.1 - map pages of memory
 *
 *  COPYRIGHT (c) 2010.
 *  Chris Johns (chrisj@rtems.org)
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.com/license/LICENSE.
 *
 *  $Id$
 */

#ifndef MMAP_INTERNAL_H

#include <stdint.h>

#include <rtems.h>
#include <rtems/chain.h>

/**
 * Every mmap'ed region has a mapping.
 */
typedef struct mmap_mappings_s {
  rtems_chain_node node;  /**< The mapping chain's node */
  void*            addr;  /**< The address of the mapped memory */
  size_t           len;   /**< The length of memory mapped */
  int              flags; /**< The mapping flags */
} mmap_mapping;

extern rtems_chain_control mmap_mappings;

bool mmap_mappings_lock_obtain( void );
bool mmap_mappings_lock_release( void );


#endif
