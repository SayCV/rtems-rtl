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

    opt.add_option('--gsyms-embedded',
                   action = 'store_true',
                   default = False,
                   dest = 'gsym_embedded',
                   help = 'Embedded the global symbols in the executable (buggy).')

def configure(conf):
    rtems.configure(conf)

    conf.env.ASCIIDOC = conf.find_program(['asciidoc.py'], mandatory = False)
    conf.env.ASCIIDOC_FLAGS = ['-b', 'html', '-a', 'data-uri', '-a', 'icons', '-a', 'max-width=55em-a']

    conf.env.GSYM_EMBEDDED = conf.options.gsym_embedded

def build(bld):
    rtems.build(bld)

    arch_bsp = bld.get_env()['RTEMS_ARCH_BSP']
    arch = bld.get_env()['RTEMS_ARCH']

    bld.includes = ['.',
                    'libbsd/include',
                    'libbsd/include/arch/' + arch]

    bld.defines = ['PACKAGE_VERSION="' + version + '"']
    if bld.env.GSYM_EMBEDDED:
        bld.defines += ['RTL_GSYM_EMBEDDED=1']

    rtl_bspinit(bld, arch)
    rtl_root_fs(bld)
    rtl_gsyms(bld)

    bld(target = 'rtl',
        features = 'c cstlib',
        includes = bld.includes,
        cflags = '-g',
        source = ['dlfcn.c',
                  'dlfcn-shell.c',
                  'rtl.c',
                  'rtl-alloc-heap.c',
                  'rtl-allocator.c',
                  'rtl-chain-iterator.c',
                  'rtl-debugger.c',
                  'rtl-elf.c',
                  'rtl-error.c',
                  'rtl-obj.c',
                  'rtl-obj-cache.c',
                  'rtl-shell.c',
                  'rtl-string.c',
                  'rtl-sym.c',
                  'rtl-trace.c',
                  'rtl-unresolved.c',
                  'rtl-mdreloc-%s.c' % (arch)],
        install_path = '${PREFIX}/%s' % (rtems.arch_bsp_lib_path(arch_bsp)))

    bsp_include_base = '${PREFIX}/%s' % (rtems.arch_bsp_include_path(arch_bsp))

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

    bld(target = 'rtld',
        features = 'c cprogram',
        source = ['init.c',
                  'main.c',
                  'fs-root-tarfile.o'],
        includes = bld.includes,
        defines = bld.defines,
        cflags = '-g',
        use = ['rtl', 'bspinit', 'rootfs'],
        install_path = '${PREFIX}/%s/samples' % (rtems.arch_bsp_path(arch_bsp)))

    if bld.env.ASCIIDOC:
        bld(target = 'rtems-rtl.html', source = 'rtems-rtl.txt')

def rebuild(ctx):
    import waflib.Options
    waflib.Options.commands.extend(['clean', 'build'])

def tags(ctx):
    ctx.exec_command('etags $(find . -name \*.[sSch])', shell = True)

def rtl_bspinit(bld, arch):
    if arch == 'arm':
        bld(target = 'bspinit',
            features = 'c',
            includes = bld.includes,
            defines = bld.defines,
            source = ['bspinit.c'])

def mmap_source(bld, arch_bsp):
    bld(target = 'mmap',
        features = 'c cstlib',
        includes = bld.includes,
        source = ['mmap.c',
                  'munmap.c'],
        install_path = '${PREFIX}/%s' % (rtems.arch_bsp_lib_path(arch_bsp)))

def rtl_root_fs(bld):
    bld(target = 'fs-root.tar',
        source = ['shell-init'],
        rule = 'tar cf - ${SRC} > ${TGT}')
    bld.objects(name = 'rootfs',
                target = 'fs-root-tarfile.o',
                source = 'fs-root.tar',
                rule = '${OBJCOPY} -I binary -B ${RTEMS_ARCH} ${OBJCOPY_FLAGS} ${SRC} ${TGT}')

def rtl_gsyms(bld):
    import os.path
    src = os.path.join(bld.get_variant_dir(), 'gsyms.c')
    if os.path.exists(src):
        if os.path.exists(os.path.join(bld.get_variant_dir(), 'rtld')):
            import os
            sb = os.stat(src)
            if sb.st_size == 0:
                if bld.env.GSYM_EMBEDDED:
                    flags = '--embed'
                else:
                    flags = ''
                bld(name = 'gsyms',
                    target = 'gsyms.c',
                    always = True,
                    rule = '${NM} -g rtld | awk -f ../../mksyms.awk - ' + flags + ' > ${TGT}')
    else:
        open(src, 'a').close()
    bld(target = 'rtld-gsyms',
        features = 'c',
        includes = bld.includes,
        defines = bld.defines,
        source = ['rtld-gsyms.c'],
        depends_on = 'gsyms')

import waflib.TaskGen
waflib.TaskGen.declare_chain(name      = 'html',
                             rule      = '${ASCIIDOC} ${ASCIIDOC_FLAGS} -o ${TGT} ${SRC}',
                             shell     = False,
                             ext_in    = '.txt',
                             ext_out   = '.html',
                             reentrant = False)
