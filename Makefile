# 

MODULE_big = test_sw
OBJS = \
	$(WIN32RES) \
	test_sw.o
PGFILEDESC = "test_sw - test code for context switch"

EXTENSION = test_sw
DATA = test_sw--1.0.sql

REGRESS = test_sw

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = src/test/modules/test_sw
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
