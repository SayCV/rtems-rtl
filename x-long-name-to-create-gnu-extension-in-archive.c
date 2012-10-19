

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "x.h"

static int zero;
unsigned int public = 0x12345678;

void w_writeln(float d);
void x_writeln(const char* s);
void y_writeln(const char* s) __attribute__ ((section (".bar")));
int z_writeln(int argc, const char* argv[]);
int my_main (int argc, char* argv[]);

void
w_writeln(float d)
{
  printf ("%f / 3 = %f\n", d / 3);
}

void
x_writeln(const char* s)
{
  printf ("%s\n", s);
}

void
y_writeln(const char* s)
{
  x_writeln (s);
}

int
z_writeln(int argc, const char* argv[])
{
  int arg;
  printf ("public = 0x%08x, zero = %d\n", public, ++zero);
  for (arg = 0; arg < argc; ++arg)
    y_writeln (argv[arg]);
  return 123;
}

int
my_main (int argc, char* argv[])
{
  exit (0);
}
