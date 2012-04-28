/*
 *  $Id$
 *
 * RTEMS Project (http://www.rtems.org/)
 *
 * Copyright 2010 Chris Johns (chrisj@rtems.org)
 */

/**
 * Run Time Linker test program.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>

#include <dlfcn.h>

#include <rtems.h>
#include <rtems/bdbuf.h>
#include <rtems/blkdev.h>
#include <rtems/diskdevs.h>
#include <rtems/error.h>
#include <rtems/fsmount.h>
#include <rtems/ramdisk.h>
#include <rtems/shell.h>
#include <rtems/untar.h>

#if RTEMS_APP_FLASHDISK
#include <rtems/flashdisk.h>
#include <libchip/am29lv160.h>
#endif

#if RTEMS_APP_IDEDISK
#include <libchip/ide_ctrl.h>
#include <libchip/ata.h>
#endif

#if pc586
#include "pc386-gdb.h"
int remote_debug;
#endif

#include <rtl-shell.h>
#include <rtl-trace.h>

#include <dlfcn-shell.h>

/*
 *  The tarfile is built automatically externally so we need to account
 *  for the leading symbol on the names.
 */
#if defined(__sh__)
  #define SYM(_x) _x
#else
  #define SYM(_x) _ ## _x
#endif

/**
 * The shell-init tarfile parameters.
 */
extern int SYM(binary_fs_root_tar_start);
extern int SYM(binary_fs_root_tar_size);

#define TARFILE_START SYM(binary_fs_root_tar_start)
#define TARFILE_SIZE  SYM(binary_fs_root_tar_size)

/**
 * The RAM Disk configuration.
 */
rtems_ramdisk_config rtems_ramdisk_configuration[] =
{
  {
    block_size: 512,
    block_num:  3 * 1024 * 2,
    location:   NULL
  }
};

/**
 * The number of RAM Disk configurations.
 */
size_t rtems_ramdisk_configuration_size = 1;

/**
 * Create the RAM Disk Driver entry.
 */
rtems_driver_address_table rtems_ramdisk_io_ops = {
  initialization_entry: ramdisk_initialize,
  open_entry:           rtems_blkdev_generic_open,
  close_entry:          rtems_blkdev_generic_close,
  read_entry:           rtems_blkdev_generic_read,
  write_entry:          rtems_blkdev_generic_write,
  control_entry:        rtems_blkdev_generic_ioctl
};

#if RTEMS_APP_FLASHDISK
/**
 * The Flash Device Driver configuration.
 */
const rtems_am29lv160_config rtems_am29lv160_configuration[] =
{
  {
    bus_8bit: 0,
    base:     (void*) 0xFFE00000
  }
};

/**
 * The number of AM29LV160 configurations.
 */
uint32_t rtems_am29lv160_configuration_size = 1;

/**
 * The Flash Segment descriptor. The mcf5235 board uses the
 * bottom part of the flash for the dBug monitor.
 */
const rtems_fdisk_segment_desc rtems_mcf5235_segment_descriptor[] =
{
  {
    count:   26,
    segment: 0,
    offset:  0x00050000,
    size:    RTEMS_FDISK_KBYTES (64)
  }
};

/**
 * The Flash Device descriptor.
 */
const rtems_fdisk_device_desc rtems_mcf5235_device_descriptor[] =
{
  {
    segment_count: 1,
    segments:      &rtems_mcf5235_segment_descriptor[0],
    flash_ops:     &rtems_am29lv160_handlers
  }
};

/**
 * The Flash Disk configuration.
 */
const rtems_flashdisk_config rtems_flashdisk_configuration[] =
{
  {
    block_size:         512,
    device_count:       1,
    devices:            &rtems_mcf5235_device_descriptor[0],
    flags:              RTEMS_FDISK_BLANK_CHECK_BEFORE_WRITE,
    unavail_blocks:     256,
    compact_segs:       100,
    avail_compact_segs: 100,
    info_level:         0
  }
};

/**
 * The number of Flash Disk configurations.
 */
size_t rtems_flashdisk_configuration_size = 1;

/**
 * Create the Flash Disk Driver entry.
 */
