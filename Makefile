INSTALL_PREFIX?=

INSTDIR = $(INSTALL_PREFIX)/opt/freeswitch
MODDIR = $(INSTDIR)/mod
CC=gcc
INCLUDE= -lcpg -I$(INSTDIR)/include
CFLAGS= -fPIC -Werror -fvisibility=hidden -DSWITCH_API_VISIBILITY=1 -DHAVE_VISIBILITY=1 -g -ggdb -g -O2 -Wall -std=c99 -pedantic -Wdeclaration-after-statement -D_GNU_SOURCE -DHAVE_CONFIG_H
LDFLAGS=
SOURCES= arpator.c cpg_utils.c profile.c mod_cpg.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=mod_cpg.so

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(INCLUDE) $(CFLAGS) $(LDFLAGS) -lm -module -avoid-version -o mod_cpg.so -shared -Xlinker -x mod_cpg.o arpator.o cpg_utils.o profile.o

.c.o:
	$(CC) $(INCLUDE) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o; \
	rm -f *.lo; \
	rm -f *.so; \
	rm -rf .libs;

install: all 
	mkdir -p $(MODDIR); \
	install *.so $(MODDIR);

uninstall:
	rm -f $(MODDIR)/mod_cpg.so
