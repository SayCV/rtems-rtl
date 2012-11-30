#
#  COPYRIGHT (c) 2012 Chris Johns <chrisj@rtems.org>
#
#  The license and distribution terms for this file may be
#  found in the file LICENSE in this distribution or at
#  http://www.rtems.com/license/LICENSE.
#
# Covert the list of symbols into a C structure.
#
#

function c_header()
{
  print ("/*");
  print (" * RTEMS Global Symbol Table");
  print (" *  Automatically generated. Do not edit, just regenerate.");
  print (" */");
  print ("");
  print ("extern unsigned char __rtems_rtl_base_globals[];");
  print ("extern unsigned int __rtems_rtl_base_globals_size;");
  print ("");
  print ("asm(\"  .align   4\");");
  print ("asm(\"__rtems_rtl_base_globals:\");");
}

function c_trailer()
{
  print ("asm(\"  .byte    0\");");
  print ("asm(\"  .align 0\");");
  print ("asm(\"  .ascii \\\"\\xde\\xad\\xbe\\xef\\\"\");");
  print ("asm(\"  .align   4\");");
  print ("asm(\"__rtems_rtl_base_globals_size:\");");
  print ("asm(\"  .long __rtems_rtl_base_globals_size - __rtems_rtl_base_globals\");");
  print ("");
  print ("void rtems_rtl_base_sym_global_add (const unsigned char* , unsigned int );");
}

function c_rtl_call_body()
{
  print ("{");
  print ("  rtems_rtl_base_sym_global_add (__rtems_rtl_base_globals,");
  print ("                                 __rtems_rtl_base_globals_size);");
  print ("}");
}

function c_constructor_trailer()
{
  c_trailer();
  print ("static void init(void) __attribute__ ((constructor));");
  print ("static void init(void)");
  c_rtl_call_body();
}

function c_embedded_trailer()
{
  c_trailer();
  print ("void rtems_rtl_base_global_syms_init(void);");
  print ("void rtems_rtl_base_global_syms_init(void)");
  c_rtl_call_body();
}

BEGIN {
  FS = "[ \t\n]";
  OFS = " ";
  started = 0
  embed = 1
  for (a = 0; a < ARGC; ++a)
  {
    if (ARGV[a] == "--no-embed")
    {
      embed = 1
      delete ARGV[a];
    }
    else if (ARGV[a] != "-")
    {
      ap = index (ARGV[a], "awk")
      if ((ap == 0) || (ap != (length(ARGV[a]) - 2)))
      {
        print ("invalid option:", ARGV[a]);
        exit 2
      }
    }
  }
  c_header();
  syms = 0
  started = 1
}
END {
  if (started)
  {
    for (s = 0; s < syms; ++s)
    {
      printf ("asm(\"  .asciz \\\"%s\\\"\");\n", symbols[s]);
      if (embed)
      {
        printf ("asm(\"  .align 0\");\n");
        printf ("asm(\"  .long %s\");\n", symbols[s]);
      }
      else
        printf ("asm(\"  .long 0x%s\");\n", addresses[s]);
    }
    if (embed)
      c_embedded_trailer();
    else
      c_constructor_trailer();
  }
}

#
# Parse the output of 'nm'
#

{
  #
  # We need 3 fields
  #
  if (NF == 3)
  {
    if (($3 != "__DTOR_END__") ||
        ($3 != "__CTOR_END__") ||
        ($3 != "__DYNAMIC"))
    {
      symbols[syms] = $3;
      addresses[syms] = $1;
      ++syms;
    }
  }
}
