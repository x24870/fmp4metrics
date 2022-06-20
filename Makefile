PWD=$(shell pwd)
OS=$(shell uname | tr '[:upper:]' '[:lower:]')
BIN=$(shell basename `git rev-parse --show-toplevel`)
TAG=korob/${BIN}
BUILDER_REPO=korob/builder:latest

.PHONY: all linux darwin docker clean

all: $(OS)

linux:
	docker run \
		--rm \
		-v $(PWD):/build:z \
		-it $(BUILDER_REPO) \
		/bin/sh -c "cd /build && make -f ${BIN}.mk clean all BIN=${BIN}"

darwin:
	$(MAKE) -f ${BIN}.mk clean all BIN=${BIN}

docker: linux
	@[ -z "$(shell git status --porcelain)" ] || \
		(echo "\033[0;31mYou have uncommitted local changes.\033[0m" ; \
		 echo "\033[0;31mCommit/stash them before building.\033[0m" ; false)
	docker build -t ${TAG} .
	docker tag ${TAG} ${TAG}:latest
	docker push ${TAG}:latest

clean:
	$(MAKE) -f ${BIN}.mk clean BIN=${BIN}

