CC ?= gcc
CROSS ?= aarch64-linux-gnu-gcc
CFLAGS ?= -O2 -std=gnu11 -Wall -Wextra -pthread -Isrc
NATIVE_CFLAGS = $(CFLAGS) -Ithird_party/libjpeg
ARM_CFLAGS = $(CFLAGS) -Ithird_party/libjpeg62
LDLIBS ?= -ljpeg -lm

SRC = src/main.c src/server.c src/camera.c src/jpeg.c src/vision.c src/risk.c src/adb.c src/util.c
CORE = src/util.c src/risk.c src/vision.c

.PHONY: all native arm64 test clean check-deps

all: native

native: pasillo-seguro

pasillo-seguro: $(SRC) src/pasillo.h
	$(CC) $(NATIVE_CFLAGS) -o $@ $(SRC) $(LDLIBS)

arm64: pasillo-seguro-arm64

pasillo-seguro-arm64: $(SRC) src/pasillo.h third_party/libjpeg-arm64/libjpeg.so.62
	$(CROSS) $(ARM_CFLAGS) -o $@ $(SRC) -Lthird_party/libjpeg-arm64 -l:libjpeg.so.62 -lm -pthread

tests/test_core: tests/test_core.c $(CORE) src/pasillo.h
	$(CC) $(NATIVE_CFLAGS) -o $@ tests/test_core.c $(CORE) -lm -pthread

test: tests/test_core pasillo-seguro
	./tests/test_core
	@if command -v python3 >/dev/null 2>&1; then python3 tests/integration.py; fi

check-deps:
	@test -f third_party/libjpeg/jpeglib.h || (echo "faltan headers de libjpeg v8" && exit 1)
	@test -f third_party/libjpeg62/jpeglib.h || (echo "faltan headers de libjpeg v62" && exit 1)
	@test -f third_party/libjpeg-arm64/libjpeg.so.62 || (echo "falta libjpeg arm64; ejecuta scripts/build_arm64.sh" && exit 1)

clean:
	rm -f pasillo-seguro pasillo-seguro-arm64 tests/test_core
	rm -f src/*.o
