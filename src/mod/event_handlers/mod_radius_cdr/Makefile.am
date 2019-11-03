include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_radius_cdr

if HAVE_FREERADIUS
mod_LTLIBRARIES = mod_radius_cdr.la
mod_radius_cdr_la_SOURCES  = mod_radius_cdr.c
mod_radius_cdr_la_CFLAGS   = $(AM_CFLAGS)
mod_radius_cdr_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_radius_cdr_la_LDFLAGS  = -avoid-version -module -no-undefined -shared
else
install: error
all: error
error:
	$(error You must install freeradius-client >= 1.1.7 to build $(MODNAME))
endif
