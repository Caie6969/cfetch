CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
LDFLAGS ?= -lpthread

SRC     := src/main.c src/config.c src/sysinfo.c src/setup.c src/render.c \
           src/ascii_extract.c src/embedded_blobs.c src/color_tui.c
OBJ     := $(SRC:.c=.o)
BIN     := cfetch

PREFIX  ?= /usr/local

BLOB_GEN     := src/gen_blobs
BLOB_GEN_SRC := src/gen_blobs.c
EMBED_C      := src/embedded_blobs.c
EMBED_H      := src/embedded_blobs.h
BLOB_INPUTS  := ascii/pfetch ascii/neofetch ascii/tux.txt ascii/minimal-tux.txt

all: $(BIN)

# Build the tiny generator
$(BLOB_GEN): $(BLOB_GEN_SRC)
	$(CC) -O2 -o $@ $<

# Generate embedded_blobs.{c,h} from ascii/pfetch + ascii/neofetch + tux files
$(EMBED_C) $(EMBED_H): $(BLOB_GEN) $(BLOB_INPUTS)
	./$(BLOB_GEN) $(EMBED_C) $(EMBED_H) \
	    pfetch_blob       ascii/pfetch \
	    neofetch_blob     ascii/neofetch \
	    tux_blob          ascii/tux.txt \
	    minimal_tux_blob  ascii/minimal-tux.txt

# All sources depend on the header existing
$(OBJ): $(EMBED_H)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

clean:
	rm -f $(OBJ) $(BIN) $(BLOB_GEN) $(EMBED_C) $(EMBED_H)

.PHONY: all install clean
