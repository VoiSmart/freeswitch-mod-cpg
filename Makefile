CC=gcc
INCLUDE= -lcpg -I/opt/freeswitch/include
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

#mod_cpg.lo: 
#	gcc -lcpg -I/usr/src/freeswitch/src/include -I/usr/src/freeswitch/src/include -I/usr/src/freeswitch/libs/libteletone/src -fPIC -Werror -fvisibility=hidden -DSWITCH_API_VISIBILITY=1 -DHAVE_VISIBILITY=1 -g -ggdb -g -O2 -Wall -std=c99 -pedantic -Wdeclaration-after-statement -D_GNU_SOURCE -DHAVE_CONFIG_H -c -o mod_cpg.lo mod_cpg.c

#lib-mod_cpg:
#	mkdir -p .libs && gcc -lcpg -I/opt/freeswitch/include -I/usr/src/freeswitch/libs/libteletone/src -fPIC -Werror -fvisibility=hidden -DSWITCH_API_VISIBILITY=1 -DHAVE_VISIBILITY=1 -g -ggdb -g -ggdb -Wall -std=c99 -pedantic -Wdeclaration-after-statement -D_GNU_SOURCE -DHAVE_CONFIG_H -c mod_cpg.c  -fPIC -DPIC -o .libs/mod_cpg.o

clean:
	rm -f *.o; \
	rm -f *.lo; \
	rm -f *.so; \
	rm -rf .libs;

install: all 
	mkdir -p /opt/freeswitch/mod; \
	mv *.so /opt/freeswitch/mod/;

uninstall:
	rm -f /opt/freeswitch/mod/mod_cpg.so
