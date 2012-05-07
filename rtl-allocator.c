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
 * @brief RTEMS Run-Time Linker Allocator
 */

#include <stdio.h>

#include <rtl.h>
#include <rtl-trace.h>

/**
 * Tags as symbols for tracing.
 */
#if RTEMS_RTL_TRACE
static const char* tag_labels[4] =
{
  "SYMBOL",
  "STRING",
  "OBJECT",
  "MODULE"
};
#define rtems_rtl_trace_tag_label(_l) tag_labels[_l]
#else
#define rtems_rtl_trace_tag_label(_l) ""
#endif

void*
rtems_rtl_alloc_new(rtems_rtl_alloc_tag_t tag, size_t size)
{
  rtems_rtl_data_t* rtl = rtems_rtl_lock ();
  void*             address = NULL;

  if (rtl)
    rtl->allocator (true, tag, &address, size);
  
  if (rtems_rtl_trace (RTEMS_RTL_TRACE_ALLOCATOR))
    printf ("alloc: new: %s addr=%p size=%zu\n",
            rtems_rtl_trace_tag_label (tag), address, size);

  rtems_rtl_unlock ();

  return address;
}

void
rtems_rtl_alloc_del(rtems_rtl_alloc_tag_t tag, void* address)
{
  rtems_rtl_data_t* rtl = rtems_rtl_lock ();

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_ALLOCATOR))
    printf ("alloc: del: %s addr=%p\n",
            rtems_rtl_trace_tag_label (tag), address);
  
  if (rtl)
    rtl->allocator (false, tag, &address, 0);
  
  rtems_rtl_unlock ();
}

rtems_rtl_allocator_t
rtems_rtl_alloc_hook(rtems_rtl_allocator_t handler)
{
  rtems_rtl_data_t* rtl = rtems_rtl_lock ();
  rtems_rtl_allocator_t previous = rtl->allocator;
  rtl->allocator = handler;
  rtems_rtl_unlock ();
  return previous;
}

