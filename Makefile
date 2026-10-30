# Hospital Priority Queue & Bed Allocation Scheduler
#
#   make          build build/hospital_scheduler
#   make run      build and run on http://127.0.0.1:8080
#   make test     build and run all unit/API tests
#   make clean    remove build output and the generated asset header

CC       ?= gcc
CFLAGS   ?= -O2 -g
CFLAGS   += -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -Isrc
LDLIBS    = -lm

CORE     = conditions patient priority queue bed simulation statistics \
           json_utils api
CORE_OBJ = $(CORE:%=build/%.o)
APP_OBJ  = build/main.o build/server.o
TESTS    = test_priority test_queue test_beds test_simulation \
           test_statistics test_json test_api
TEST_BIN = $(TESTS:%=build/tests/%)

# The suites model a 10-bed hospital that accepts arrivals at the current
# minute (a fixed fixture), independent of the production defaults in
# config.h.  Their core objects are therefore built separately.
TEST_DEFS = -DNUM_BEDS=10 -DMIN_ARRIVAL_LEAD_MINUTES=0
TEST_OBJ  = $(CORE:%=build/t_%.o)

WEB_FILES = web/index.html web/styles.css web/app.js

.PHONY: all run test clean

all: build/hospital_scheduler

build/hospital_scheduler: $(CORE_OBJ) $(APP_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

# The web/ files are turned into a C header at build time.
generated/embedded_assets.h: $(WEB_FILES) scripts/embed_assets.py
	python3 scripts/embed_assets.py web $@

build/server.o: generated/embedded_assets.h

build/%.o: src/%.c $(wildcard src/*.h) | build
	$(CC) $(CFLAGS) -c $< -o $@

build/t_%.o: src/%.c $(wildcard src/*.h) | build
	$(CC) $(CFLAGS) $(TEST_DEFS) -c $< -o $@

build/tests/%: tests/%.c tests/test_util.h $(TEST_OBJ) | build/tests
	$(CC) $(CFLAGS) $(TEST_DEFS) -Itests $< $(TEST_OBJ) -o $@ $(LDLIBS)

build build/tests:
	mkdir -p $@

run: all
	./build/hospital_scheduler

test: $(TEST_BIN)
	@for t in $(TEST_BIN); do ./$$t || exit 1; done
	@echo "All test suites passed."

clean:
	rm -rf build generated/embedded_assets.h
