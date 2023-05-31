SRCPATH = src
BUILD_DIR = build

include $(SRCPATH)/mjs_sources.mk

TOP_HEADERS = $(addprefix $(SRCPATH)/, $(HEADERS))
TOP_MJS_PUBLIC_HEADERS = $(addprefix $(SRCPATH)/, $(MJS_PUBLIC_HEADERS))
TOP_MJS_SOURCES = $(addprefix $(SRCPATH)/, $(MJS_SOURCES))
TOP_COMMON_SOURCES = $(addprefix $(SRCPATH)/, $(COMMON_SOURCES))

CFLAGS_EXTRA ?=
MFLAGS += -I. -Isrc -Isrc/frozen
MFLAGS += -DMJS_MAIN -DMJS_EXPOSE_PRIVATE -DCS_ENABLE_STDIO -DMJS_ENABLE_DEBUG -I../frozen
MFLAGS += $(CFLAGS_EXTRA)
CFLAGS += -lm -std=c99 -Wall -Wextra -g $(MFLAGS)
COMMON_CFLAGS = -DCS_MMAP -DMJS_MODULE_LINES
ASAN_CFLAGS = -fsanitize=address

VERBOSE ?=
ifeq ($(VERBOSE),1)
Q :=
else
Q := @
endif

ifeq ($(OS),Windows_NT)
  UNAME_S := Windows
else
  UNAME_S := $(shell uname -s)
endif

ifeq ($(UNAME_S),Linux)
  COMMON_CFLAGS += -Wl,--no-as-needed -ldl
  ASAN_CFLAGS += -fsanitize=leak
endif

ifeq ($(UNAME_S),Darwin)
  MFLAGS += -D_DARWIN_C_SOURCE
endif

PROG = $(BUILD_DIR)/mjs
TEST = $(BUILD_DIR)/unit_test

all: mjs.c mjs_no_common.c $(PROG) $(TEST)

clean:
	rm -f mjs_no_common.c mjs.h mjs.c $(PROG) $(TEST)
	rm -f -d $(BUILD_DIR)

TESTUTIL_FILES = $(SRCPATH)/common/cs_dirent.c \
                 $(SRCPATH)/common/cs_time.c   \
                 $(SRCPATH)/common/test_main.c \
                 $(SRCPATH)/common/test_util.c

mjs.h: $(TOP_MJS_PUBLIC_HEADERS) Makefile tools/amalgam.py
	@printf "AMALGAMATING $@\n"
	$(Q) (tools/amalgam.py \
    --autoinc -I src --prefix MJS --strict --license src/mjs_license.h \
    --first common/platform.h $(TOP_MJS_PUBLIC_HEADERS)) > $@

mjs.c: $(TOP_COMMON_SOURCES) $(TOP_MJS_SOURCES) mjs.h Makefile
	@printf "AMALGAMATING $@\n"
	$(Q) (tools/amalgam.py \
    --autoinc -I src -I src/frozen --prefix MJS --license src/mjs_license.h \
    --license src/mjs_license.h --public-header mjs.h --autoinc-ignore mjs_*_public.h \
    --first mjs_common_guard_begin.h,common/platform.h,common/platforms/platform_windows.h,common/platforms/platform_unix.h,common/platforms/platform_esp_lwip.h \
    $(TOP_COMMON_SOURCES) $(TOP_MJS_SOURCES)) > $@

mjs_no_common.c: $(TOP_MJS_SOURCES) mjs.h Makefile
	@printf "AMALGAMATING $@\n"
	$(Q) (tools/amalgam.py \
    --autoinc -I src -I src/frozen --prefix MJS --license src/mjs_license.h \
    --public-header mjs.h --ignore mjs.h,*common/*,*frozen.[ch] \
    --first mjs_common_guard_begin.h,common/platform.h,common/platforms/platform_windows.h,common/platforms/platform_unix.h,common/platforms/platform_esp_lwip.h \
    $(TOP_MJS_SOURCES)) > $@

CFLAGS += $(COMMON_CFLAGS)

# NOTE: we compile straight from sources, not from the single amalgamated file,
# in order to make sure that all sources include the right headers
$(PROG): $(TOP_MJS_SOURCES) $(TOP_COMMON_SOURCES) $(TOP_HEADERS)
	mkdir -p $(BUILD_DIR)
	gcc $(CFLAGS) $(TOP_MJS_SOURCES) $(TOP_COMMON_SOURCES) -o $(PROG)

$(TEST): tests/unit_test.c mjs.c $(TESTUTIL_FILES)
	mkdir -p $(BUILD_DIR)
	gcc tests/unit_test.c $(TESTUTIL_FILES) -g -o $(TEST) -I src -I . \
	-DMJS_MEMORY_STATS -DCS_MMAP -DMJS_MODULE_LINES -DMJS_ENABLE_DEBUG -lm
