# Host tests. The map lookup and the globe projection are plain arithmetic, so
# they can be checked with a native compiler in a second rather than by copying an
# NRO to an SD card. Run from the project root:
#
#     make -C tests

CC      ?= cc
CFLAGS  := -O2 -Wall -Wextra -I../include
LDLIBS  := -lm

JSONC_CFLAGS := $(shell pkg-config --cflags json-c 2>/dev/null)
JSONC_LIBS   := $(shell pkg-config --libs json-c 2>/dev/null)
SHARED  := ../source/world.c ../source/util.c

BINDIR  := build
TESTS   := $(BINDIR)/test_world $(BINDIR)/test_projection $(BINDIR)/test_version \
           $(BINDIR)/test_sanitize $(BINDIR)/test_shelf

.PHONY: all clean
all: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; (cd .. && ./tests/$$t) || exit 1; done

$(BINDIR)/test_world: test_world.c $(SHARED) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BINDIR)/test_projection: test_projection.c ../source/projection.c $(SHARED) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BINDIR)/test_version: test_version.c ../source/util.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BINDIR)/test_sanitize: test_sanitize.c ../source/util.c | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# The shelf writes JSON, so this one needs the real library and a scratch
# directory standing in for the card.
$(BINDIR)/test_shelf: test_shelf.c ../source/shelf.c ../source/media_item.c ../source/util.c | $(BINDIR)
	$(CC) $(CFLAGS) -DSHELF_DIR='"/tmp/nxmedia-shelf-test"' $(JSONC_CFLAGS) \
		-o $@ $^ $(LDLIBS) $(JSONC_LIBS)

$(BINDIR):
	@mkdir -p $@

clean:
	@rm -rf $(BINDIR)
