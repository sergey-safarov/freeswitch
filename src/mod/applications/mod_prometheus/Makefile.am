include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_prometheus

mod_LTLIBRARIES = mod_prometheus.la
mod_prometheus_la_SOURCES  = mod_prometheus.c
mod_prometheus_la_CFLAGS   = $(AM_CFLAGS)
mod_prometheus_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_prometheus_la_LDFLAGS  = -L/usr/lib -lpromhttp -L/usr/lib -lprom -avoid-version -module -no-undefined -shared
