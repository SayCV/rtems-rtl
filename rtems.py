#
# RTEMS support for applications.
#
# Copyright 2012 Chris Johns (chrisj@rtems.org)
#
import os
import os.path
import pkgconfig
import subprocess

default_version = '4.11'
default_label = 'rtems-' + default_version
default_path = '/opt/' + default_label

def options(opt):
    opt.add_option('--rtems',
                   default = default_path,
                   dest = 'rtems_path',
                   help = 'Path to an installed RTEMS.')
    opt.add_option('--rtems-tools',
                   default = None,
                   dest = 'rtems_tools',
                   help = 'Path to RTEMS tools.')
    opt.add_option('--rtems-version',
                   default = default_version,
                   dest = 'rtems_version',
                   help = 'RTEMS version (default ' + default_version + ').')
    opt.add_option('--rtems-archs',
                   default = 'all',
                   dest = 'rtems_archs',
                   help = 'List of RTEMS architectures to build.')
    opt.add_option('--rtems-bsps',
                   default = 'all',
                   dest = 'rtems_bsps',
                   help = 'List of BSPs to build.')
    opt.add_option('--show-commands',
                   action = 'store_true',
                   default = False,
                   dest = 'show_commands',
                   help = 'Print the commands as strings.')

def init(ctx):
    try:
        import waflib.Options
        import waflib.ConfigSet

        #
        # Load the configuation set from the lock file.
        #
        env = waflib.ConfigSet.ConfigSet()
        env.load(waflib.Options.lockfile)
    
        #
        # Check the tools, architectures and bsps.
        #
        rtems_tools, archs, arch_bsps = check_options(ctx,
                                                      env.options['rtems_tools'],
                                                      env.options['rtems_path'],
                                                      env.options['rtems_version'],
                                                      env.options['rtems_archs'],
                                                      env.options['rtems_bsps'])

        #
        # Update the contextes for all the bsps.
        #
        from waflib.Build import BuildContext, CleanContext, \
            InstallContext, UninstallContext
        for x in arch_bsps:
            for y in (BuildContext, CleanContext, InstallContext, UninstallContext):
                name = y.__name__.replace('Context','').lower()
                class context(y):
                    cmd = name + '-' + x
                    variant = x
    
        #
        # Add the various commands.
        #
        for cmd in ['build', 'clean']:
            if cmd in waflib.Options.commands:
                waflib.Options.commands.remove(cmd)
                for x in arch_bsps:
                    waflib.Options.commands.insert(0, cmd + '-' + x)
    except:
        pass

def configure(conf):
    #
    # Handle the show commands option.
    #
    if conf.options.show_commands:
        show_commands = 'yes'
    else:
        show_commands = 'no'

    rtems_tools, archs, arch_bsps = check_options(conf,
                                                  conf.options.rtems_tools,
                                                  conf.options.rtems_path,
                                                  conf.options.rtems_version,
                                                  conf.options.rtems_archs,
                                                  conf.options.rtems_bsps)

    _log_header(conf)
    conf.to_log('Architectures: ' + ','.join(archs))

    tools = _find_tools(conf, archs, rtems_tools)

    for ab in arch_bsps:
        env = conf.env.copy()
        env.set_variant(ab)
        conf.set_env_name(ab, env)
        conf.setenv(ab)

        arch = _arch_from_arch_bsp(ab)

        conf.env.RTEMS_PATH = conf.options.rtems_path
        conf.env.RTEMS_VERSION = conf.options.rtems_version
        conf.env.RTEMS_ARCH_BSP = ab
        conf.env.RTEMS_ARCH = arch.split('-')[0]
        conf.env.RTEMS_ARCH_RTEMS = arch
        conf.env.RTEMS_BSP = _bsp_from_arch_bsp(ab)

        for t in tools[arch]:
            conf.env[t] = tools[arch][t]

        conf.load('gcc')
        conf.load('g++')
        conf.load('gas')

        flags = _load_flags(conf, ab, conf.options.rtems_path)

        conf.env.CFLAGS = flags['CFLAGS']
        conf.env.LINKFLAGS = flags['CFLAGS'] + flags['LDFLAGS']
        conf.env.LIB = flags['LIB']
                                   
        #
        # Hack to work around NIOS2 naming.
        #
        if conf.env.RTEMS_ARCH in ['nios2']:
            objcopy_format = 'elf32-little' + conf.env.RTEMS_ARCH
        else:
            objcopy_format = 'elf32-' + conf.env.RTEMS_ARCH

        conf.env.OBJCOPY_FLAGS = ['-O ', objcopy_format]

        conf.env.SHOW_COMMANDS = show_commands

    conf.setenv('')
    
    conf.env.RTEMS_TOOLS = rtems_tools    
    conf.env.ARCHS = archs
    conf.env.ARCH_BSPS = arch_bsps

    conf.env.SHOW_COMMANDS = show_commands

