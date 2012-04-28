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
 * @brief RTEMS Run-Time Linker Error
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <rtems/libio_.h>

#include <rtl.h>
#include <rtl-chain-iterator.h>
#include <rtl-obj.h>
#include "rtl-error.h"
#include "rtl-trace.h"

rtems_rtl_obj_t*
rtems_rtl_obj_alloc (void)
{
  rtems_rtl_obj_t* obj = malloc (sizeof (rtems_rtl_obj_t));
  if (obj)
  {
    /*
     * Initalise to 0.
     */
    *obj = (rtems_rtl_obj_t) { { 0 } };

    /*
     * Initialise the chains.
     */
    rtems_chain_initialize_empty (&obj->sections);
  }
  return obj;
}

static void
rtems_rtl_obj_free_names (rtems_rtl_obj_t* obj)
{
  free ((void*) obj->oname);
  free ((void*) obj->aname);
  if ((obj->fname != obj->aname) && (obj->fname != obj->oname))
    free ((void*) obj->fname);
}

bool
rtems_rtl_obj_free (rtems_rtl_obj_t* obj)
{
  if (obj->users || ((obj->flags & RTEMS_RTL_OBJ_LOCKED) != 0))
  {
    rtems_rtl_set_error (EINVAL, "cannot free obj still in use");
    return false;
  }
  if (!rtems_chain_is_node_off_chain (&obj->link))
    rtems_chain_extract (&obj->link);
  rtems_rtl_obj_symbol_erase (obj);
  if (obj->text_base)
    free (obj->text_base);
  rtems_rtl_obj_free_names (obj);
  free (obj);
  return true;
}

bool
rtems_rtl_obj_unresolved (rtems_rtl_obj_t* obj)
{
  return (obj->flags & RTEMS_RTL_OBJ_UNRESOLVED) != 0 ? true : false; 
}

static bool
rtems_rtl_obj_parse_name (rtems_rtl_obj_t* obj, const char* name)
{
  const char* p;
  const char* e;
  char*       aname;
  char*       oname;
  
  /*
   * Parse the name to determine if the object file is part of an archive or it
   * is an object file.
   */
  e = name + strlen (name);

  p = strchr (name, ':');

  if (p == NULL)
    p = e;

  aname = NULL;
  
  oname = malloc (p - name + 1);
  if (oname == NULL)
  {
    rtems_rtl_set_error (ENOMEM, "no memory for object file name");
    return false;
  }

  memcpy (oname, name, p - name);
  oname[p - name] = '\0';

  if (p != e)
  {
    const char* o;
    
    /*
     * The file name is an archive and the object file name is next after the
     * delimiter.
     */
    aname = oname;
    ++p;

    /*
     * See if there is a '@' to delimit an archive offset for the object in the
     * archive.
     */
    o = strchr (p, '@');

    if (o == NULL)
      o = e;
    
    oname = malloc (o - p + 1);
    if (oname == NULL)
    {
      free (aname);
      rtems_rtl_set_error (ENOMEM, "no memory for object file name");
      return false;
    }

    memcpy (oname, p, o - p);
    oname[o - p] = '\0';

    if (o != e)
    {
      /*
       * The object name has an archive offset. If the number
       * does not parse 0 will be returned and the archive will be
       * searched.
       */
      obj->ooffset = strtoul (o + 1, 0, 0);
    }
  }

  obj->oname = oname;
  obj->aname = aname;
  
  return true;
}

static bool
rtems_rtl_seek_read (int fd, off_t off, size_t len, uint8_t* buffer)
{
  if (lseek (fd, off, SEEK_SET) < 0)
    return false;
  if (read (fd, buffer, len) != len)
    return false;
  return true;
}

/**
 * Scan the decimal number returning the value found.
 */
static uint64_t
rtems_rtl_scan_decimal (const uint8_t* string, size_t len)
{
  uint64_t value = 0;

  while (len && (*string != ' '))
  {
    value *= 10;
    value += *string - '0';
    ++string;
    --len;
  }

  return value;
}

/**
 * Align the size to the next alignment point. Assume the alignment is a
 * positive integral power of 2 if not 0 or 1. If 0 or 1 then there is no
 * alignment.
 */
static size_t
rtems_rtl_sect_align (size_t offset, uint32_t alignment)
{
  if ((alignment > 1) && ((offset & ~alignment) != 0))
    offset = (offset + alignment) & ~(alignment - 1);
  return offset;
}

