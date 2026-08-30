# Yuga compiler — C11, libc only. Generated programs are gnu99 (C99 + statement exprs).

CC      := cc
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -O2 -g -MMD -MP
CFLAGS  += -DYUGA_RT_PATH=\"$(CURDIR)/runtime/yuga_rt.h\"
CFLAGS  += -DYUGA_RUNTIME_DIR=\"$(CURDIR)/runtime\"
CFLAGS  += -DYUGA_STD_DIR=\"$(CURDIR)/std\"
CFLAGS  += -DYUGA_ZEUS_DIR=\"$(CURDIR)/zeus\"

SRCDIR  := src
OBJDIR  := obj
BINDIR  := bin

ALL_C   := $(wildcard $(SRCDIR)/*.c) $(wildcard $(SRCDIR)/sema/*.c)
LIB_C   := $(filter-out $(SRCDIR)/driver.c $(SRCDIR)/lsp.c,$(ALL_C))
LIB_O   := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(LIB_C))
YUGAC_O := $(LIB_O) $(OBJDIR)/driver.o
LSP_O   := $(LIB_O) $(OBJDIR)/lsp.o

TARGET  := $(BINDIR)/yugac
LSP     := $(BINDIR)/yuga-lsp

PASS    := $(sort $(wildcard tests/compile_pass/*.yuga))
FAIL    := $(sort $(wildcard tests/compile_fail/*.yuga))
EXDIR   := tests/examples
EXBUILD := $(EXDIR)/build
ZEUSDIR := zeus
# An app's entry point is named after its directory; every other .yuga beside
# it is a module that app imports, not a program to link.
ZEUSAPPS := $(foreach d,$(wildcard $(ZEUSDIR)/apps/*),$(wildcard $(d)/$(notdir $(d)).yuga))
EXAMPLES:= $(sort $(filter-out $(EXDIR)/oob.yuga,$(wildcard $(EXDIR)/*.yuga) $(ZEUSAPPS)))

.PHONY: all clean test mkdirs lsp grammar

all: mkdirs $(TARGET) $(LSP)

lsp: mkdirs $(LSP)

grammar:
	cd tree-sitter-yuga && npx --yes tree-sitter-cli generate

mkdirs:
	@mkdir -p $(OBJDIR) $(OBJDIR)/sema $(BINDIR) tests/tmp $(EXBUILD)

$(TARGET): $(YUGAC_O)
	$(CC) $(CFLAGS) $(YUGAC_O) -o $@

$(LSP): $(LSP_O)
	$(CC) $(CFLAGS) $(LSP_O) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

-include $(LIB_O:.o=.d) $(OBJDIR)/driver.d $(OBJDIR)/lsp.d

clean:
	rm -rf $(OBJDIR) $(BINDIR) tests/tmp $(EXBUILD) runtime/.obj

test: all
	@mkdir -p tests/tmp $(EXBUILD)
	@export ZEUS_HEADLESS=1 MAYA_HEADLESS=1; \
	err=0; \
	for f in $(PASS); do \
	  stem=$$(basename $$f .yuga); \
	  if ! ./$(TARGET) $$f -o tests/tmp/$$stem >tests/tmp/$$stem.log 2>&1; then \
	    echo "FAIL compile $$f"; cat tests/tmp/$$stem.log; err=1; continue; \
	  fi; \
	  if ! tests/tmp/$$stem >/dev/null; then \
	    echo "FAIL run $$f"; err=1; continue; \
	  fi; \
	  echo "ok   $$f"; \
	done; \
	for f in $(FAIL); do \
	  stem=$$(basename $$f .yuga); \
	  if ./$(TARGET) $$f -o tests/tmp/$$stem >tests/tmp/$$stem.log 2>&1; then \
	    echo "FAIL should-reject $$f"; err=1; \
	  else \
	    echo "ok   $$f (rejected)"; \
	  fi; \
	done; \
	for f in $(EXAMPLES); do \
	  stem=$$(basename $$f .yuga); \
	  if ! ./$(TARGET) $$f -o $(EXBUILD)/$$stem >tests/tmp/ex_$$stem.log 2>&1; then \
	    echo "FAIL compile $$f"; cat tests/tmp/ex_$$stem.log; err=1; continue; \
	  fi; \
	  if ! $(EXBUILD)/$$stem >/dev/null; then \
	    echo "FAIL run $$f"; err=1; continue; \
	  fi; \
	  echo "ok   $$f"; \
	done; \
	if ! ./$(TARGET) $(EXDIR)/oob.yuga -o $(EXBUILD)/oob >tests/tmp/ex_oob.log 2>&1; then \
	  echo "FAIL compile $(EXDIR)/oob.yuga"; cat tests/tmp/ex_oob.log; err=1; \
	elif $(EXBUILD)/oob >tests/tmp/ex_oob.out 2>tests/tmp/ex_oob.err; then \
	  echo "FAIL should-trap $(EXDIR)/oob.yuga"; err=1; \
	elif ! grep -q "index out of bounds" tests/tmp/ex_oob.err; then \
	  echo "FAIL trap message $(EXDIR)/oob.yuga"; cat tests/tmp/ex_oob.err; err=1; \
	else \
	  echo "ok   $(EXDIR)/oob.yuga (trapped)"; \
	fi; \
	for f in $(PASS) $(EXAMPLES); do \
	  stem=$$(basename $$f .yuga); \
	  if ! ./$(TARGET) --emit-ir $$f -o tests/tmp/ir_$$stem.ir >tests/tmp/ir_$$stem.log 2>&1; then \
	    echo "FAIL ir $$f"; cat tests/tmp/ir_$$stem.log; err=1; continue; \
	  fi; \
	  if grep -q "PARTIAL" tests/tmp/ir_$$stem.ir; then \
	    echo "warn ir $$f (partial lowering)"; \
	  fi; \
	done; \
	echo "ok   ir lowering + verify"; \
	for g in tests/ir_golden/*.ir; do \
	  [ -e $$g ] || continue; \
	  stem=$$(basename $$g .ir); \
	  src=tests/compile_pass/$$stem.yuga; \
	  if [ ! -f $$src ]; then src=tests/examples/$$stem.yuga; fi; \
	  if [ ! -f $$src ]; then echo "FAIL ir golden missing source $$stem"; err=1; continue; fi; \
	  if ! ./$(TARGET) --emit-ir $$src -o tests/tmp/golden_$$stem.ir >tests/tmp/golden_$$stem.log 2>&1; then \
	    echo "FAIL ir golden emit $$stem"; cat tests/tmp/golden_$$stem.log; err=1; continue; \
	  fi; \
	  if ! diff -u $$g tests/tmp/golden_$$stem.ir >tests/tmp/golden_$$stem.diff; then \
	    echo "FAIL ir golden $$stem"; cat tests/tmp/golden_$$stem.diff; err=1; \
	  else \
	    echo "ok   ir golden $$stem"; \
	  fi; \
	done; \
	if ! python3 tests/lsp_smoke.py; then err=1; fi; \
	if [ $$err -ne 0 ]; then echo "TESTS FAILED"; exit 1; fi; \
	echo "ALL TESTS PASSED"
