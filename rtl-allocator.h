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

#if !defined (_RTEMS_RTL_ALLOCATOR_H_)
#define _RTEMS_RTL_ALLOCATOR_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Define the types of allocation the loader requires.
 */
enum rtems_rtl_alloc_tags_e {
  RTEMS_RTL_ALLOC_SYMBOL,  /**< A symbol in the symbol table. */
  RTEMS_RTL_ALLOC_STRING,  /**< A runtime loader string. */
  RTEMS_RTL_ALLOC_OBJECT,  /**< An RTL object. */
  RTEMS_RTL_ALLOC_MODULE   /**< The module's code, data and bss memory. */
};

typedef enum rtems_rtl_alloc_tags_e rtems_rtl_alloc_tag_t;

/**
 * Allocator handler handles all RTL allocations. It can be hooked and
 * overridded for customised allocation schemes or memory maps.
 *
 * @param allocation If true the request is to allocate memory else free.
 * @param tag The type of allocation request.
 * @param address Pointer to the memory address. If an allocation the value is
 *                unspecific on entry and the allocated address or NULL on
 *                exit. The NULL value means the allocation failed. If a delete
 *                or free request the memory address is the block to free. A
 *                free request of NULL is silently ignored.
 * @param size The size of the allocation if an allocation request and
 *             not used if deleting or freeing a previous allocation.
 */
typedef void (*rtems_rtl_allocator_t)(bool                  allocate,
                                      rtems_rtl_alloc_tag_t tag,
                                      void**                address,
                                      size_t                size);

/**
 * The Runtime Loader allocator new allocates new memory.
 *
 * @param tag The type of allocation request.
 * @param size The size of the allocation.
 * @return void* The memory address or NULL is not memory available.
 */
void* rtems_rtl_alloc_new(rtems_rtl_alloc_tag_t tag, size_t size);

/**
 * The Runtime Loader allocator delete deletes allocated memory.
 *
 * @param tag The type of allocation request.
 * @param address The memory address to delete. A NULL is ignored.
 */
void rtems_rtl_alloc_del(rtems_rtl_alloc_tag_t tag, void* address);

/**
 * Hook the Runtime Loader allocatior. A handler can call the previous handler
 * in the chain to use it for specific tags. The default handler uses the
 * system heap. Do not unhook your handler if memory it allocates has not been
 * returned.
 *
 * @param handler The handler to use as the allocator.
 * @return rtems_rtl_alloc_handler_t The previous handler.
 */
rtems_rtl_allocator_t rtems_rtl_alloc_hook(rtems_rtl_allocator_t handler);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
