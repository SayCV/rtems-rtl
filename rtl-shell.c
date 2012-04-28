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
 * @ingroup rtems_rtld
 *
 * @brief RTEMS Run-Time Link Editor Shell Commands
 *
 * A simple RTL command to aid using the RTL.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>

//#if SIZEOF_OFF_T == 8
#define PRIdoff_t PRIo64
//#elif SIZEOF_OFF_T == 4
//#define PRIdoff_t PRIo32
//#else
//#error "unsupported size of off_t"
//#endif

#include <stdio.h>
#include <string.h>

#include <rtl.h>
#include <rtl-chain-iterator.h>
#include <rtl-shell.h>
#include <rtl-trace.h>

/**
 * The type of the shell handlers we have.
 */
typedef int (*rtems_rtl_shell_handler_t) (rtems_rtl_data_t* rtl, int argc, char *argv[]);

/**
 * Table of handlers we parse to invoke the command.
 */
typedef struct
{
  const char*               name;    /**< The sub-command's name. */
  rtems_rtl_shell_handler_t handler; /**< The sub-command's handler. */
  const char*               help;    /**< The sub-command's help. */
} rtems_rtl_shell_cmd_t;

/**
 * Object summary data.
 */
typedef struct
{
  int    count;   /**< The number of object files. */
  size_t exec;    /**< The amount of executable memory allocated. */
  size_t symbols; /**< The amount of symbol memory allocated. */
} rtems_rtl_obj_summary_t;

/**
 * Object summary iterator.
 */
static bool
rtems_rtl_obj_summary_iterator (rtems_chain_node* node, void* data)
{
  rtems_rtl_obj_summary_t* summary = data;
  rtems_rtl_obj_t*         obj = (rtems_rtl_obj_t*) node;
  ++summary->count;
  summary->exec += obj->exec_size;
  summary->symbols += obj->global_size;
  return true;
}

/**
 * Count the number of symbols.
 */
static int
rtems_rtl_count_symbols (rtems_rtl_data_t* rtl)
{
  int count;
  int bucket;
  for (count = 0, bucket = 0; bucket < rtl->globals.nbuckets; ++bucket)
    count += rtems_rtl_chain_count (&rtl->globals.buckets[bucket]);
  return count;
}

static int
rtems_rtl_shell_status (rtems_rtl_data_t* rtl, int argc, char *argv[])
{
  rtems_rtl_obj_summary_t summary;
  size_t                  total_memory;
  
  summary.count   = 0;
  summary.exec    = 0;
  summary.symbols = 0;
  rtems_rtl_chain_iterate (&rtl->objects,
                           rtems_rtl_obj_summary_iterator,
                           &summary);
  /*
   * Currently does not include the name strings in the obj struct.
   */
  total_memory =
    sizeof (*rtl) + (summary.count * sizeof (rtems_rtl_obj_t)) +
    summary.exec + summary.symbols;
  
  printf ("Runtime Linker Status:\n");
  printf ("        paths: %s\n", rtl->paths);
  printf ("      objects: %d\n", summary.count);
  printf (" total memory: %zi\n", total_memory);
  printf ("  exec memory: %zi\n", summary.exec);
  printf ("   sym memory: %zi\n", summary.symbols);
  printf ("      symbols: %d\n", rtems_rtl_count_symbols (rtl));
  
  return 0;
}

/**
 * Object print data.
 */
typedef struct
{
  rtems_rtl_data_t* rtl; /**< The RTL data. */
  int  indent;           /**< Spaces to indent. */
  bool names;            /**< Print details of all names. */
  bool memory_map;       /**< Print the memory map. */
  bool symbols;          /**< Print the global symbols. */
  bool base;             /**< Include the base object file. */
} rtems_rtl_obj_print_t;

/**
 * Return the different between 2 void*.
 */
static size_t
rtems_rtl_delta_voids (void* higher, void* lower)
{
  char* ch = higher;
  char* cl = lower;
  return ch - cl;
}

/**
 * Object print iterator.
 */
