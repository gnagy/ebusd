AM_CXXFLAGS = -I$(top_srcdir)/src \
	      -isystem$(top_srcdir) \
	      -Wno-unused-parameter

noinst_PROGRAMS = test_rotatefile

test_rotatefile_SOURCES = test_rotatefile.cpp
test_rotatefile_LDADD = ../libutils.a -lpthread

distclean-local:
	-rm -f Makefile.in
	-rm -rf .libs
