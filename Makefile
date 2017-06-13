#-------------------------------------------------------------------------
#  Copyright 2016, Daan Leijen. 
#-------------------------------------------------------------------------

.PHONY : clean dist init tests staticlib main

include out/makefile.config

ifndef $(VARIANT)
VARIANT= debug
endif

CONFIGDIR  = out/$(CONFIG)
OUTDIR 		 = $(CONFIGDIR)/$(VARIANT)
INCLUDES   = -Iinc -I$(CONFIGDIR)

ifeq ($(VARIANT),release)
CCFLAGS    = $(CCFLAGSOPT) -DNDEBUG $(INCLUDES)
else
ifeq ($(VARIANT),testopt)
CCFLAGS    = $(CCFLAGSOPT) $(INCLUDES)
else
CCFLAGS    = $(CCFLAGSDEBUG) $(INCLUDES)
endif
endif

# -------------------------------------
# Sources
# -------------------------------------

SRCFILES = libhandler.c

TESTFILES= main-tests.c tests.c \
					 test-exn.c test-state.c test-amb.c test-dynamic.c test-raise.c test-general.c \
					 test-tailops.c 

BENCHFILES=main-perf.c perf.c tests.c test-state.c \
					 perf-counter.c


SRCS     = $(patsubst %,src/%,$(SRCFILES)) $(patsubst %,src/%,$(ASMFILES))
OBJS  	 = $(patsubst %.c,$(OUTDIR)/%$(OBJ), $(SRCFILES)) $(patsubst %$(ASM),$(OUTDIR)/%$(OBJ),$(ASMFILES))
HLIB     = $(OUTDIR)/libhandler$(LIB)

TESTSRCS = $(patsubst %,test/%,$(TESTFILES)) 
TESTMAIN = $(OUTDIR)/libh-tests$(EXE)

BENCHSRCS= $(patsubst %,test/%,$(BENCHFILES))
BENCHMAIN= $(OUTDIR)/libh-bench$(EXE)

# -------------------------------------
# Main targets
# -------------------------------------

main: init staticlib

tests: init staticlib testmain
	  @echo ""
		@echo "run tests"
		$(TESTMAIN)

bench: init staticlib benchmain
	  @echo ""
		@echo "run benchmark"
		$(BENCHMAIN)


# -------------------------------------
# build tests
# -------------------------------------

testmain: $(TESTMAIN)

$(TESTMAIN): $(TESTSRCS) $(HLIB)
	$(CC) $(CCFLAGS)  $(LINKFLAGOUT)$@ $(TESTSRCS) $(HLIB)

# -------------------------------------
# build benchmark
# -------------------------------------

benchmain: $(BENCHMAIN)

$(BENCHMAIN): $(BENCHSRCS) $(HLIB)
	$(CC) $(CCFLAGS)  $(LINKFLAGOUT)$@  $(BENCHSRCS) $(HLIB)


# -------------------------------------
# build the static library
# -------------------------------------

staticlib: $(HLIB)

$(HLIB): $(OBJS)
	$(AR) $(ARFLAGS)  $(ARFLAGOUT)$@ $(OBJS) 

$(OUTDIR)/%$(OBJ): src/%.c
	$(CC) $(CCFLAGS)  $(CCFLAGOUT)$@ -c $< 
	# -g -fverbose-asm -Wa,-aln=$@.s

$(OUTDIR)/%$(OBJ): src/%$(ASM)
	$(CC) $(ASMFLAGS)  $(ASMFLAGOUT)$@ -c $< 



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
	@if test -d "$(OUTDIR)/asm"; then :; else $(MKDIR) "$(OUTDIR)/asm"; fi

help:
	@echo "Usage: make <target>"
	@echo "Or   : make VARIANT=<variant> <target>"
	@echo ""
	@echo "Variants:"
	@echo "  debug       : Build a debug version (default)"
	@echo "  testopt     : Build an optimized version but with assertions"
	@echo "  release     : Build an optimized release version"
	@echo ""
	@echo "Targets:"
	@echo "  main        : Build a static library (default)"
	@echo "  tests       : Run tests"
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
depend:
	$(CCDEPEND) $(INCLUDES) src/*.c > $(CONFIGDIR)/temp.depend
	sed -e "s|\(.*\.o\)|$(CONFIGDIR)/\$$(VARIANT)/\1|g" $(CONFIGDIR)/temp.depend > $(CONFIGDIR)/makefile.depend
	$(CCDEPEND) $(INCLUDES) test/*.c > $(CONFIGDIR)/temp.depend
	sed -e "s|\(.*\.o\)|$(CONFIGDIR)/\$$(VARIANT)/\1|g" $(CONFIGDIR)/temp.depend >> $(CONFIGDIR)/makefile.depend
	$(RM) $(CONFIGDIR)/temp.depend

include $(CONFIGDIR)/makefile.depend