/**
 * Section size summer iterator data.
 */
typedef struct
{
  uint32_t mask; /**< The selection mask to sum. */
  size_t   size; /**< The size of all section fragments. */
} rtems_rtl_obj_sect_summer_t;

static bool
rtems_rtl_obj_sect_summer (rtems_chain_node* node, void* data)
{
  rtems_rtl_obj_sect_t*        sect = (rtems_rtl_obj_sect_t*) node;
  rtems_rtl_obj_sect_summer_t* summer = data;
  if ((sect->flags & summer->mask) == summer->mask)
    summer->size =
      rtems_rtl_sect_align (summer->size, sect->alignment) + sect->size;
  return true;
}

static size_t
rtems_rtl_obj_section_size (rtems_rtl_obj_t* obj, uint32_t mask)
{
  rtems_rtl_obj_sect_summer_t summer;
  summer.mask = mask;
  summer.size = 0;
  rtems_rtl_chain_iterate (&obj->sections,
                           rtems_rtl_obj_sect_summer,
                           &summer);
  return summer.size;
}

/**
 * Section alignment iterator data. The first section's alignment sets the
 * alignment for that type of section.
 */
typedef struct
{
  uint32_t mask;      /**< The selection mask to look for alignment. */
  uint32_t alignment; /**< The alignment of the section type. */
} rtems_rtl_obj_sect_aligner_t;

/**
 * The section aligner iterator.
 */
static bool
rtems_rtl_obj_sect_aligner (rtems_chain_node* node, void* data)
{
  rtems_rtl_obj_sect_t*         sect = (rtems_rtl_obj_sect_t*) node;
  rtems_rtl_obj_sect_aligner_t* aligner = data;
  if ((sect->flags & aligner->mask) == aligner->mask)
  {
    aligner->alignment = sect->alignment;
    return false;
  }
  return true;
}

static size_t
rtems_rtl_obj_section_alignment (rtems_rtl_obj_t* obj, uint32_t mask)
{
  rtems_rtl_obj_sect_aligner_t aligner;
  aligner.mask = mask;
  aligner.alignment = 0;
  rtems_rtl_chain_iterate (&obj->sections,
                           rtems_rtl_obj_sect_aligner,
                           &aligner);
  return aligner.alignment;
}

static bool
rtems_rtl_obj_section_handler (uint32_t                     mask,
                               rtems_rtl_obj_t*             obj,
                               int                          fd,
                               rtems_rtl_obj_sect_handler_t handler,
                               void*                        data)
{
  rtems_chain_node* node = rtems_chain_first (&obj->sections);
  while (!rtems_chain_is_tail (&obj->sections, node))
  {
    rtems_rtl_obj_sect_t* sect = (rtems_rtl_obj_sect_t*) node;
    if ((sect->flags & mask) != 0)
    {
      if (!handler (obj, fd, sect, data))
        return false;
    }
    node = rtems_chain_next (node);
  }
  return true;
}

bool
rtems_rtl_match_name (rtems_rtl_obj_t* obj, const char* name)
{
  const char* n1 = obj->oname;
  while ((*n1 != '\0') && (*n1 != '\n') && (*n1 != '/') &&
         (*name != '\0') && (*name != '/') && (*n1 == *name))
  {
    ++n1;
    ++name;
  }
  if (((*n1 == '\0') || (*n1 == '\n') || (*n1 == '/')) &&
      ((*name == '\0') || (*name == '/')))
    return true;
  return false;
}

