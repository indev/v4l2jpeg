import os

def set_options(opt):
  opt.tool_options("compiler_cxx")
  opt.tool_options("compiler_cc")

def configure(conf):
  conf.check_tool('compiler_cxx')
  conf.check_tool("compiler_cc")	
  conf.check_tool('node_addon')
  conf.env.append_unique('CPPFLAGS', ["-D_FILE_OFFSET_BITS=64", "-D_LARGEFILE_SOURCE"])
  conf.env.append_unique('CXXFLAGS', ["-Wall", "-Werror"])

def build(bld):
  obj = bld.new_task_gen("cc", "shlib", "cxx", "node_addon")
  obj.target = "v4l2jpeg"
  obj.defines = ['OUTPUT_INTERNAL=1']
  obj.source = ["node_main.cc", "main.C", "mjpegtojpeg.C"]

def clean(ctx):
  os.popen('rm -rf .lock-wscript ./build v4l2jpeg.node')
