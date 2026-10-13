# ============================================================
# CodeArena Sandbox — Makefile
# ============================================================

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Wpedantic -O2 -g -std=c11 -D_GNU_SOURCE
LDFLAGS ?=

SRCDIR  = src
INCDIR  = include
TESTDIR = tests
TARGET  = sandbox

LIB_SRCS = $(SRCDIR)/sandbox.c $(SRCDIR)/pipe_reader.c
LIB_OBJS = $(LIB_SRCS:.c=.o)

TEST_BINS = \
    $(TESTDIR)/hello \
    $(TESTDIR)/stderr_test \
    $(TESTDIR)/exit_test \
    $(TESTDIR)/flood_test \
    $(TESTDIR)/cpu_hog \
    $(TESTDIR)/file_filler \
    $(TESTDIR)/fork_bomb

.PHONY: all clean rebuild test

all: $(TARGET) $(TEST_BINS)

$(TARGET): $(SRCDIR)/main.c $(LIB_OBJS)
	$(CC) $(CFLAGS) -I$(INCDIR) -o $@ $^ $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/sandbox.h
	$(CC) $(CFLAGS) -I$(INCDIR) -c -o $@ $<

$(TESTDIR)/%: $(TESTDIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET) $(LIB_OBJS) $(TEST_BINS)

rebuild: clean all

test: all
	@echo "============================================================"
	@echo "  Regression: hello with stdin"
	@echo "============================================================"
	@echo "codearena" | ./$(TARGET) --binary ./$(TESTDIR)/hello

	@echo ""
	@echo "============================================================"
	@echo "  Regression: stderr + stdout in parallel"
	@echo "============================================================"
	./$(TARGET) --binary ./$(TESTDIR)/stderr_test

	@echo ""
	@echo "============================================================"
	@echo "  Regression: output flooding"
	@echo "============================================================"
	./$(TARGET) --binary ./$(TESTDIR)/flood_test --output-limit 512

	@echo ""
	@echo "============================================================"
	@echo "  Step 3: CPU limit (expect SIGXCPU / verdict=TIME LIMIT)"
	@echo "============================================================"
	./$(TARGET) --binary ./$(TESTDIR)/cpu_hog --cpu-seconds 2 --output-limit 512 || true

	@echo ""
	@echo "============================================================"
	@echo "  Step 3: File size limit (expect SIGXFSZ / verdict=FILE SIZE LIMIT)"
	@echo "============================================================"
	rm -f /tmp/codearena_file_filler_test.dat
	./$(TARGET) --binary ./$(TESTDIR)/file_filler --max-file-size 1048576 || true
	@ls -l /tmp/codearena_file_filler_test.dat 2>/dev/null || true
	@rm -f /tmp/codearena_file_filler_test.dat

	@echo ""
	@echo "============================================================"
	@echo "  Step 3: Fork bomb (only effective when sandbox is non-root)"
	@echo "  NOTE: skipped if running as root — RLIMIT_NPROC has no"
	@echo "  effect on root. Full containment arrives in step 4 cgroups."
	@echo "============================================================"
	@if [ "$$(id -u)" = "0" ]; then \
	    echo "  [SKIP] running as root — fork_bomb refuses to run"; \
	else \
	    ./$(TARGET) --binary ./$(TESTDIR)/fork_bomb --max-pids 32 --output-limit 4096 || true; \
	fi