rtems_driver_address_table rtems_flashdisk_io_ops = {
  initialization_entry: rtems_fdisk_initialize,
  open_entry:           rtems_blkdev_generic_open,
  close_entry:          rtems_blkdev_generic_close,
  read_entry:           rtems_blkdev_generic_read,
  write_entry:          rtems_blkdev_generic_write,
  control_entry:        rtems_blkdev_generic_ioctl
};
#endif

#if RTEMS_APP_IDEDISK
/**
 * Create the IDE Disk Driver entry.
 */
rtems_driver_address_table rtems_idedisk_io_ops = {
  initialization_entry: rtems_ata_initialize,
  open_entry:           rtems_blkdev_generic_open,
  close_entry:          rtems_blkdev_generic_close,
  read_entry:           rtems_blkdev_generic_read,
  write_entry:          rtems_blkdev_generic_write,
  control_entry:        rtems_blkdev_generic_ioctl
};
#endif

/**
 * Let the IO system allocation the next available major number.
 */
#define RTEMS_DRIVER_AUTO_MAJOR (0)

/**
 * Start the RTEMS Shell.
 */
static void
shell_start (void)
{
  rtems_status_code sc;
  printf ("Starting shell....\n\n");
  sc = rtems_shell_init ("fstst", 60 * 1024, 150, "/dev/console", 0, 1, NULL);
  if (sc != RTEMS_SUCCESSFUL)
    printf ("error: starting shell: %s (%d)\n", rtems_status_text (sc), sc);
}

/**
 * Run the /shell-init script.
 */
static void
shell_init_script (void)
{
  rtems_status_code sc;
  printf ("Running /shell-init....\n\n");
  sc = rtems_shell_script ("fstst", 60 * 1024, 160, "/shell-init", "stdout",
                           0, 1, 1);
  if (sc != RTEMS_SUCCESSFUL)
    printf ("error: running shell script: %s (%d)\n", rtems_status_text (sc), sc);
}

static int
setup_ramdisk (void)
{
  rtems_device_major_number major;
  rtems_status_code         sc;
  
  /*
   * Register the RAM Disk driver.
   */
  printf ("Register RAM Disk Driver: ");
  sc = rtems_io_register_driver (RTEMS_DRIVER_AUTO_MAJOR,
                                 &rtems_ramdisk_io_ops,
                                 &major);
  if (sc != RTEMS_SUCCESSFUL)
  {
    printf ("error: ramdisk driver not initialised: %s\n",
            rtems_status_text (sc));
    return 1;
  }
  
  printf ("successful\n");

  return 0;
}

static int
setup_flashdisk (void)
{
#if RTEMS_APP_FLASHDISK
  rtems_device_major_number major;
  rtems_status_code         sc;
  
  /*
   * Register the Flash Disk driver.
   */
  printf ("Register Flash Disk Driver: ");
  sc = rtems_io_register_driver (RTEMS_DRIVER_AUTO_MAJOR,
                                 &rtems_flashdisk_io_ops,
                                 &major);
  if (sc != RTEMS_SUCCESSFUL)
  {
    printf ("error: flashdisk driver not initialised: %s\n",
            rtems_status_text (sc));
    return 1;
  }
  
  printf ("successful\n");
#endif
  return 0;
}

#if RTEMS_APP_IDEDISK
extern rtems_status_code rtems_ide_part_table_initialize (const char* );
#endif

static int
setup_idedisk (const char* path)
{
#if RTEMS_APP_IDEDISK
  rtems_status_code sc;

  /*
   * Register the IDE Disk driver.
   */
  printf ("Read IDE Disk Partition Table: ");
  sc = rtems_ide_part_table_initialize (path);
  if (sc != RTEMS_SUCCESSFUL)
    printf ("error: ide partition table not found: %s\n",
            rtems_status_text (sc));
  else
    printf ("successful\n");

#endif
  return 0;
}