def build(bld):
    if bld.env.SHOW_COMMANDS == 'yes':
        output_command_line()

def check_options(ctx, rtems_tools, rtems_path, rtems_version, rtems_archs, rtems_bsps):
    #
    # Check the paths are valid.
    #
    if not os.path.exists(rtems_path):
        ctx.fatal('RTEMS path not found.')
    if os.path.exists(os.path.join(rtems_path, 'lib', 'pkgconfig')):
        rtems_config = None
    elif os.path.exists(os.path.join(rtems_path, 'rtems-config')):
        rtems_config = os.path.join(rtems_path, 'rtems-config')
    else:
        ctx.fatal('RTEMS path is not valid. No lib/pkgconfig or rtems-config found.')

    #
    # We can more than one path to tools. This happens when testing different
    # versions.
    #
    if rtems_tools is not None:
        rt = rtems_tools.split(',')
        tools = []
        for path in rt:
            if not os.path.exists(path):
                ctx.fatal('RTEMS tools path not found: ' + path)
            if not os.path.exists(os.path.join(path, 'bin')):
                ctx.fatal('RTEMS tools path does not contain a \'bin\' directory: ' + path)
            tools += [os.path.join(path, 'bin')]
    else:
        tools = None

    #
    # Match the archs requested against the ones found. If the user
    # wants all (default) set all used.
    #
    if rtems_archs == 'all':
        archs = _find_installed_archs(rtems_config, rtems_path, rtems_version)
    else:
        archs = _check_archs(rtems_config, rtems_archs, rtems_path, rtems_version)

    if len(archs) == 0:
        ctx.fatal('Could not find any architectures')

    #
    # Get the list of valid BSPs. This process filters the architectures
    # to those referenced by the BSPs.
    #
    if rtems_bsps == 'all':
        arch_bsps = _find_installed_arch_bsps(rtems_config, rtems_path, archs)
    else:
        arch_bsps = _check_arch_bsps(rtems_config, rtems_bsps, rtems_path, archs)

    return tools, archs, arch_bsps

def arch(arch_bsp):
    return _arch_from_arch_bsp(arch_bsp).split('-')[0]

def bsp(arch_bsp):
    return _bsp_from_arch_bsp(arch_bsp)

def arch_bsps(ctx):
    return ctx.env.ARCH_BSPS

def arch_bsp_env(ctx, arch_bsp):
    return ctx.env_of_name(arch_bsp).derive()

def clone_tasks(bld):
    if bld.cmd == 'build':
        for obj in bld.all_task_gen[:]: 
            for x in arch_bsp:
                cloned_obj = obj.clone(x) 
                kind = Options.options.build_kind
                if kind.find(x) < 0:
                    cloned_obj.posted = True
            obj.posted = True 

#
# From the demos. Use this to get the command to cut+paste to play.
#
def output_command_line():
    # first, display strings, people like them
    from waflib import Utils, Logs
    from waflib.Context import Context
    def exec_command(self, cmd, **kw):
        subprocess = Utils.subprocess
        kw['shell'] = isinstance(cmd, str)
        if isinstance(cmd, str):
            Logs.info('%s' % cmd)
        else:
            Logs.info('%s' % ' '.join(cmd)) # here is the change
        Logs.debug('runner_env: kw=%s' % kw)
        try:
            if self.logger:
                self.logger.info(cmd)
                kw['stdout'] = kw['stderr'] = subprocess.PIPE
                p = subprocess.Popen(cmd, **kw)
                (out, err) = p.communicate()
                if out:
                    self.logger.debug('out: %s' % out.decode(sys.stdout.encoding or 'iso8859-1'))
                if err:
                    self.logger.error('err: %s' % err.decode(sys.stdout.encoding or 'iso8859-1'))
                return p.returncode
            else:
                p = subprocess.Popen(cmd, **kw)
                return p.wait()
        except OSError:
            return -1
    Context.exec_command = exec_command

    # Change the outputs for tasks too
    from waflib.Task import Task
    def display(self):
        return '' # no output on empty strings

    Task.__str__ = display

