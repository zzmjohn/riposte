# This Makefile requires GNU make.
 
UNAME := $(shell uname -s)
CXXFLAGS := -Wall
CFLAGS := -Wall
LFLAGS := -fpic
LIBS := -lpthread

ifeq ($(UNAME),Linux)
	#for clock_gettime
	LIBS += -lrt
endif

EPEE=1

SRC := main.cpp type.cpp strings.cpp bc.cpp value.cpp output.cpp interpreter.cpp compiler.cpp internal.cpp runtime.cpp coerce.cpp library.cpp format.cpp gc.cpp call.cpp

SRC += parser/lexer.cpp

ifeq ($(EPEE),1)
	CXXFLAGS += -DEPEE
	SRC += epee/ir.cpp epee/trace.cpp epee/trace_compile.cpp epee/assembler-x64.cpp
endif

EXECUTABLE := riposte
LINENOISE := build/linenoise.o

OBJECTS := $(patsubst %.cpp,build/%.o,$(SRC))
ASM := $(patsubst %.cpp,build/%.s,$(SRC))
DEPENDENCIES := $(patsubst %.cpp,build/%.d,$(SRC))

default: debug

debug: CXXFLAGS += -DDEBUG -O0 -g
debug: $(EXECUTABLE)

release: CXXFLAGS += -DNDEBUG -O4 -g
release: $(EXECUTABLE)

asm: CXXFLAGS += -DNDEBUG -O3 -g 
asm: $(ASM)

$(EXECUTABLE): $(OBJECTS) $(LINENOISE)
	$(CXX) $(LFLAGS) -o $@ $^ $(LIBS)

build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@ 

build/%.s: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -S -c $< -o $@ 

$(LINENOISE): libs/linenoise/linenoise.c
	 $(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(EXECUTABLE) $(OBJECTS) $(LINENOISE) $(DEPENDENCIES)

# dependency rules
build/%.d: src/%.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -MM -MT '$@ $(@:.d=.o)' $< -o $@
	
-include $(DEPENDENCIES)


# tests
COVERAGE_TESTS = $(shell find tests/coverage -type f -name '*.R')
BLACKBOX_TESTS = $(shell find tests/blackbox -type f -name '*.R')

.PHONY: tests $(COVERAGE_TESTS) $(BLACKBOX_TESTS)
COVERAGE_FLAGS := 
tests: COVERAGE_FLAGS += >/dev/null
tests: $(COVERAGE_TESTS) $(BLACKBOX_TESTS)

$(COVERAGE_TESTS):
	-@Rscript --vanilla --default-packages=NULL $@ > $@.key 2>/dev/null
	-@./riposte --format=R -f $@ > $@.out
	-@diff -b $@.key $@.out $(COVERAGE_FLAGS)
	-@rm $@.key $@.out

$(BLACKBOX_TESTS):
	-@Rscript --vanilla --default-packages=NULL $@ > $@.key 2>/dev/null
	-@./riposte --format=R -f $@ > $@.out
	-@diff -b $@.key $@.out $(COVERAGE_FLAGS)
	-@rm $@.key $@.out

