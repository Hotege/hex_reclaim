CC      ?= cc
AR      ?= ar
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude

BUILD := build
LIB   := $(BUILD)/libhexreclaim.a

.PHONY: all test clean

all: $(LIB) $(BUILD)/play $(BUILD)/test_hexreclaim

$(BUILD):
	mkdir -p $@

$(BUILD)/hexreclaim.o: src/hexreclaim.c include/hexreclaim.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(BUILD)/hexreclaim.o
	$(AR) rcs $@ $^

$(BUILD)/play: examples/play.c $(LIB) include/hexreclaim.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LIB) -o $@

$(BUILD)/test_hexreclaim: tests/test_hexreclaim.c $(LIB) include/hexreclaim.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LIB) -o $@

test: $(BUILD)/test_hexreclaim
	./$(BUILD)/test_hexreclaim

clean:
	rm -rf $(BUILD)
