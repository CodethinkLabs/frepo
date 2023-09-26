BINARY_RELEASE = frepo
BINARY_DEBUG   = $(BINARY_RELEASE)-debug

INCLUDE = -I include
CFLAGS_COMMON = $(INCLUDE) -Wall -Wextra -Werror -MD -MP
CFLAGS_RELEASE  = -DNDEBUG -O3 $(CFLAGS_COMMON)
CFLAGS_DEBUG = -O0 -g $(CFLAGS_COMMON)
LDFLAGS_DEBUG = -lm
LDFLAGS_RELEASE = -s $(LDFLAGS_DEBUG)

SRC = $(shell find src -type f)
OBJ_RELEASE = $(patsubst src/%.c, .build/%.o, $(SRC))
DEP_RELEASE = $(patsubst src/%.c, .build/%.d, $(SRC))
OBJ_DEBUG = $(patsubst src/%.c, .build/debug/%.o, $(SRC))
DEP_DEBUG = $(patsubst src/%.c, .build/debug/%.d, $(SRC))


PREFIX ?= $(DESTDIR)/usr/local
BINDIR ?= $(PREFIX)/bin

all : release

release : $(BINARY_RELEASE)

debug : $(BINARY_DEBUG)

.build/%.o: src/%.c
	mkdir -p $(basename $@)
	$(CC) -c $(CFLAGS_RELEASE) -o $@ $<

.build/debug/%.o: src/%.c
	mkdir -p $(basename $@)
	$(CC) -c $(CFLAGS_DEBUG) -o $@ $<

$(BINARY_RELEASE) : $(OBJ_RELEASE)
	$(CC) -o $@ $^ $(LDFLAGS_RELEASE)

$(BINARY_DEBUG) : $(OBJ_DEBUG)
	$(CC) -o $@ $^ $(LDFLAGS_DEBUG)

clean:
	rm -rf .build $(BINARY_RELEASE) $(BINARY_DEBUG)

install: $(BINARY_RELEASE)
	install -d $(BINDIR)
	install $(BINARY_RELEASE) $(BINDIR)

uninstall:
	rm -rf $(addprefix $(BINDIR)/,$(BINARY_RELEASE))

.build/cppcheck.log: $(SRC)
	cppcheck -I include --enable=all --force --quiet $^ 2> $@

cppcheck: .build/cppcheck.log
	cat $<

loc:
	wc -l $(SRC)

-include $(DEP_RELEASE) $(DEP_DEBUG)

.PHONY : all release debug clean install uninstall cppcheck loc
