SUBDIRS = src
dist_doc_DATA = README.md

.PHONY: test
test: all
	docker build -t leaf .
	docker run --privileged --rm -v $(shell pwd):/opt/leaf leaf /opt/leaf/test/test.sh

.PHONY: format
format:
	$(CLANG_FORMAT) -i */*.h */*.c

TEST_ENV_STAMPS=$(shell find test/envs/ -type f -name '*.dockerfile' | sed s/dockerfile/stamp/)
.PHONY: test-envs
test-envs: $(TEST_ENV_STAMPS)

test/envs/%.stamp: test/envs/%.dockerfile
	docker build -t leaf-$(shell basename $< .dockerfile) -f $< $(CURDIR)
	touch $@
