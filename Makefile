# tcomp build
#
#   make                 debug build with sanitizers   -> build/debug/tcomp
#   make BUILD=release   optimised build               -> build/release/tcomp
#   make test            unit tests + roundtrip tests (debug unless BUILD is set)
#   make CC=clang test   same, with clang
#   make corpus          download the Canterbury corpus, round-trip it, print ratios
#   make format         reformat all C files with clang-format
#   make format-check    fail if any file is not formatted (used by CI)
#   make clean

BUILD    ?= debug
BUILDDIR := build/$(BUILD)
OBJDIR   := $(BUILDDIR)/obj

CPPFLAGS := -Iinclude -D_POSIX_C_SOURCE=200809L -MMD -MP
CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -Werror \
            -Wshadow -Wstrict-prototypes -Wmissing-prototypes
LDFLAGS  :=

ifeq ($(BUILD),debug)
  SAN      := -fsanitize=address,undefined -fno-sanitize-recover=all
  CFLAGS   += -O0 -g3 -fno-omit-frame-pointer $(SAN)
  LDFLAGS  += $(SAN)
else ifeq ($(BUILD),release)
  CFLAGS   += -O2 -DNDEBUG
else
  $(error BUILD must be 'debug' or 'release', got '$(BUILD)')
endif

# Every .c in src/ except main.c is library code, linked into the CLI and the tests.
LIB_SRCS  := $(filter-out src/main.c,$(wildcard src/*.c))
LIB_OBJS  := $(LIB_SRCS:src/%.c=$(OBJDIR)/src/%.o)
LIB       := $(BUILDDIR)/libtcomp.a

CLI       := $(BUILDDIR)/tcomp
CLI_OBJ   := $(OBJDIR)/src/main.o

TEST_SRCS := $(wildcard tests/unit/*.c)
TEST_OBJS := $(TEST_SRCS:tests/unit/%.c=$(OBJDIR)/tests/%.o)
TEST_BIN  := $(BUILDDIR)/unit_tests

FORMAT_FILES := $(wildcard src/*.c include/tcomp/*.h tests/unit/*.c tests/unit/*.h)

.PHONY: all test unit roundtrip corpus clean format format-check

all: $(CLI)

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(CLI): $(CLI_OBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^

# Tests link libm for log2() in the entropy-bound test; the library itself needs no libm.
$(TEST_BIN): $(TEST_OBJS) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

$(OBJDIR)/src/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/tests/%.o: tests/unit/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -Itests/unit $(CFLAGS) -c -o $@ $<

test: unit roundtrip

unit: $(TEST_BIN)
	./$(TEST_BIN)

roundtrip: $(CLI)
	./tests/roundtrip.sh $(CLI) $(BUILDDIR)/testdata

corpus: $(CLI)
	./bench/fetch-corpus.sh
	./tests/corpus.sh $(CLI) $(BUILDDIR)/corpus

clean:
	rm -rf build

format:
	clang-format -i $(FORMAT_FILES)

format-check:
	clang-format --dry-run --Werror $(FORMAT_FILES)

-include $(LIB_OBJS:.o=.d) $(CLI_OBJ:.o=.d) $(TEST_OBJS:.o=.d)