bool
rtems_rtl_obj_find_file (rtems_rtl_obj_t* obj, const char* name)
{
  struct stat sb;
  const char* n;

  /*
   * Parse the name. The object descriptor will have the archive name and/or
   * object name fields filled in. A find of the file will result in the file
   * name (fname) field pointing to the actual file if present on the file
   * system.
   */
  if (!rtems_rtl_obj_parse_name (obj, name))
    return false;

  /*
   * If the archive field (aname) is set we use that name else we use the
   * object field (oname). If selected name is absolute we just point the aname
   * field to the fname field to that name. If the field is relative we search
   * the paths set in the RTL for the file.
   */
  if (obj->aname != NULL)
    n = obj->aname;
  else
    n = obj->oname;
  
  if (rtems_filesystem_is_delimiter (n[0]))
  {
    if (stat (n, &sb) == 0)
      obj->fname = n;
  }
  else
  {
    rtems_rtl_data_t* rtl;
    const char*       s;
    const char*       e;
    int               l;

    rtl = rtems_rtl_lock ();
    
    s = rtl->paths;
    e = s + strlen (rtl->paths);
    l = strlen (n);

    while ((obj->fname == NULL) && (s != e))
    {
      const char* d;
      char*       p;

      d = strchr (s, ':');
      if (d == NULL)
        d = e;

      /*
       * Allocate the path fragment, separator, name, terminating nul.
       */
      p = malloc ((d - s) + 1 + l + 1);
      if (p == NULL)
      {
        rtems_rtl_set_error (ENOMEM, "no memory searching for object file");
        rtems_rtl_unlock ();
        return false;
      }

      memcpy (p, s, d - s);
      p[d - s] = '/';
      memcpy (p + (d - s) + 1, n, l);
      p[(d - s) + 1 + l] = '\0';

      if (stat (p, &sb) < 0)
        free (p);
      else
      {
        /*
         * We have found the file. Do not release the path memory.
         */
        obj->fname = p;
      }
      
      s = d;
    }

    rtems_rtl_unlock ();
  }

  if (obj->fname)
    obj->fsize = sb.st_size;
  else
    rtems_rtl_set_error (ENOMEM, "object file not found");

  return obj->fname ? true : false;
}

bool
rtems_rtl_obj_add_section (rtems_rtl_obj_t* obj,
                           int              section,
                           const char*      name,
                           size_t           size,
                           off_t            offset,
                           uint32_t         alignment,
                           int              link,
                           int              info,
                           uint32_t         flags)
{
  rtems_rtl_obj_sect_t* sect = malloc (sizeof (rtems_rtl_obj_sect_t));
  if (!sect)
  {
    rtems_rtl_set_error (ENOMEM, "adding allocated section");
    return false;
  }
  sect->section = section;
  sect->name = strdup (name);
  sect->size = size;
  sect->offset = offset;
  sect->alignment = alignment;
  sect->link = link;
  sect->info = info;
  sect->flags = flags;
  sect->base = NULL;
  rtems_chain_append (&obj->sections, &sect->node);

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_SECTION))
    printf ("sect: %-2d: %s\n", section, name);
  
  return true;
}

void
rtems_rtl_obj_erase_sections (rtems_rtl_obj_t* obj)
{
  rtems_chain_node* node = rtems_chain_first (&obj->sections);
  while (!rtems_chain_is_tail (&obj->sections, node))
  {
    rtems_rtl_obj_sect_t* sect = (rtems_rtl_obj_sect_t*) node;
    rtems_chain_node*     next_node = rtems_chain_next (node);
    rtems_chain_extract (node);
    free ((void*) sect->name);
    free (node);
    node = next_node;
  }
}

/**
 * Section finder iterator data.
 */
typedef struct
{
  rtems_rtl_obj_sect_t*  sect;  /**< The matching section. */
  const char*            name;  /**< The name to match. */
  int                    index; /**< The index to match. */
} rtems_rtl_obj_sect_finder_t;

static bool
rtems_rtl_obj_sect_match_name (rtems_chain_node* node, void* data)
{
  rtems_rtl_obj_sect_t*        sect = (rtems_rtl_obj_sect_t*) node;
  rtems_rtl_obj_sect_finder_t* match = data;
  if (strcmp (sect->name, match->name) == 0)
  {
    match->sect = sect;
    return false;
  }
  return true;
}

rtems_rtl_obj_sect_t*
rtems_rtl_obj_find_section (rtems_rtl_obj_t* obj, const char* name)
{
  rtems_rtl_obj_sect_finder_t match;
  match.sect = NULL;
  match.name = name;
  rtems_rtl_chain_iterate (&obj->sections,
                           rtems_rtl_obj_sect_match_name,
                           &match);
  return match.sect;
}

static bool
rtems_rtl_obj_sect_match_index (rtems_chain_node* node, void* data)
{
  rtems_rtl_obj_sect_t*        sect = (rtems_rtl_obj_sect_t*) node;
  rtems_rtl_obj_sect_finder_t* match = data;
  if (sect->section == match->index)
  {
    match->sect = sect;
    return false;
  }
  return true;
}

rtems_rtl_obj_sect_t*
rtems_rtl_obj_find_section_by_index (rtems_rtl_obj_t* obj, int index)
{
  rtems_rtl_obj_sect_finder_t match;
  match.sect = NULL;
  match.index = index;
  rtems_rtl_chain_iterate (&obj->sections,
                           rtems_rtl_obj_sect_match_index,
                           &match);
  return match.sect;
}

