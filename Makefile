CPP    := g++ -Wall -g
LIB   := -lfuse
INC   := -I/usr/include -I.
C_SRCS:= $(wildcard *.c)
CXSRCS:= $(wildcard *.cpp)
OBJ   := $(patsubst %.c,%.o,$(C_SRCS)) $(patsubst %.cpp,%.o,$(CXSRCS))
DEFS  := -D_REENTRANT -D_THREAD_SAFE

LOGINFS := loginfs
LIBANCILLARY_DIR := libancillary/

DESTDIR = /
BIN_DIR = /usr/bin

all: loginfs

loginfs: $(OBJ) libancillary
	$(CPP) -o $(WISHD) $(OBJ) $(LIBINC) $(LIB)

libancillary:
	$(MAKE) -C $(LIBANCILLARY_DIR)

%.o : %.c
	$(CPP) -o $@ $(INC) -c $< $(DEFS)

%.o : %.cpp
	$(CPP) -o $@ $(INC) -c $< $(DEFS)

.PHONY: clean
clean:
	/bin/rm -f $(OBJ) $(LOGINFS)
	$(MAKE) -C $(LIBANCILLARY_DIR) clean
