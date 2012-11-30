#
# Waf build script for the Run Time Link editor development project.
#
import rtems

version = "1.0.0"

#
# Filter out BSPs in the ARM architecture which it cannot fit in or have other
# issues.
#
filters = {
    'bsps': {
        'out': ['arm/rtl22xx',
                'arm/lpc32xx_mzx_stage_1',
                'arm/lpc23xx_tli800',
                'arm/lpc2362',
                'arm/nds']
        }
    }

def init(ctx):
    rtems.init(ctx, filters)

def options(opt):
    rtems.options(opt)

def configure(conf):
    conf.find_program('awk')
    conf.env.GSYMS_AWK = '%s/mksyms.awk' % (conf.srcnode.abspath())
    conf.env.GSYMS_FLAGS = []

    conf.env.ASCIIDOC = conf.find_program(['asciidoc.py'], mandatory = False)
    conf.env.ASCIIDOC_FLAGS = ['-b', 'html', '-a', 'data-uri', '-a', 'icons', '-a', 'max-width=55em-a']

    rtems.configure(conf)

def build(bld):
    rtems.build(bld)

    arch_bsp = bld.get_env()['RTEMS_ARCH_BSP']
    arch = bld.get_env()['RTEMS_ARCH']

    #
    # The include paths and defines.
    #
    bld.includes = ['.',
                    'libbsd/include',
                    'libbsd/include/arch/' + arch]
    bld.defines = ['PACKAGE_VERSION="' + version + '"',
                   'RTEMS_RTL_ELF_LOADER=1',
                   'RTEMS_RTL_RAP_LOADER=1']
    bld.cflags = ['-g']

    #
    # The ARM as special BSP initialise code.
    #
    if arch == 'arm':
        bld(target = 'bspinit',
            features = 'c',
            includes = bld.includes,
            defines = bld.defines,
            source = ['bspinit.c'])

    bld(target = 'x',
        features = 'c cstlib',
        includes = bld.includes,
        defines = bld.defines,
        source = ['xa.c',
                  'x-long-name-to-create-gnu-extension-in-archive.c'])

    #
    # The RTL library.
    #
    bld(features = 'c cstlib',
        target = 'rtl',
        includes = bld.includes,
        cflags = bld.cflags,
        defines = bld.defines,
        source = ['dlfcn.c',
                  'dlfcn-shell.c',
                  'fastlz.c',
                  'rtl.c',
                  'rtl-alloc-heap.c',
                  'rtl-allocator.c',
                  'rtl-chain-iterator.c',
                  'rtl-debugger.c',
                  'rtl-elf.c',
                  'rtl-error.c',
                  'rtl-obj.c',
                  'rtl-obj-cache.c',
                  'rtl-obj-comp.c',
                  'rtl-rap.c',
                  'rtl-shell.c',
                  'rtl-string.c',
                  'rtl-sym.c',
                  'rtl-trace.c',
                  'rtl-unresolved.c',
                  'rtl-mdreloc-%s.c' % (arch)],
        install_path = '${PREFIX}/%s' % (rtems.arch_bsp_lib_path(arch_bsp)))

    bsp_include_base = '${PREFIX}/%s' % (rtems.arch_bsp_include_path(arch_bsp))

    #
    # Installing the library and headers.
    #
    bld.install_files("%s" % (bsp_include_base),
                      ['libbsd/include/dlfcn.h',
                       'libbsd/include/err.h',
                       'libbsd/include/link.h',
                       'libbsd/include/link_elf.h'])
    bld.install_files("%s/sys" % (bsp_include_base),
                      ['libbsd/include/sys/ansi.h',
                       'libbsd/include/sys/cdefs.h',
                       'libbsd/include/sys/cdefs_elf.h',
                       'libbsd/include/sys/exec_elf.h',
                       'libbsd/include/sys/featuretest.h',
                       'libbsd/include/sys/nb-queue.h'])
    bld.install_files("%s/machine" % (bsp_include_base),
                      ['libbsd/include/arch/%s/machine/ansi.h' % (arch),
                       'libbsd/include/arch/%s/machine/asm.h' % (arch),
                       'libbsd/include/arch/%s/machine/cdefs.h' % (arch),
                       'libbsd/include/arch/%s/machine/elf_machdep.h' % (arch),
                       'libbsd/include/arch/%s/machine/int_types.h' % (arch)])

    #
    # Create an RTEMS kernel to boot and test with. The kernel contains the RTL
    # loader and shell commands to use it.
    #
    # The following performs a prelink (which is actually just a normal link)
    # without a global symbol table to create an ELF file with the
    # symbols. This is processed to create a C file of global symbols and
    # finally the second link occurs with the global symbol table to create the
    # executable to install.
    #
    #
    # Create the root file system for the prelink.
    #
    bld(target = 'fs-root.tar',
        source = ['shell-init', 'libx.a'],
        rule = 'tar cf - ${SRC} > ${TGT}')
    bld.objects(name = 'rootfs.prelink',
                target = 'fs-root-tarfile.o',
                source = 'fs-root.tar',
                rule = '${OBJCOPY} -I binary -B ${RTEMS_ARCH} ${OBJCOPY_FLAGS} ${SRC} ${TGT}')

    bld(features = 'c cprogram',
        target = 'rtld.prelink',
        includes = bld.includes,
        defines = bld.defines,
        cflags = bld.cflags,
        source = ['init.c',
                  'main.c',
                  'fs-root-tarfile.o'],
        use = ['rtl', 'bspinit'],
        install_path = None)

    bld.add_group ()

    #
    # Build the actual kernel with the root file system with the application.
    #

    bld(name = 'gsyms',
        target = 'rtld-gsyms.c',
        source = 'rtld.prelink',
        rule = '${NM} -g ${SRC} | ${AWK} -f ${GSYMS_AWK} ${GSYMS_FLAGS} > ${TGT}')

    bld(target = 'x.rap',
        features = 'c rap',
        xxxx = 'hello',
        rtems_linkflags = ['--base', 'rtld.prelink',
                           '--entry', 'my_main'],
        source = ['xa.c',
                  'x-long-name-to-create-gnu-extension-in-archive.c'])

    bld(target = 'fs-root.tar',
        source = ['shell-init', 'libx.a', 'x.rap'],
        rule = 'tar cf - ${SRC} > ${TGT}')
    bld.objects(name = 'rootfs',
                target = 'fs-root-tarfile.o',
                source = 'fs-root.tar',
                rule = '${OBJCOPY} -I binary -B ${RTEMS_ARCH} ${OBJCOPY_FLAGS} ${SRC} ${TGT}')

    bld(features = 'c cprogram',
        target = 'rtld',
        source = ['init.c',
                  'main.c',
                  'fs-root-tarfile.o',
                  'rtld-gsyms.c'],
        includes = bld.includes,
        defines = bld.defines,
        cflags = bld.cflags,
        use = ['app', 'rtl', 'bspinit', 'rootfs'],
        install_path = '${PREFIX}/%s/samples' % (rtems.arch_bsp_path(arch_bsp)))

    if bld.env.ASCIIDOC:
        bld(target = 'rtems-rtl.html', source = 'rtems-rtl.txt')

def rebuild(ctx):
    import waflib.Options
    waflib.Options.commands.extend(['clean', 'build'])

def tags(ctx):
    ctx.exec_command('etags $(find . -name \*.[sSch])', shell = True)

def mmap_source(bld, arch_bsp):
    bld(target = 'mmap',
        features = 'c cstlib',
        includes = bld.includes,
        source = ['mmap.c',
                  'munmap.c'],
        install_path = '${PREFIX}/%s' % (rtems.arch_bsp_lib_path(arch_bsp)))

import waflib.TaskGen
waflib.TaskGen.declare_chain(name      = 'html',
                             rule      = '${ASCIIDOC} ${ASCIIDOC_FLAGS} -o ${TGT} ${SRC}',
                             shell     = False,
                             ext_in    = '.txt',
                             ext_out   = '.html',
                             reentrant = False)