size_t
rtems_rtl_obj_text_size (rtems_rtl_obj_t* obj)
{
  return rtems_rtl_obj_section_size (obj, RTEMS_RTL_OBJ_SECT_TEXT);
}

uint32_t
rtems_rtl_obj_text_alignment (rtems_rtl_obj_t* obj)
{
  return rtems_rtl_obj_section_alignment (obj, RTEMS_RTL_OBJ_SECT_TEXT);
}

size_t
rtems_rtl_obj_const_size (rtems_rtl_obj_t* obj)
{
  return rtems_rtl_obj_section_size (obj, RTEMS_RTL_OBJ_SECT_CONST);
}

uint32_t
rtems_rtl_obj_const_alignment (rtems_rtl_obj_t* obj)
{
  return rtems_rtl_obj_section_alignment (obj, RTEMS_RTL_OBJ_SECT_CONST);
}

size_t
rtems_rtl_obj_data_size (rtems_rtl_obj_t* obj)
{
  return rtems_rtl_obj_section_size (obj, RTEMS_RTL_OBJ_SECT_DATA);
}

uint32_t
rtems_rtl_obj_data_alignment (rtems_rtl_obj_t* obj)
{
  return rtems_rtl_obj_section_alignment (obj, RTEMS_RTL_OBJ_SECT_DATA);
}

size_t
rtems_rtl_obj_bss_size (rtems_rtl_obj_t* obj)
{
  return rtems_rtl_obj_section_size (obj, RTEMS_RTL_OBJ_SECT_BSS);
}

uint32_t
rtems_rtl_obj_bss_alignment (rtems_rtl_obj_t* obj)
{
  return rtems_rtl_obj_section_alignment (obj, RTEMS_RTL_OBJ_SECT_BSS);
}

bool
rtems_rtl_obj_relocate (rtems_rtl_obj_t*             obj,
                        int                          fd,
                        rtems_rtl_obj_sect_handler_t handler,
                        void*                        data)
{
  uint32_t mask = RTEMS_RTL_OBJ_SECT_REL | RTEMS_RTL_OBJ_SECT_RELA;
  return rtems_rtl_obj_section_handler (mask, obj, fd, handler, data);
}

bool
rtems_rtl_obj_load_symbols (rtems_rtl_obj_t*             obj,
                            int                          fd,
                            rtems_rtl_obj_sect_handler_t handler,
                            void*                        data)
{
  uint32_t mask = RTEMS_RTL_OBJ_SECT_SYM;
  return rtems_rtl_obj_section_handler (mask, obj, fd, handler, data);
}

static size_t
rtems_rtl_obj_sections_loader (rtems_chain_control* sections,
                               uint32_t             mask,
                               int                  fd,
                               off_t                offset,
                               uint8_t*             base)
{
  rtems_chain_node* node = rtems_chain_first (sections);
  size_t            base_offset = 0;
  bool              first = true;
  while (!rtems_chain_is_tail (sections, node))
  {
    rtems_rtl_obj_sect_t* sect = (rtems_rtl_obj_sect_t*) node;
    
    if ((sect->size != 0) && ((sect->flags & mask) != 0))
    {
      uint8_t* sect_base = base + base_offset;
      
      if (!first)
        base_offset = rtems_rtl_sect_align (base_offset, sect->alignment);

      if (rtems_rtl_trace (RTEMS_RTL_TRACE_LOAD_SECT))
        printf ("rtl: loading: %s -> %8p (%zi)\n",
                sect->name, base + base_offset, sect->size);
      
      if ((sect->flags & RTEMS_RTL_OBJ_SECT_LOAD) == RTEMS_RTL_OBJ_SECT_LOAD)
      {
        size_t len;
        
        if (lseek (fd, offset + sect->offset, SEEK_SET) < 0)
        {
          rtems_rtl_set_error (errno, "section load seek failed");
          return false;
        }

        len = sect->size;
        while (len)
        {
          ssize_t r = read (fd, base + base_offset, len);
          if (r <= 0)
          {
            rtems_rtl_set_error (errno, "section load read failed");
            return false;
          }
          base_offset += r;
          len -= r;
        }
      }
      else if ((sect->flags & RTEMS_RTL_OBJ_SECT_ZERO) == RTEMS_RTL_OBJ_SECT_ZERO)
      {
        memset (base + base_offset, 0, sect->size);
        base_offset += sect->size;
      }
      else
      {
        rtems_rtl_set_error (errno, "section has no load op");
        return false;
      }
      
      sect->base = sect_base;
      first = false;
    }
    
    node = rtems_chain_next (node);
  }
  
  return true;
}

