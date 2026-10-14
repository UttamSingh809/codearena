# ============================================================
# CodeArena Sandbox — Makefile
# ============================================================

CC       ?= gcc
CPPFLAGS += -Iinclude
CFLAGS   ?= -Wall -Wextra -Wpedantic -O2 -g -std=c11 -D_GNU_SOURCE
LDFLAGS  ?=
LDLIBS   ?= -lseccomp

SRCDIR  = src
INCDIR  = include
TESTDIR = tests
TARGET  = sandbox

LIB_SRCS = \
    $(SRCDIR)/sandbox.c \
    $(SRCDIR)/pipe_reader.c \
    $(SRCDIR)/cgroup.c \
    $(SRCDIR)/namespace.c \
    $(SRCDIR)/seccomp_filter.c
LIB_OBJS = $(LIB_SRCS:.c=.o)

TEST_BINS = \
    $(TESTDIR)/hello \
    $(TESTDIR)/stderr_test \
    $(TESTDIR)/exit_test \
    $(TESTDIR)/flood_test \
    $(TESTDIR)/cpu_hog \
    $(TESTDIR)/file_filler \
    $(TESTDIR)/fork_bomb \
    $(TESTDIR)/memory_bomb \
    $(TESTDIR)/file_reader \
    $(TESTDIR)/proc_scanner \
    $(TESTDIR)/network_probe \
    $(TESTDIR)/show_ns \
    $(TESTDIR)/syscall_fuzzer

.PHONY: all clean rebuild test rootfs

all: $(TARGET) $(TEST_BINS)

$(TARGET): $(SRCDIR)/main.c $(LIB_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# Explicit rule for each .o. No header prerequisites, so a missing
# header can never silently fall through to the built-in rule.
$(SRCDIR)/sandbox.o:        $(SRCDIR)/sandbox.c        $(INCDIR)/sandbox.h $(INCDIR)/cgroup.h $(INCDIR)/namespace.h $(INCDIR)/seccomp_filter.h
$(SRCDIR)/pipe_reader.o:    $(SRCDIR)/pipe_reader.c    $(INCDIR)/sandbox.h
$(SRCDIR)/cgroup.o:         $(SRCDIR)/cgroup.c         $(INCDIR)/cgroup.h
$(SRCDIR)/namespace.o:      $(SRCDIR)/namespace.c      $(INCDIR)/namespace.h
$(SRCDIR)/seccomp_filter.o: $(SRCDIR)/seccomp_filter.c $(INCDIR)/seccomp_filter.h

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(TESTDIR)/%: $(TESTDIR)/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $<

rootfs:
	mkdir -p rootfs/{bin,lib,lib64,tmp,etc}
	chmod 1777 rootfs/tmp

clean:
	rm -f $(TARGET) $(LIB_OBJS) $(TEST_BINS)
	-rmdir /sys/fs/cgroup/codearena/sandbox_* 2>/dev/null || true

rebuild: clean all

test: all rootfs
	@echo "============================================================"
	@echo "  Regression: hello with stdin"
	@echo "============================================================"
	@echo "codearena" | ./$(TARGET) --binary ./$(TESTDIR)/hello

	@echo ""
	@echo "============================================================"
	@echo "  Step 6: seccomp enabled — hello should still succeed"
	@echo "============================================================"
	./$(TARGET) --binary ./$(TESTDIR)/hello --input "codearena" \
	    --no-cgroup --seccomp

	@echo ""
	@echo "============================================================"
	@echo "  Step 6: syscall_fuzzer under seccomp — expect SIGSYS"
	@echo "============================================================"
	./$(TARGET) --binary ./$(TESTDIR)/syscall_fuzzer --no-cgroup --seccomp || true

	@echo ""
	@echo "============================================================"
	@echo "  Step 6: syscall_fuzzer without seccomp — expect survival"
	@echo "============================================================"
	./$(TARGET) --binary ./$(TESTDIR)/syscall_fuzzer --no-cgroup || true