include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_rad_auth

if HAVE_FREERADIUS
mod_LTLIBRARIES = mod_rad_auth.la
mod_rad_auth_la_SOURCES  = mod_rad_auth.c
mod_rad_auth_la_CFLAGS   = $(AM_CFLAGS)
mod_rad_auth_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_rad_auth_la_LDFLAGS  = -avoid-version -module -no-undefined -shared
else
install: error
all: error
error:
	$(error You must install freeradius-client >= 1.1.7 to build $(MODNAME))
endif
