CC      ?= cc
AR      ?= ar
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude
# Needed by the tree search only.
LDLIBS  += -lm

BUILD := build
LIB   := $(BUILD)/libhexreclaim.a
OBJS  := $(BUILD)/hexreclaim.o $(BUILD)/hexreclaim_mcts.o
PROGS := $(BUILD)/play $(BUILD)/mc $(BUILD)/test_hexreclaim $(BUILD)/test_sim \
         $(BUILD)/test_mcts

.PHONY: all test clean

all: $(LIB) $(PROGS)

$(BUILD):
	mkdir -p $@

$(BUILD)/hexreclaim.o: src/hexreclaim.c include/hexreclaim.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/hexreclaim_mcts.o: src/hexreclaim_mcts.c include/hexreclaim_mcts.h \
		include/hexreclaim.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

$(BUILD)/play: examples/play.c $(LIB) include/hexreclaim.h \
		include/hexreclaim_mcts.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(BUILD)/mc: examples/mc.c $(LIB) include/hexreclaim.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LIB) -o $@

$(BUILD)/test_hexreclaim: tests/test_hexreclaim.c $(LIB) include/hexreclaim.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LIB) -o $@

$(BUILD)/test_sim: tests/test_sim.c $(LIB) include/hexreclaim.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LIB) -o $@

$(BUILD)/test_mcts: tests/test_mcts.c $(LIB) include/hexreclaim.h \
		include/hexreclaim_mcts.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LIB) $(LDLIBS) -o $@

test: $(BUILD)/test_hexreclaim $(BUILD)/test_sim $(BUILD)/test_mcts
	./$(BUILD)/test_hexreclaim
	./$(BUILD)/test_sim
	./$(BUILD)/test_mcts

clean:
	rm -rf $(BUILD)
