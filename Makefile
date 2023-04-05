EXECBIN  = httpserver
SOURCES  = $(wildcard *.c)
HEADERS  = $(wildcard *.h)
OBJECTS  = $(SOURCES:%.c=%.o)
LIBRARY  =  asgn4_helper_funcs.a
FORMATS  = $(SOURCES:%.c=.format/%.c.fmt) $(HEADERS:%.h=.format/%.h.fmt)

CC       = clang
FORMAT   = clang-format
CFLAGS   = -Wall -Wpedantic -Werror -Wextra 

.PHONY: all clean format

all: $(EXECBIN) format

$(EXECBIN): $(OBJECTS) $(LIBRARY)
	$(CC) -o $@ $^

%.o : %.c %.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(EXECBIN) $(OBJECTS)

nuke: clean
	rm -rf .format
	
format:
	clang-format -i -style=file *.[ch]