def _find_tools(conf, archs, paths):
    tools = {}
    for arch in archs:
        arch_tools = {}
        arch_tools['CC']       = conf.find_program([arch + '-gcc'], path_list = paths)
        arch_tools['CXX']      = conf.find_program([arch + '-g++'], path_list = paths)
        arch_tools['AS']       = conf.find_program([arch + '-gcc'], path_list = paths)
        arch_tools['LD']       = conf.find_program([arch + '-ld'],  path_list = paths)
        arch_tools['AR']       = conf.find_program([arch + '-ar'],  path_list = paths)
        arch_tools['LINK_CC']  = arch_tools['CC']
        arch_tools['LINK_CXX'] = arch_tools['CXX']
        arch_tools['AR']       = conf.find_program([arch + '-ar'], path_list = paths)
        arch_tools['LD']       = conf.find_program([arch + '-ld'], path_list = paths)
        arch_tools['NM']       = conf.find_program([arch + '-nm'], path_list = paths)
        arch_tools['OBJDUMP']  = conf.find_program([arch + '-objdump'], path_list = paths)
        arch_tools['OBJCOPY']  = conf.find_program([arch + '-objcopy'], path_list = paths)
        arch_tools['READELF']  = conf.find_program([arch + '-readelf'], path_list = paths)
        tools[arch] = arch_tools
    return tools

def _find_installed_archs(config, path, version):
    archs = []
    if config is None:
        for d in os.listdir(path):
            if d.endswith('-rtems' + version):
                archs += [d]
    else:
        a = subprocess.check_output([config, '--list-format', '"%(arch)s"'])
        a = a[:-1].replace('"', '')
        archs = a.split()
        archs = ['%s-rtems4.11' %(x) for x in archs]
    archs.sort()
    return archs

def _check_archs(config, req, path, version):
    installed = _find_all_archs(config, path, version)
    archs = []
    for a in req.split(','):
        arch = a + '-rtems' + version
        if arch in installed:
            archs += [arch]
    archs.sort()
    return archs

def _find_installed_arch_bsps(config, path, archs):
    arch_bsps = []
    if config is None:
        for f in os.listdir(_pkgconfig_path(path)):
            if f.endswith('.pc'):
                if _arch_from_arch_bsp(f[:-3]) in archs:
                    arch_bsps += [f[:-3]]
    else:
        ab = subprocess.check_output([config, '--list-format'])
        ab = ab[:-1].replace('"', '')
        ab = ab.replace('/', '-rtems4.11-')
        arch_bsps = ab.split()
    arch_bsps.sort()
    return arch_bsps

def _check_arch_bsps(req, path, archs):
    installed = _find_installed_bsps(path, archs)
    bsps = []
    for b in req.split(','):
        if b in installed:
            bsps += [b]
    bsps.sort()
    return bsps

def _arch_from_arch_bsp(arch_bsp):
    return '-'.join(arch_bsp.split('-')[:2])
    
def _bsp_from_arch_bsp(arch_bsp):
    return '-'.join(arch_bsp.split('-')[2:])

def _pkgconfig_path(path):
    return os.path.join(path, 'lib', 'pkgconfig')

def _load_flags(conf, arch_bsp, path):
    if not os.path.exists(path):
        ctx.fatal('RTEMS path not found.')
    if os.path.exists(_pkgconfig_path(path)):
        pc = os.path.join(_pkgconfig_path(path), arch_bsp + '.pc')
        conf.to_log('Opening and load pkgconfig: ' + pc)
        pkg = pkgconfig.package(pc)
        config = None
    elif os.path.exists(os.path.join(path, 'rtems-config')):
        config = os.path.join(path, 'rtems-config')
        pkg = None
    flags = {}
    _log_header(conf)
    flags['CFLAGS'] = _load_flags_set('CFLAGS', arch_bsp, conf, config, pkg)
    flags['LDFLAGS'] = _load_flags_set('LDFLAGS', arch_bsp, conf, config, pkg)
    flags['LIB'] = _load_flags_set('LIB', arch_bsp, conf, config, pkg)
    return flags

def _load_flags_set(flags, arch_bsp, conf, config, pkg):
    conf.to_log('%s ->' % flags)
    if pkg is not None:
        flagstr = ''
        try:
            flagstr = pkg.get(flags)
        except pkgconfig.error as e:
            conf.to_log('pkconfig warning: ' + e.msg)
        conf.to_log('  ' + flagstr)
    else:
        flags_map = { 'CFLAGS': '--cflags',
                      'LDFLAGS': '--ldflags',
                      'LIB': '--libs' }
        ab = arch_bsp.split('-')
        #conf.check_cfg(path = config, 
        #               package = '',
        #               uselib_store = 'rtems',
        #               args = '--bsp %s/%s %s' % (ab[0], ab[2], flags_map[flags]))
        #print conf.env
        #print '%r' % conf
        #flagstr = '-l -c'
        flagstr = subprocess.check_output([config, '--bsp', '%s/%s' % (ab[0], ab[2]), flags_map[flags]])
    return flagstr.split()

def _log_header(conf):
    conf.to_log('-----------------------------------------')
