
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

LDLIBS += $(shell pkg-config --libs   lldpctl)
CFLAGS += $(shell pkg-config --cflags lldpctl)

LDLIBS += $(shell pkg-config --libs   libnl-3.0)
CFLAGS += $(shell pkg-config --cflags libnl-3.0)

LDLIBS += $(shell pkg-config --libs   libnl-genl-3.0)
CFLAGS += $(shell pkg-config --cflags libnl-genl-3.0)

LDLIBS += $(shell pkg-config --libs   libnl-route-3.0)
CFLAGS += $(shell pkg-config --cflags libnl-route-3.0)

LDLIBS += $(shell pkg-config --libs   pthread-stubs)
LDLIBS += -pthread
CFLAGS += $(shell pkg-config --cflags pthread-stubs)

SRC-y += src/netlink.c
SRC-y += src/lldp.c
SRC-y += src/leaf.c

EXECUTABLE = src/leaf

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
	clang-format-3.7 -i */*.h */*.c



TEST_ENV_STAMPS=$(shell find test/envs/ -type f -name '*.dockerfile' | sed s/dockerfile/stamp/)
.PHONY: test-envs
test-envs: $(TEST_ENV_STAMPS)

test/envs/%.stamp: test/envs/%.dockerfile
	docker build -t leaf-$(shell basename $< .dockerfile) -f $< $(CURDIR)
	touch $@