bool
rtems_rtl_obj_load_sections (rtems_rtl_obj_t* obj, int fd)
{
  size_t text_size;
  size_t const_size;
  size_t data_size;
  size_t bss_size;

  text_size  = rtems_rtl_obj_text_size (obj) + rtems_rtl_obj_const_alignment (obj);
  const_size = rtems_rtl_obj_const_size (obj) + rtems_rtl_obj_data_alignment (obj);
  data_size  = rtems_rtl_obj_data_size (obj) + rtems_rtl_obj_bss_alignment (obj);
  bss_size   = rtems_rtl_obj_bss_size (obj);
  
  /*
   * The object file's memory allocated on the heap. This should be the
   * first allocation and any temporary allocations come after this
   * so the heap does not become fragmented.
   */
  obj->exec_size = text_size + const_size + data_size + bss_size;
  obj->text_base = malloc (obj->exec_size);
  if (!obj->text_base)
  {
    obj->exec_size = 0;
    rtems_rtl_set_error (ENOMEM, "no memory to load obj");
    return false;
  }

  obj->const_base = obj->text_base + text_size;
  obj->data_base  = obj->const_base + const_size;
  obj->bss_base   = obj->data_base + data_size;

  if (rtems_rtl_trace (RTEMS_RTL_TRACE_LOAD_SECT))
  {
    printf ("rtl: load sect: text  - b:%p s:%zi a:%" PRIu32 "\n",
            obj->text_base, text_size, rtems_rtl_obj_text_alignment (obj));
    printf ("rtl: load sect: const - b:%p s:%zi a:%" PRIu32 "\n",
            obj->const_base, const_size, rtems_rtl_obj_const_alignment (obj));
    printf ("rtl: load sect: data  - b:%p s:%zi a:%" PRIu32 "\n",
            obj->data_base, data_size, rtems_rtl_obj_data_alignment (obj));
    printf ("rtl: load sect: bss   - b:%p s:%zi a:%" PRIu32 "\n",
            obj->bss_base, bss_size, rtems_rtl_obj_bss_alignment (obj));
  }
  
  /*
   * Load all text then data then bss sections in seperate operations so each
   * type of section is grouped together.
   */
  if (!rtems_rtl_obj_sections_loader (&obj->sections, RTEMS_RTL_OBJ_SECT_TEXT,
                                      fd, obj->ooffset, obj->text_base) ||
      !rtems_rtl_obj_sections_loader (&obj->sections, RTEMS_RTL_OBJ_SECT_CONST,
                                      fd, obj->ooffset, obj->const_base) ||
      !rtems_rtl_obj_sections_loader (&obj->sections, RTEMS_RTL_OBJ_SECT_DATA,
                                      fd, obj->ooffset, obj->data_base) ||
      !rtems_rtl_obj_sections_loader (&obj->sections, RTEMS_RTL_OBJ_SECT_BSS,
                                      fd, obj->ooffset, obj->bss_base))
  {
    free (obj->text_base);
    obj->exec_size = 0;
    obj->text_base = 0;
    obj->data_base = 0;
    obj->bss_base = 0;
    return false;
  }

  return true;
}

static void
rtems_rtl_obj_run_cdtors (rtems_rtl_obj_t* obj, uint32_t mask)
{
  rtems_chain_node* node = rtems_chain_first (&obj->sections);
  while (!rtems_chain_is_tail (&obj->sections, node))
  {
    rtems_rtl_obj_sect_t* sect = (rtems_rtl_obj_sect_t*) node;
    if ((sect->flags & mask) == mask)
    {
      rtems_rtl_cdtor_t* handler;
      size_t             handlers = sect->size / sizeof (rtems_rtl_cdtor_t);
      int                c;
      for (c = 0, handler = sect->base; c < handlers; ++c)
        if (*handler)
          (*handler) ();
    }
    node = rtems_chain_next (node);
  }
}

void
rtems_rtl_obj_run_ctors (rtems_rtl_obj_t* obj)
{
  return rtems_rtl_obj_run_cdtors (obj, RTEMS_RTL_OBJ_SECT_CTOR);
}

