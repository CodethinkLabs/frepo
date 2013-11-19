PROJECT = frepo

CC     ?= gcc
LD     ?= gcc

CFLAGS ?= -O3 -Wextra -Isrc

SOURCE  = $(foreach dir, ., $(wildcard $(dir)/src/*.c))
OBJECTS = $(patsubst %.c, %.o, $(SOURCE))

.PHONY : all
all : $(PROJECT)

$(PROJECT) : $(OBJECTS)
	gcc $(OBJECTS) -o $(PROJECT)

$(OBJECTS) : %.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@


.PHONY : clean
clean :
	rm -rf $(OBJECTS)
	rm -rf $(PROJECT)