static bool
rtems_rtl_obj_print_iterator (rtems_chain_node* node, void* data)
{
  rtems_rtl_obj_print_t* print = data;
  rtems_rtl_obj_t*       obj = (rtems_rtl_obj_t*) node;
  char                   flags_str[33];

  /*
   * Skip the base module unless asked to show it.
   */
  if (!print->base && (obj == print->rtl->base))
      return true;
      
  printf ("%-*cobject name   : %s\n", print->indent, ' ', obj->oname);
  if (print->names)
  {
    printf ("%-*cfile name     : %s\n", print->indent, ' ', obj->fname);
    printf ("%-*carchive name  : %s\n", print->indent, ' ', obj->aname);
    strcpy (flags_str, "--");
    if (obj->flags & RTEMS_RTL_OBJ_LOCKED)
      flags_str[0] = 'L';
    if (obj->flags & RTEMS_RTL_OBJ_UNRESOLVED)
      flags_str[1] = 'U';
    printf ("%-*cflags         : %s\n", print->indent, ' ', flags_str);
    printf ("%-*cfile offset   : %" PRIdoff_t "\n", print->indent, ' ', obj->ooffset);
    printf ("%-*cfile size     : %zi\n", print->indent, ' ', obj->fsize);
  }
  printf ("%-*cexec size     : %zi\n", print->indent, ' ', obj->exec_size);
  if (print->memory_map)
  {
    printf ("%-*ctext base     : %p (%zi)\n", print->indent, ' ',
            obj->text_base, rtems_rtl_delta_voids (obj->const_base, obj->text_base));
    printf ("%-*cconst base    : %p (%zi)\n", print->indent, ' ',
            obj->const_base, rtems_rtl_delta_voids (obj->data_base, obj->const_base));
    printf ("%-*cdata base     : %p (%zi)\n", print->indent, ' ',
            obj->data_base, rtems_rtl_delta_voids (obj->bss_base, obj->data_base));
    printf ("%-*cbss base      : %p (%zi)\n", print->indent, ' ',
            obj->bss_base, obj->bss_size);
  }
  printf ("%-*csymbols       : %zi\n", print->indent, ' ', obj->global_syms);
  printf ("%-*csymbol memory : %zi\n", print->indent, ' ', obj->global_size);
  if (print->symbols)
  {
    int max_len = 0;
    int s;
    for (s = 0; s < obj->global_syms; ++s)
    {
      int len = strlen (obj->global_table[s].name);
      if (len > max_len)
        max_len = len;
    }
    for (s = 0; s < obj->global_syms; ++s)
      printf ("%-*c%-*s = %p\n", print->indent + 2, ' ',
              max_len, obj->global_table[s].name, obj->global_table[s].value);
  }
  return true;
}

static int
rtems_rtl_shell_list (rtems_rtl_data_t* rtl, int argc, char *argv[])
{
  rtems_rtl_obj_print_t print;
  print.rtl = rtl;
  print.indent = 1;
  print.names = true;
  print.memory_map = true;
  print.symbols = true;
  print.base = false;
  rtems_rtl_chain_iterate (&rtl->objects,
                           rtems_rtl_obj_print_iterator,
                           &print);
  return 0;
}

static int
rtems_rtl_shell_sym (rtems_rtl_data_t* rtl, int argc, char *argv[])
{
  return 0;
}

static int
rtems_rtl_shell_object (rtems_rtl_data_t* rtl, int argc, char *argv[])
{
  return 0;
}

static void
rtems_rtl_shell_usage (const char* arg)
{
  printf ("%s: Runtime Linker\n", arg);
  printf ("  %s [-hl] <command>\n", arg);
  printf ("   where:\n");
  printf ("     command: A n RTL command. See -l for a list plus help.\n");
  printf ("     -h:      This help\n");
  printf ("     -l:      The command list.\n");
}

int
rtems_rtl_shell_command (int argc, char* argv[])
{
  const rtems_rtl_shell_cmd_t table[] =
  {
    { "status", rtems_rtl_shell_status,
      "Display the status of the RTL" },
    { "list", rtems_rtl_shell_list,
      "\tList the object files currently loaded" },
    { "sym", rtems_rtl_shell_sym,
      "\tDisplay the symbols, sym [<name>], sym -o <obj> [<name>]" },
    { "obj", rtems_rtl_shell_object,
      "\tDisplay the object details, obj <name>" }
  };

  int arg;
  int t;
  
  for (arg = 1; arg < argc; arg++)
  {
    if (argv[arg][0] != '-')
      break;

    switch (argv[arg][1])
    {
      case 'h':
        rtems_rtl_shell_usage (argv[0]);
        return 0;
      case 'l':
        printf ("%s: commands are:\n", argv[0]);
        for (t = 0;
             t < (sizeof (table) / sizeof (const rtems_rtl_shell_cmd_t));
             ++t)
          printf ("  %s\t%s\n", table[t].name, table[t].help);
        return 0;
      default:
        printf ("error: unknown option: %s\n", argv[arg]);
        return 1;
    }
  }

  if ((argc - arg) < 1)
    printf ("error: you need to provide a command, try %s -h\n", argv[0]);
  else
  {
    for (t = 0;
         t < (sizeof (table) / sizeof (const rtems_rtl_shell_cmd_t));
         ++t)
    {
      if (strncmp (argv[arg], table[t].name, strlen (argv[arg])) == 0)
      {
        rtems_rtl_data_t* rtl = rtems_rtl_data ();
        int               r;
        if (!rtl)
        {
          printf ("error: cannot lock the linker\n");
          return 1;
        }
        r = table[t].handler (rtl, argc - 1, argv + 1);
        rtems_rtl_unlock ();
        return r;
      }
    }
    printf ("error: command not found: %s (try -h)\n", argv[arg]);
  }
  
  return 1;
}
