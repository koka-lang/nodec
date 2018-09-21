#-------------------------------------------------------------------------
#  Copyright 2016, Daan Leijen.
#-------------------------------------------------------------------------

.PHONY : clean dist init tests staticlib main

include out/makefile.config

ifndef $(VARIANT)
VARIANT=debug
endif

# NodeC library
MAIN       = nodec
CONFIGDIR  = out/$(CONFIG)
OUTDIR 		 = $(CONFIGDIR)/$(MAIN)/$(VARIANT)
INCLUDES   = -Iinc -I$(CONFIGDIR) -Ideps -Ideps/libuv/include

# nodecx library: The ..X variants are for clients
# nodecx includes all needed static libraries (libuv,libz etc) and uses
# a single include path. All under "out/nodecx/$(VARIANT)"
MAINX      = $(MAIN)x
OUTDIRX    = $(CONFIGDIR)/$(MAINX)/$(VARIANT)
INCLUDESX  = -I$(OUTDIRX)/include

ifeq ($(VARIANT),release)
CCFLAGS    = $(CCFLAGSOPT) -DNDEBUG $(INCLUDES)
CCFLAGSX   = $(CCFLAGSOPT) -DNDEBUG $(INCLUDESX)
else ifeq ($(VARIANT),testopt)
CCFLAGS    = $(CCFLAGSOPT) $(INCLUDES)
CCFLAGSX   = $(CCFLAGSOPT) $(INCLUDESX)
else ifeq ($(VARIANT),debug)
CCFLAGS    = $(CCFLAGSDEBUG) $(INCLUDES)
CCFLAGSX   = $(CCFLAGSDEBUG) $(INCLUDESX)
else
VARIANTUNKNOWN=1
endif

# Use VALGRIND=1 to memory check under valgrind
ifeq ($(VALGRIND),1)
VALGRINDX=yes
else ifeq ($(VALGRIND),yes)
VALGRINDX=yes
else
VALGRINDX=
endif

ifeq ($(VALGRINDX),yes)
VALGRINDX=valgrind --leak-check=full --show-leak-kinds=all --suppressions=./valgrind.supp
endif

# Uncomment to generate assembly for nodec
# SHOWASM    = -Wa,-aln=$@.s

# -------------------------------------
# Sources
# -------------------------------------

SRCFILES = async.c interleave.c channel.c memory.c \
					 dns.c fs.c stream.c tcp.c timer.c tty.c log.c \
           http.c http_request.c http_static.c http_url.c  mime.c\
					 https.c tls-mbedtls.c

CEXAMPLES= main.c \
           ex-http-connect.c ex-http-server-static.c \
           ex-https-connect.c ex-https-server-static.c \
					 ex-fs-search.c

TESTFILES= main.c

BENCHFILES=


SRCS     = $(patsubst %,src/%,$(SRCFILES)) $(patsubst %,src/%,$(ASMFILES)) deps/http-parser/http_parser.c
OBJS  	 = $(patsubst %.c,$(OUTDIR)/%$(OBJ), $(SRCFILES)) $(patsubst %$(ASM),$(OUTDIR)/%$(OBJ),$(ASMFILES)) $(OUTDIR)/http_parser$(OBJ)
NLIB     = $(OUTDIR)/lib$(MAIN)$(LIB)
NLIBX    = $(OUTDIRX)/lib/lib$(MAINX)$(LIB)

# link statically
LIBS     =  $(NLIBX)

DEPSLIBS = deps/libhandler/out/$(CONFIG)/$(VARIANT)/libhandler.a deps/libuv/out/lib/libuv.a deps/zlib/out/lib/libz.a \
					 deps/mbedtls/library/libmbedcrypto.a deps/mbedtls/library/libmbedx509.a deps/mbedtls/library/libmbedtls.a

# for libuv
EXTRA-LIBS= -lrt -lpthread -lnsl -ldl

TESTSRCS = $(patsubst %,test/%,$(TESTFILES))
TESTMAIN = $(OUTDIR)/nodec-tests$(EXE)

BENCHSRCS= $(patsubst %,test/%,$(BENCHFILES))
BENCHMAIN= $(OUTDIR)/nodec-bench$(EXE)

EXAMPLEOUT = $(CONFIGDIR)/nodec-examples/$(VARIANT)
EXAMPLESRCS= $(patsubst %,examples/%,$(CEXAMPLES))
EXAMPLEMAIN= $(EXAMPLEOUT)/nodec-examples$(EXE)

# -------------------------------------
# Main targets
# -------------------------------------

main: init staticlib

examples: init staticlib examplemain
		@echo ""
		@echo "run example"
		$(EXAMPLEMAIN)


tests: init staticlib testmain
	@echo ""
	@echo "run tests"
	$(VALGRINDX) $(TESTMAIN)

bench: init staticlib benchmain
	@echo ""
	@echo "run benchmark"
	$(BENCHMAIN)


# -------------------------------------
# build example
# -------------------------------------

examplemain: $(EXAMPLEMAIN)

$(EXAMPLEMAIN): $(EXAMPLESRCS) $(NLIBX)
	$(CC) $(CCFLAGSX)  $(LINKFLAGOUT)$@ $(EXAMPLESRCS) $(LIBS) $(EXTRA-LIBS)


# -------------------------------------
# build tests
# -------------------------------------

testmain: $(TESTMAIN)

$(TESTMAIN): $(TESTSRCS) $(NLIBX)
	$(CC) $(CCFLAGSX)  $(LINKFLAGOUT)$@ $(TESTSRCS) $(LIBS) $(EXTRA-LIBS)




# -------------------------------------
# build benchmark
# -------------------------------------

benchmain: $(BENCHMAIN)

