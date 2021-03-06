MAKE_ROOT:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
srcExt = c
objDir = $(MAKE_ROOT)/obj
libDir = $(MAKE_ROOT)/lib
dpdkDir = $(libDir)/dpdk_backend
thriftDir = $(MAKE_ROOT)/thrift

libname = libbluebridge

# add the raw socket flag to differentiate from other builds
CFLAGS += -DDEFAULT
# a list of c files we do not want to compile
filter = *$(dpdkDir)*
sources := $(shell find "$(libDir)" -name '*.$(srcExt)' ! -path "$(filter)" )
srcDirs := $(shell find . -name '*.$(srcExt)' -exec dirname {} \; | uniq)
objects := $(patsubst $(MAKE_ROOT)/%.$(srcExt), $(objDir)/%.o, $(sources))

all: libmake
	@-rm -rf $(objDir)

libmake: buildrepo $(objects)
	@mkdir -p `dirname $@`
	@ar rcs $(libname).a $(objects)
	@ranlib $(libname).a

$(objDir)/%.o: %.$(srcExt)
	@echo "Building $@ ... "
	@$(CC) $(CFLAGS) $(MAKE_ROOT)/$< -o $@

clean:
	@echo "Cleaning..."
	@-rm -rf $(objDir)
	@-rm -rf $(libname).a

buildrepo:
	@$(call make-repo)

define make-repo
   for dir in $(srcDirs);\
   do\
	mkdir -p $(objDir)/$$dir;\
   done
endef