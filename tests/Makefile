# Host tests. The map lookup and the globe projection are plain arithmetic, so
# they can be checked with a native compiler in a second rather than by copying an
# NRO to an SD card. Run from the project root:
#
#     make -C tests

CC      ?= cc
CFLAGS  := -O2 -Wall -Wextra -I../include
LDLIBS  := -lm
SHARED  := ../source/world.c ../source/util.c

BINDIR  := build
TESTS   := $(BINDIR)/test_world $(BINDIR)/test_projection $(BINDIR)/test_version

.PHONY: all clean
all: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; (cd .. && ./tests/$$t) || exit 1; done

$(BINDIR)/test_world: test_world.c $(SHARED) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BINDIR)/test_projection: test_projection.c ../source/projection.c $(SHARED) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BINDIR)/test_version: test_version.c ../source/util.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BINDIR):
	@mkdir -p $@

clean:
	@rm -rf $(BINDIR)