$(BENCHMAIN): $(BENCHSRCS) $(NLIB)
	$(CC) $(CCFLAGSX) $(LINKFLAGOUT)$@  $(BENCHSRCS) $(LIBS) -lm $(EXTRA-LIBS)



# -------------------------------------
# build the static library
# -------------------------------------

staticlib: init $(NLIBX)

$(NLIBX): $(NLIB) $(DEPSLIBS)
	@if test -d "$(OUTDIRX)/lib"; then :; else $(MKDIR) "$(OUTDIRX)/lib"; fi
	./libmerge.sh $(OUTDIRX)/lib lib$(MAINX).a $^
	@if test -d "$(OUTDIRX)/include/libhandler/inc"; then :; else $(MKDIR) "$(OUTDIRX)/include/libhandler/inc"; fi
	@if test -d "$(OUTDIRX)/include/libuv/include"; then :; else $(MKDIR) "$(OUTDIRX)/include/libuv/include"; fi
	@if test -d "$(OUTDIRX)/include/uv"; then :; else $(MKDIR) "$(OUTDIRX)/include/uv"; fi
	@if test -d "$(OUTDIRX)/include/http-parser"; then :; else $(MKDIR) "$(OUTDIRX)/include/http-parser"; fi
	$(CP) -f deps/libhandler/inc/libhandler.h    $(OUTDIRX)/include/libhandler/inc
	$(CP) -f deps/http-parser/http_parser.h $(OUTDIRX)/include/http-parser
	$(CP) -f deps/libuv/out/include/uv.h $(OUTDIRX)/include/libuv/include/uv.h
	$(CP) -fa deps/libuv/out/include/uv/* $(OUTDIRX)/include/uv
	$(CP) -f inc/nodec.h $(OUTDIRX)/include
	$(CP) -f inc/nodec-primitive.h $(OUTDIRX)/include

$(NLIB): $(OBJS)
	$(AR) $(ARFLAGS)  $(ARFLAGOUT)$@ $(OBJS)

$(OUTDIR)/%$(OBJ): src/%.c
	$(CC) $(CCFLAGS) $(CCFLAG99) $(CCFLAGOUT)$@ -c $< $(SHOWASM)

$(OUTDIR)/%$(OBJ): src/%$(ASM)
	$(CC) $(ASMFLAGS)  $(ASMFLAGOUT)$@ -c $<

$(OUTDIR)/http_parser$(OBJ): deps/http-parser/http_parser.c
	$(CC) $(CCFLAGS) $(CCFLAG99) $(CCFLAGOUT)$@ -c $< $(SHOWASM)


$(EXAMPLEOUT)/%$(OBJ): examples/%.c
		$(CC) $(CCFLAGS) $(CCFLAG99) $(CCFLAGOUT)$@ -c $< $(SHOWASM)


# -------------------------------------
# other targets
# -------------------------------------

docs:

clean:
	rm -rf $(CONFIGDIR)/*/*
	touch $(CONFIGDIR)/makefile.depend

init:
	@echo "use 'make help' for help"
	@echo "build variant: $(VARIANT), configuration: $(CONFIG)"
	@if test "$(VARIANTUNKNOWN)" = "1"; then echo ""; echo "Error: unknown build variant: $(VARIANT)"; echo "Use one of 'debug', 'release', or 'testopt'"; false; fi
	@if test -d "$(OUTDIR)/asm"; then :; else $(MKDIR) "$(OUTDIR)/asm"; fi
	@if test -d "$(OUTDIRX)"; then :; else $(MKDIR) "$(OUTDIRX)"; $(MKDIR) "$(OUTDIRX)/lib"; $(MKDIR) "$(OUTDIRX)/include"; fi
	@if test -d "$(EXAMPLEOUT)"; then :; else $(MKDIR) "$(EXAMPLEOUT)"; fi


help:
	@echo "Usage: make <target>"
	@echo "Or   : make VARIANT=<variant> <target>"
	@echo "Or   : make VALGRIND=1 tests"
	@echo ""
	@echo "Variants:"
	@echo "  debug       : Build a debug version (default)"
	@echo "  testopt     : Build an optimized version but with assertions"
	@echo "  release     : Build an optimized release version"
	@echo ""
	@echo "Targets:"
	@echo "  main        : Build a static library (default)"
	@echo "  tests       : Run tests"
	@echo "  mainxx      : Build a static library for C++"
	@echo "  testsxx     : Run tests for C++"
	@echo "  bench       : Run benchmarks, use 'VARIANT=release'"
	@echo "  clean       : Clean output directory"
	@echo "  depend      : Generate dependencies"
	@echo ""
	@echo "Configuration:"
	@echo "  output dir  : $(OUTDIR)"
	@echo "  c-compiler  : $(CC) $(CCFLAGS)"
	@echo ""

# dependencies
# [gcc -MM] generates the dependencies without the full
# directory name, ie.
#  evaluator.o: ...
# instead of
#  core/evaluator.o: ..
# we therefore use [sed] to append the directory name
depend: init
	$(CCDEPEND) $(INCLUDES) src/*.c > $(CONFIGDIR)/temp.depend
	sed -e "s|\(.*\.o\)|$(CONFIGDIR)/\$$(VARIANT)/\1|g" $(CONFIGDIR)/temp.depend > $(CONFIGDIR)/makefile.depend
	$(CCDEPEND) $(INCLUDES) test/*.c > $(CONFIGDIR)/temp.depend
	sed -e "s|\(.*\.o\)|$(CONFIGDIR)/\$$(VARIANT)/\1|g" $(CONFIGDIR)/temp.depend >> $(CONFIGDIR)/makefile.depend
	$(RM) $(CONFIGDIR)/temp.depend

include $(CONFIGDIR)/makefile.depend
