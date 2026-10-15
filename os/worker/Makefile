# ============================================================
# CodeArena Worker — Makefile
# ============================================================

CC        ?= gcc

# Query pg_config for PostgreSQL include and lib directories
PG_INCLUDEDIR ?= $(shell pg_config --includedir 2>/dev/null || echo /usr/include/postgresql)
PG_LIBDIR     ?= $(shell pg_config --libdir 2>/dev/null || echo /usr/lib)

CPPFLAGS  += -Iinclude -I../sandbox/include -I../ipc/include -I$(PG_INCLUDEDIR)
CFLAGS    ?= -Wall -Wextra -Wpedantic -O2 -g -std=c11 -D_GNU_SOURCE
LDFLAGS   ?= -L$(PG_LIBDIR)
LDLIBS    ?= -lpq -lseccomp -lpthread -lrt

SANDBOX_DIR = ../sandbox
IPC_DIR     = ../ipc

SANDBOX_LIB = $(SANDBOX_DIR)/src/sandbox.o \
              $(SANDBOX_DIR)/src/pipe_reader.o \
              $(SANDBOX_DIR)/src/cgroup.o \
              $(SANDBOX_DIR)/src/namespace.o \
              $(SANDBOX_DIR)/src/seccomp_filter.o \
              $(SANDBOX_DIR)/src/watchdog.o \
              $(SANDBOX_DIR)/src/verdict.o

IPC_LIB     = $(IPC_DIR)/src/queue.o \
              $(IPC_DIR)/src/queue_producer.o \
              $(IPC_DIR)/src/queue_consumer.o

WORKER_SRCS = src/worker.c src/db_client.c src/job_processor.c
WORKER_OBJS = $(WORKER_SRCS:.c=.o)

TARGET = codearena-worker

.PHONY: all clean rebuild sandbox ipc

all: sandbox ipc $(TARGET)

sandbox:
	$(MAKE) -C $(SANDBOX_DIR)

ipc:
	$(MAKE) -C $(IPC_DIR)

$(TARGET): src/main.c $(WORKER_OBJS) $(SANDBOX_LIB) $(IPC_LIB)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(WORKER_OBJS): %.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

clean:
	rm -f $(WORKER_OBJS) $(TARGET)
	$(MAKE) -C $(SANDBOX_DIR) clean
	$(MAKE) -C $(IPC_DIR) clean

rebuild: clean all