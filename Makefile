CFLAGS:= -fPIC -Wall -Werror -O6 -g -D_GNU_SOURCE
CFLAGS+= $(shell pkg-config --cflags glib-2.0)
LDFLAGS:= -g -fPIC
LDFLAGS+= $(shell pkg-config --libs glib-2.0) -lpcre
RANLIB:=ranlib

MAJOR:=1
MINOR:=0
REV:=0
VERSION:=$(MAJOR).$(MINOR).$(REV)

ifeq (${NDEBUG}, 1)
CFLAGS+= -DNDEBUG
endif

ifeq (${PROFILE}, 1)
CFLAGS+= -p
LDFLAGS+= -p
endif

all: libfmime.so.$(VERSION) libfmime.a test megaTest

libfmime.o: libfmime.c fmime.h

test: test.o libfmime.o

megaTest: megaTest.o libfmime.a

libfmime.a: libfmime.o
	rm -f $@
	$(AR) rc $@ $^
	$(RANLIB) $@

libfmime.so.$(VERSION): libfmime.o
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-soname,libfmime.so.$(MAJOR) -shared -o $@ $< 

clean:
	$(RM) *~ *.o core core.* libfmime.so.* fmime-test test megaTest

install: all
	install -d --owner=root --group=root $(DESTDIR)/usr/lib $(DESTDIR)/usr/include/fmime
	install --owner=root --group=root libfmime.a libfmime.so.$(VERSION) $(DESTDIR)/usr/lib/
	install --owner=root --group=root *.h $(DESTDIR)/usr/include/fmime/
	ln -sf /usr/lib/libfmime.so.$(VERSION) $(DESTDIR)/usr/lib/libfmime.so.$(MAJOR)
	ln -sf /usr/lib/libfmime.so.$(MAJOR) $(DESTDIR)/usr/lib/libfmime.so
	-ldconfig

.PHONY: all install clean
