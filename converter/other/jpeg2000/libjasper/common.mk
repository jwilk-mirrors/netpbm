# This is common rules for the libjasper subdirectories.
#
# Set the following variables before including this:
#
#  SUBDIRS:      Subdirectory names
#  LIB_OBJECTS:  List of object files from this directory that go into 
#                libjasper.
#  JASPERSRCDIR: libjasper source directory

all: $(LIB_OBJECTS) partlist

partlist: $(SUBDIRS:%=%/partlist)
	cat /dev/null $(SUBDIRS:%=%/partlist) >$@
	echo $(LIB_OBJECTS:%=$(CURDIR)/%) >>$@

$(SUBDIRS:%=%/partlist):
	$(MAKE) -C $(dir $@) -f $(SRCDIR)/$(SUBDIR)/$(dir $@)Makefile \
	    SRCDIR=$(SRCDIR) BUILDDIR=$(BUILDDIR) $(notdir $@) 

include $(SRCDIR)/common.mk

INCLUDES := -I$(JASPERSRCDIR)/include $(INCLUDES)

DEFS = -DHAVE_LIBM=1 -DSTDC_HEADERS=1 -DHAVE_FCNTL_H=1 -DHAVE_LIMITS_H=1 -DHAVE_UNISTD_H=1 -DHAVE_SYS_TYPES_H=1 -DHAVE_STDLIB_H=1 -DHAVE_STDDEF_H=1 -DEXCLUDE_BMP_SUPPORT -DEXCLUDE_RAS_SUPPORT -DEXCLUDE_MIF_SUPPORT -DEXCLUDE_JPG_SUPPORT -DEXCLUDE_PGX_SUPPORT -DEXCLUDE_PNM_SUPPORT

$(LIB_OBJECTS):%.o:%.c
	$(CC) -c $(INCLUDES) $(DEFS) $(CFLAGS_ALL) $<

$(LIB_OBJECTS): importinc

thisdirclean: localclean

.PHONY: localclean
localclean:
	rm -f partlist

.PHONY: FORCE
FORCE:
