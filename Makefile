INSTALL_PREFIX?=

CORODIR = /etc/corosync
INSTDIR = $(INSTALL_PREFIX)/opt/freeswitch
MODDIR = $(INSTDIR)/mod
CONFDIR = $(INSTDIR)/conf/autoload_configs
CC=gcc
INCLUDE= -lcpg -lnl -I$(INSTDIR)/include
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

install: all conf-install 
	mkdir -p $(MODDIR); \
	install *.so $(MODDIR);

conf-install:
	mkdir -p $(CONFDIR); \
	if [ ! -f $(CONFDIR)/cpg.conf.xml ]; \
	then \
		echo "installing conf file"; \
		install cpg.conf.xml $(CONFDIR); \
	fi
	#echo "installing corosync conf file";
	mkdir -p $(CORODIR); \	
	install corosync.conf $(CORODIR); 
	if [ ! -f $(CORODIR)/uidgid.d/freeswitch.corosync ]; \
	then \
		echo "installing corosync uidgig conf file"; \
		mkdir -p $(CORODIR)/uidgid.d; \
		install freeswitch.corosync $(CORODIR)/uidgid.d; \
	fi


uninstall:
	rm -f $(MODDIR)/mod_cpg.so
