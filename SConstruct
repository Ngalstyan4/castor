
opts = Variables('Local.sc')
opts.AddVariables(
    ("CC", "C Compiler"),
    ("CXX", "C++ Compiler"),
    ("AS", "Assembler"),
    ("LINK", "Linker"),
    EnumVariable("RR", "R/R Type", "ctr", ["ctr", "tsx"])
)

objlib = Builder(action = 'ld -r -o $TARGET $SOURCES',
                 suffix = '.o',
                 src_suffix = '.c',
                 src_builder = 'StaticObject')

env = Environment(options = opts, BUILDERS = {'ObjectLibrary' : objlib})
env.Append(CXXFLAGS = "-std=c++11")

Export('env')

SConscript("librr/SConstruct", variant_dir="build/librr")
SConscript("libsnap/SConstruct", variant_dir="build/libsnap")
SConscript("rrlog/SConstruct", variant_dir="build/rrlog")

AlwaysBuild(Alias('test', "build/librr/librr.o", "test/testbench.py"))