void
rtems_rtl_obj_run_dtors (rtems_rtl_obj_t* obj)
{
  return rtems_rtl_obj_run_cdtors (obj, RTEMS_RTL_OBJ_SECT_DTOR);
}

/**
 * Find a module in an archive returning the offset in the archive in the
 * object descriptor.
 */
static bool
rtems_rtl_obj_archive_find (rtems_rtl_obj_t* obj, int fd)
{
#define RTEMS_RTL_AR_IDENT      "!<arch>\n"
#define RTEMS_RTL_AR_IDENT_SIZE (sizeof (RTEMS_RTL_AR_IDENT) - 1)
#define RTEMS_RTL_AR_FHDR_BASE  RTEMS_RTL_AR_IDENT_SIZE
#define RTEMS_RTL_AR_FNAME      (0)
#define RTEMS_RTL_AR_FNAME_SIZE (16)
#define RTEMS_RTL_AR_SIZE       (48)
#define RTEMS_RTL_AR_SIZE_SIZE  (10)
#define RTEMS_RTL_AR_MAGIC      (58)
#define RTEMS_RTL_AR_MAGIC_SIZE (2)
#define RTEMS_RTL_AR_FHDR_SIZE  (60)

  size_t  fsize = obj->fsize;
  off_t   extended_file_names;
  uint8_t header[RTEMS_RTL_AR_FHDR_SIZE];
  bool    scanning;
  
  if (read (fd, &header[0], RTEMS_RTL_AR_IDENT_SIZE) !=  RTEMS_RTL_AR_IDENT_SIZE)
  {
    rtems_rtl_set_error (errno, "reading archive identifer");
    return false;
  }

  if (memcmp (header, RTEMS_RTL_AR_IDENT, RTEMS_RTL_AR_IDENT_SIZE) != 0)
  {
    rtems_rtl_set_error (EINVAL, "invalid archive identifer");
    return false;
  }

  /*
   * Seek to the current offset in the archive and if we have a valid archive
   * file header present check the file name for a match with the oname field
   * of the object descriptor. If the archive header is not valid and it is the
   * first pass reset the offset and start the search again in case the offset
   * provided is not valid any more.
   *
   * The archive can have a symbol table at the start. Ignore it. A symbol
   * table has the file name '/'.
   *
   * The archive can also have the GNU extended file name table. This
   * complicates the processing. If the object's file name starts with '/' the
   * remainder of the file name is an offset into the extended file name
   * table. To find the extended file name table we need to scan from start of
   * the archive for a file name of '//'. Once found we remeber the table's
   * start and can direct seek to file name location. In other words the scan
   * only happens once.
   *
   * If the name had the offset encoded we go straight to that location.
   */

  if (obj->ooffset != 0)
    scanning = false;
  else
  {
    scanning = true;
    obj->ooffset = RTEMS_RTL_AR_FHDR_BASE;
  }

  extended_file_names = 0;
  
  while (obj->ooffset < fsize)
  {
    /*
     * Clean up any existing data so 
     */
    memset (header, 0, sizeof (header));
    
    if (!rtems_rtl_seek_read (fd, obj->ooffset, RTEMS_RTL_AR_FHDR_SIZE, &header[0]))
    {
      rtems_rtl_set_error (errno, "seek/read archive file header");
      obj->ooffset = 0;
      obj->fsize = 0;
      return false;
    }

    if ((header[RTEMS_RTL_AR_MAGIC] != 0x60) ||
        (header[RTEMS_RTL_AR_MAGIC + 1] != 0x0a))
    {
      if (scanning)
      {
        rtems_rtl_set_error (EINVAL, "invalid archive file header");
        obj->ooffset = 0;
        obj->fsize = 0;
        return false;
      }

      scanning = true;
      obj->ooffset = RTEMS_RTL_AR_FHDR_BASE;
      continue;
    }

    /*
     * The archive header is always aligned to an even address.
     */
    obj->fsize = (rtems_rtl_scan_decimal (&header[RTEMS_RTL_AR_SIZE],
                                          RTEMS_RTL_AR_SIZE_SIZE) + 1) & ~1;
    
    /*
     * Check for the GNU extensions.
     */
    if (header[0] == '/')
    {
      off_t extended_off;
      
      switch (header[1])
      {
        case ' ':
          /*
           * Symbols table. Ignore the table.
           */
          break;
        case '/':
          /*
           * Extended file names table. Remember.
           */
          extended_file_names = obj->ooffset + RTEMS_RTL_AR_FHDR_SIZE;
          break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          /*
           * Offset into the extended file name table. If we do not have the
           * offset to the extended file name table find it.
           */
          extended_off =
            rtems_rtl_scan_decimal (&header[1], RTEMS_RTL_AR_FNAME_SIZE);
          
          if (extended_file_names == 0)
          {
            off_t off = obj->ooffset;
            while (extended_file_names == 0)
            {
              off_t esize =
                (rtems_rtl_scan_decimal (&header[RTEMS_RTL_AR_SIZE],
                                         RTEMS_RTL_AR_SIZE_SIZE) + 1) & ~1;
              off += esize + RTEMS_RTL_AR_FHDR_SIZE;
              
              if (!rtems_rtl_seek_read (fd, off,
                                        RTEMS_RTL_AR_FHDR_SIZE, &header[0]))
              {
                rtems_rtl_set_error (errno,
                                     "seeking/reading archive ext file name header");
                obj->ooffset = 0;
                obj->fsize = 0;
                return false;
              }

              if ((header[RTEMS_RTL_AR_MAGIC] != 0x60) ||
                  (header[RTEMS_RTL_AR_MAGIC + 1] != 0x0a))
              {
                rtems_rtl_set_error (errno, "invalid archive file header");
                obj->ooffset = 0;
                obj->fsize = 0;
                return false;
              }
              
              if ((header[0] == '/') && (header[1] == '/'))
              {
                extended_file_names = off + RTEMS_RTL_AR_FHDR_SIZE;
                break;
              }
            }
          }

          if (extended_file_names)
          {
            /*
             * We know the offset in the archive to the extended file. Read the
             * name from the table and compare with the name we are after.
             */
#define RTEMS_RTL_MAX_FILE_SIZE (256)
            char  name[RTEMS_RTL_MAX_FILE_SIZE];

            if (!rtems_rtl_seek_read (fd, extended_file_names + extended_off,
                                      RTEMS_RTL_MAX_FILE_SIZE, (uint8_t*) &name[0]))
            {
              rtems_rtl_set_error (errno,
                                   "invalid archive ext file seek/read");
              obj->ooffset = 0;
              obj->fsize = 0;
              return false;
            }

            if (rtems_rtl_match_name (obj, name))
            {
              obj->ooffset += RTEMS_RTL_AR_FHDR_SIZE;
              return true;
            }
          }
          break;
        default:
          /*
           * Ignore the file because we do not know what it it.
           */
          break;
      }
    }
    else
    {
      if (rtems_rtl_match_name (obj, (const char*) &header[RTEMS_RTL_AR_FNAME]))
      {
        obj->ooffset += RTEMS_RTL_AR_FHDR_SIZE;
        return true;
      }
    }

    obj->ooffset += obj->fsize + RTEMS_RTL_AR_FHDR_SIZE;
    
  }

  rtems_rtl_set_error (ENOENT, "object file not found");
  obj->ooffset = 0;
  obj->fsize = 0;
  return false;
}

