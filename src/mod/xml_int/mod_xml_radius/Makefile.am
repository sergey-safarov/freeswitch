include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_xml_radius

if HAVE_FREERADIUS
mod_LTLIBRARIES = mod_xml_radius.la
mod_xml_radius_la_SOURCES  = mod_xml_radius.c
mod_xml_radius_la_CFLAGS   = $(AM_CFLAGS)
mod_xml_radius_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_xml_radius_la_LDFLAGS  = -avoid-version -module -no-undefined -shared
else
install: error
all: error
error:
	$(error You must install freeradius-client >= 1.1.7 to build $(MODNAME))
endif
