blddir = 'build'

def set_options(opt):
  opt.tool_options('compiler_cxx')

def configure(conf):
  conf.check_tool('compiler_cxx')
  conf.check_tool('node_addon')

def build(bld):
  can = bld.new_task_gen('cxx', 'shlib', 'node_addon')
  can.target = 'can'
  can.source = 'src/raw.cc'

  sig = bld.new_task_gen('cxx', 'shlib', 'node_addon')
  sig.target = 'can_signals'
  sig.source = 'src/signals.cc'