bool
rtems_rtl_obj_load (rtems_rtl_obj_t* obj)
{
  int fd;

  if (!obj->fname)
  {
    rtems_rtl_set_error (ENOMEM, "invalid object file name path");
    return false;
  }
  
  fd = open (obj->fname, O_RDONLY);
  if (fd < 0)
  {
    rtems_rtl_set_error (ENOMEM, "opening for object file");
    return false;
  }

  /*
   * Find the object file in the archive if it is an archive that
   * has been opened.
   */
  if (obj->aname != NULL)
  {
    if (!rtems_rtl_obj_archive_find (obj, fd))
    {
      rtems_rtl_obj_caches_flush ();
      close (fd);
      return false;
    }
  }

  /*
   * Call the format specific loader. Currently this is a call to the ELF
   * loader. This call could be changed to allow probes then calls if more than
   * one format is supported.
   */
  if (!rtems_rtl_obj_file_load (obj, fd))
  {
    rtems_rtl_obj_caches_flush ();
    close (fd);
    return false;
  }

  rtems_rtl_obj_caches_flush ();

  close (fd);

  return true;
}

bool
rtems_rtl_obj_unload (rtems_rtl_obj_t* obj)
{
  rtems_rtl_obj_symbol_erase (obj);
  return rtems_rtl_obj_free (obj);
}
