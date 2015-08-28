
CFLAGS += -Wall
CFLAGS += -Wextra
CFLAGS += -Werror
#CFLAGS += -Wpedantic
CFLAGS += -pedantic
CFLAGS += -Wmissing-declarations

ifdef DEBUG
CFLAGS += -g
else
CFLAGS += -O3
endif

LDLIBS += $(shell pkg-config --libs   libbsd-overlay)
CFLAGS += $(shell pkg-config --cflags libbsd-overlay)

LDLIBS += $(shell pkg-config --libs   lldpctl)
CFLAGS += $(shell pkg-config --cflags lldpctl)

LDLIBS += $(shell pkg-config --libs   libnl-3.0)
CFLAGS += $(shell pkg-config --cflags libnl-3.0)

LDLIBS += $(shell pkg-config --libs   libnl-genl-3.0)
CFLAGS += $(shell pkg-config --cflags libnl-genl-3.0)

LDLIBS += $(shell pkg-config --libs   libnl-route-3.0)
CFLAGS += $(shell pkg-config --cflags libnl-route-3.0)

SRC-y += src/netlink.c
SRC-y += src/lldp.c
SRC-y += src/leaf.c

EXECUTABLE = src/leaf

ifeq      (0,$(shell which clang-format-3.7 >/dev/null 2>/dev/null; echo $$?))
	CLANG_FORMAT=clang-format-3.7
else ifeq (0,$(shell which clang-format-3.6 >/dev/null 2>/dev/null; echo $$?))
	CLANG_FORMAT=clang-format-3.6
else ifeq (0,$(shell which clang-format >/dev/null 2>/dev/null; echo $$?))
	CLANG_FORMAT=clang-format
else
	CLANG_FORMAT=$(error No clang-format found, please ensure it is available in your $$PATH)
endif

.PHONY: all
all: $(EXECUTABLE)

SRC-o = $(subst .c,.o,$(SRC-y))

$(EXECUTABLE): $(SRC-o)
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@

.PHONY: test
test: $(EXECUTABLE)
	docker build -t leaf .
	docker run --privileged --rm -v $(shell pwd):/opt/leaf leaf /opt/leaf/test/test.sh

.PHONY: clean
clean:
	rm -f $(SRC-o)
	rm -f $(EXECUTABLE)
	rm -f test/envs/*.stamp

.PHONY: format
format:
	$(CLANG_FORMAT) -i */*.h */*.c



TEST_ENV_STAMPS=$(shell find test/envs/ -type f -name '*.dockerfile' | sed s/dockerfile/stamp/)
.PHONY: test-envs
test-envs: $(TEST_ENV_STAMPS)

test/envs/%.stamp: test/envs/%.dockerfile
	docker build -t leaf-$(shell basename $< .dockerfile) -f $< $(CURDIR)
	touch $@