static int
setup_rootfs (void)
{
  rtems_status_code sc;
  
  printf("Loading filesystem: ");
  
  sc = Untar_FromMemory((void *)(&TARFILE_START), (size_t)&TARFILE_SIZE);

  if (sc != RTEMS_SUCCESSFUL)
  {
    printf ("error: untar failed: %s\n", rtems_status_text (sc));
    return 1;
  }
  
  printf ("successful\n");

  return 0;
}

static int
shell_flash_erase (int argc, char* argv[])
{
#if RTEMS_APP_FLASHDISK
  const char* driver = NULL;
  int         arg;
  int         fd;
  
  for (arg = 1; arg < argc; arg++)
  {
    if (argv[arg][0] == '-')
    {
      printf ("error: invalid option: %s\n", argv[arg]);
      return 1;
    }
    else
    {
      if (!driver)
        driver = argv[arg];
      else
      {
        printf ("error: only one driver name allowed: %s\n", argv[arg]);
        return 1;
      }
    }
  }
  
  printf ("erase flash disk: %s\n", driver);
  
  fd = open (driver, O_WRONLY, 0);
  if (fd < 0)
  {
    printf ("error: flash driver open failed: %s\n", strerror (errno));
    return 1;
  }
  
  if (ioctl (fd, RTEMS_FDISK_IOCTL_ERASE_DISK) < 0)
  {
    printf ("error: flash driver erase failed: %s\n", strerror (errno));
    return 1;
  }
  
  close (fd);
  
  printf ("flash disk erased successful\n");
#endif
  return 0;
}

int
main (int argc, char* argv[])
{
  struct termios term;
  int            ret;

#if pc586
  int arg;
  for (arg = 1; arg < argc; arg++)
    if (strcmp (argv[arg], "--gdb") == 0)
      pc386_gdb_init ();
#endif

  if (tcgetattr(fileno(stdout), &term) < 0)
    printf ("error: cannot get terminal attributes: %s\n", strerror (errno));
  cfsetispeed (&term, B115200);
  cfsetospeed (&term, B115200);
  if (tcsetattr (fileno(stdout), TCSADRAIN, &term) < 0)
    printf ("error: cannot set terminal attributes: %s\n", strerror (errno));
  
  if (tcgetattr(fileno(stdin), &term) < 0)
    printf ("error: cannot get terminal attributes: %s\n", strerror (errno));
  cfsetispeed (&term, B115200);
  cfsetospeed (&term, B115200);
  if (tcsetattr (fileno(stdin), TCSADRAIN, &term) < 0)
    printf ("error: cannot set terminal attributes: %s\n", strerror (errno));
  
  printf ("\nRTEMS Run Time Link Editor Test, Version " \
          PACKAGE_VERSION "\n\n");

  ret = setup_ramdisk ();
  if (ret)
    exit (ret);
  
  ret = setup_flashdisk ();
  if (ret)
    exit (ret);

  ret = setup_idedisk ("/dev/hda");
  if (ret)
    exit (ret);
  
  ret = setup_idedisk ("/dev/hdb");
  if (ret)
    exit (ret);
  
  ret = setup_rootfs ();
  if (ret)
    exit (ret);

#if RTEMS_RTL_TRACE
  rtems_shell_add_cmd ("rtl-trace", "misc",
                       "RLT trace",
                       rtems_rtl_trace_shell_command);
#endif
  
  rtems_shell_add_cmd ("fderase", "misc",
                       "fderase driver", shell_flash_erase);
  rtems_shell_add_cmd ("rtl", "misc",
                       "Runtime Linker", rtems_rtl_shell_command);
  rtems_shell_add_cmd ("dlo", "misc",
                       "load object file", shell_dlopen);
  rtems_shell_add_cmd ("dlc", "misc",
                       "unload object file", shell_dlclose);
  rtems_shell_add_cmd ("dls", "misc",
                       "symbol search file", shell_dlsym);
  rtems_shell_add_cmd ("dlx", "misc",
                       "execute a call to the symbol", shell_dlcall);
  
  shell_init_script ();

  while (true)
    shell_start ();
  
  rtems_task_delete (RTEMS_SELF);

  return 0;
}

#if pc586
void
rtems_fatal_error_occurred (uint32_t code)
{
  printf ("fatal error: %08lx\n", code);
  for (;;);
}
#endif
