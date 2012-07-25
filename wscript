#
# Waf build script for the Run Time Link editor development project.
#
import rtems

version = "1.0.0"

def init(ctx):
    rtems.init(ctx)

def options(opt):
    rtems.options(opt)

def configure(conf):
    rtems.configure(conf)

    conf.env.ASCIIDOC = conf.find_program(['asciidoc.py'], mandatory = False)
    conf.env.ASCIIDOC_FLAGS = ['-b', 'html5', '-a', 'data-uri', '-a', 'icons', '-a', 'max-width=55em-a']

    # hack on at the moment.
    conf.env.GSYM_EMBEDDED = True

def build(bld):
    bld.add_post_fun(rtl_post_build)

    rtems.build(bld)

    arch = bld.get_env()['RTEMS_ARCH']

    bld.includes = ['.',
                    'libbsd/include',
                    'libbsd/include/arch/' + arch]

    bld.defines = ['PACKAGE_VERSION="' + version + '"']
    if bld.env.GSYM_EMBEDDED:
        bld.defines += ['RTL_GSYM_EMBEDDED=1']

    rtl_source(bld, arch)
    rtl_liba(bld, arch)
    rtl_root_fs(bld)
    rtl_gsyms(bld)

    bld(target = 'rtld',
        features = 'c cprogram',
        source = ['init.c',
                  'main.c',
                  'fs-root-tarfile.o'],
        includes = bld.includes,
        defines = bld.defines,
        cflags = '-g',
        use = ['rtl', 'rootfs', 'rtld-gsyms'],
        depends_on = 'gsyms')

    if bld.env.ASCIIDOC:
        bld(target = 'rtems-rtl.html', source = 'rtems-rtl.txt')

def rebuild(ctx):
    import waflib.Options
    waflib.Options.commands.extend(['clean', 'build'])

def tags(ctx):
    ctx.exec_command('etags $(find . -name \*.[sSch])', shell = True)

def rtl_source(bld, arch):
    bld(target = 'rtl',
        features = 'c',
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
                  'rtl-mdreloc-' + arch + '.c'])

def rtl_liba(bld, arch):
    bld(target = 'x',
        features = 'c cstlib',
        includes = bld.includes,
        defines = bld.defines,
        source = ['xa.c',
                  'x-long-name-to-create-gnu-extension-in-archive.c'])

def mmap_source(bld, includes):
    bld(target = 'mmap',
        features = 'c',
        includes = includes,
        source = ['mmap.c',
                  'munmap.c'])

def rtl_root_fs(bld):
    bld(target = 'fs-root.tar',
        source = ['shell-init', 'libx.a'],
        rule = 'tar cf - ${SRC} > ${TGT}')
    bld.objects(name = 'rootfs',
                target = 'fs-root-tarfile.o',
                source = 'fs-root.tar',
                rule = '${OBJCOPY} -I binary -B ${RTEMS_ARCH} ${OBJCOPY_FLAGS} ${SRC} ${TGT}')

def rtl_pre_build(bld):
    pass

def rtl_post_build(bld):
    rtl_gsyms(bld)

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
