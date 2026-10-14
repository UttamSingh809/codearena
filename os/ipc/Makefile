# ============================================================
# CodeArena IPC Queue — Makefile
# ============================================================

CC       ?= gcc
CPPFLAGS += -Iinclude -Isrc
CFLAGS   ?= -Wall -Wextra -Wpedantic -O2 -g -std=c11 -D_GNU_SOURCE
LDFLAGS  ?=
LDLIBS   ?= -pthread -lrt

SRCDIR  = src
INCDIR  = include
TESTDIR = tests

LIB_SRCS = \
    $(SRCDIR)/queue.c \
    $(SRCDIR)/queue_producer.c \
    $(SRCDIR)/queue_consumer.c
LIB_OBJS = $(LIB_SRCS:.c=.o)

TEST_BINS = \
    $(TESTDIR)/test_producer \
    $(TESTDIR)/test_consumer \
    $(TESTDIR)/test_stress

.PHONY: all clean rebuild test

all: $(LIB_OBJS) $(TEST_BINS)

# Individual rules so the Makefile can see header dependencies.
$(SRCDIR)/queue.o:          $(SRCDIR)/queue.c          $(INCDIR)/queue.h $(INCDIR)/queue_protocol.h
$(SRCDIR)/queue_producer.o: $(SRCDIR)/queue_producer.c $(INCDIR)/queue.h $(SRCDIR)/queue_internal.h
$(SRCDIR)/queue_consumer.o: $(SRCDIR)/queue_consumer.c $(INCDIR)/queue.h $(SRCDIR)/queue_internal.h

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(TESTDIR)/test_producer: $(TESTDIR)/test_producer.c $(LIB_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LIB_OBJS) $(LDFLAGS) $(LDLIBS)

$(TESTDIR)/test_consumer: $(TESTDIR)/test_consumer.c $(LIB_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LIB_OBJS) $(LDFLAGS) $(LDLIBS)

$(TESTDIR)/test_stress: $(TESTDIR)/test_stress.c $(LIB_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(LIB_OBJS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(LIB_OBJS) $(TEST_BINS)
	-rm -f /dev/shm/codearena_* 2>/dev/null || true

rebuild: clean all

test: all
	@echo "============================================================"
	@echo "  Test 1: single producer, single consumer, 100 jobs"
	@echo "============================================================"
	./$(TESTDIR)/test_producer /test_q1 8 100 > /dev/null &
	PRODUCER_PID=$$! ; \
	./$(TESTDIR)/test_consumer /test_q1 100 1 ; \
	wait $$PRODUCER_PID ; \
	rm -f /dev/shm/test_q1* /dev/shm/sem.test_q1*

	@echo ""
	@echo "============================================================"
	@echo "  Test 2: producer slower than consumer (blocks on empty)"
	@echo "============================================================"
	(./$(TESTDIR)/test_producer /test_q2 4 50 1000 &) ; \
	sleep 0.05 ; \
	./$(TESTDIR)/test_consumer /test_q2 50 1
	-rm -f /dev/shm/test_q2*

	@echo ""
	@echo "============================================================"
	@echo "  Test 3: 3 producers, 4 consumers, 10000 jobs"
	@echo "============================================================"
	./$(TESTDIR)/test_stress /test_q3 64 3 4 10000