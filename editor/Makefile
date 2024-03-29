ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = editor
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

SUBDIRS = pamflip specialty

# We tend to separate out the build targets so that we don't have
# any more dependencies for a given target than it really needs.
# That way, if there is a problem with a dependency, we can still
# successfully build all the stuff that doesn't depend upon it.
# This package is so big, it's useful even when some parts won't 
# build.

PORTBINARIES = pamaddnoise pamaltsat pambackground pambrighten \
	       pamcat pamcomp pamcut \
	       pamdice pamditherbw pamedge pamenlarge \
	       pamfunc pamhomography pamhue pamlevels \
	       pammasksharpen pammixmulti \
	       pamperspective pamrecolor pamrestack pamrubber \
	       pamscale pamshuffle pamsistoaglyph pamstretch pamthreshold \
	       pamundice pamwipeout \
	       pbmclean pbmmask pbmpscale pbmreduce \
	       pgmdeshadow pgmenhance \
	       pgmmedian \
	       pnmalias pnmconvol pnmcrop \
	       pnmgamma \
	       pnmhisteq pnminvert pnmmontage \
	       pnmnlfilt pnmnorm pnmpad pnmpaste \
	       pnmremap pnmrotate \
	       pnmscalefixed pnmshear pnmsmooth pnmstitch pnmtile \
	       ppmchange ppmcolormask \
	       ppmdim ppmdist ppmdither ppmdraw \
	       ppmflash ppmlabel ppmmix \

# We don't include programs that have special library dependencies in the
# merge scheme, because we don't want those dependencies to prevent us
# from building all the other programs.

NOMERGEBINARIES = 
MERGEBINARIES = $(PORTBINARIES)

BINARIES = $(MERGEBINARIES) $(NOMERGEBINARIES)
SCRIPTS = ppmbrighten pnmflip ppmfade ppmquant ppmshadow \
	  pamstretch-gen pnmmargin pnmquant pnmquantall 

OBJECTS = $(BINARIES:%=%.o)

MERGE_OBJECTS = $(MERGEBINARIES:%=%.o2)

HAVE_MERGE_COMPAT=YES

.PHONY: all
all: $(BINARIES) $(SUBDIRS:%=%/all)

include $(SRCDIR)/common.mk

install.bin install.merge: install.bin.local

.PHONY: install.bin.local
install.bin.local: $(PKGDIR)/bin
# Remember that $(SYMLINK) might just be a copy command.

# In December 2001, pamstretch replaced pnminterp and pamstretch-getn
# replaced pnminterp-gen
	cd $(PKGDIR)/bin ; \
	rm -f pnminterp$(EXE); \
	$(SYMLINK) pamstretch$(EXE) pnminterp$(EXE)
	cd $(PKGDIR)/bin ; \
	rm -f pnminterp-gen$(EXE); \
	$(SYMLINK) pamstretch-gen$(EXE) pnminterp-gen$(EXE)
# In March 2002, pnmnorm replaced ppmnorm and pgmnorm
	cd $(PKGDIR)/bin ; \
	rm -f ppmnorm$(EXE) ; \
	$(SYMLINK) pnmnorm$(EXE) ppmnorm$(EXE)
	cd $(PKGDIR)/bin ; \
	rm -f pgmnorm$(EXE) ; \
	$(SYMLINK) pnmnorm$(EXE) pgmnorm$(EXE)
# In March 2003, pamedge replaced pgmedge
	cd $(PKGDIR)/bin ; \
	rm -f pgmedge$(EXE) ; \
	$(SYMLINK) pamedge$(EXE) pgmedge$(EXE)
# In October 2004, pamenlarge replaced pnmenlarge
	cd $(PKGDIR)/bin ; \
	rm -f pnmenlarge$(EXE) ; \
	$(SYMLINK) pamenlarge$(EXE) pnmenlarge$(EXE)
# In March 2009, pamcut replaced pnmcut (but pamcut is much older -- pnmcut
# was obsoleted by pamcut long before this).
	cd $(PKGDIR)/bin ; \
	rm -f pnmcut$(EXE) ; \
	$(SYMLINK) pamcut$(EXE) pnmcut$(EXE)
# In March 2009, pamscale replaced pnmscale (but pamscale is much older --
# pnmscale was obsoleted by pamscale long before this).
	cd $(PKGDIR)/bin ; \
	rm -f pnmscale$(EXE) ; \
	$(SYMLINK) pamscale$(EXE) pnmscale$(EXE)
# In March 2012, pnmquantall replaced ppmquantall
	cd $(PKGDIR)/bin ; \
	rm -f ppmquantall$(EXE) ; \
	$(SYMLINK) pnmquantall$(EXE) ppmquantall$(EXE)
# In August 2014, pamcomp replaced pnmcomp
	cd $(PKGDIR)/bin ; \
	rm -f pnmcomp$(EXE) ; \
	$(SYMLINK) pamcomp$(EXE) pnmcomp$(EXE)
# In August 2022, pamcat replaced pnmcat
	cd $(PKGDIR)/bin ; \
	rm -f pnmcat$(EXE) ; \
	$(SYMLINK) pamcat$(EXE) pnmcat$(EXE)

mergecomptrylist:
	cat /dev/null >$@
	echo "TRY(\"pnminterp\",  main_pamstretch);" >>$@
	echo "TRY(\"pgmnorm\",    main_pnmnorm);"    >>$@
	echo "TRY(\"ppmnorm\",    main_pnmnorm);"    >>$@
	echo "TRY(\"pgmedge\",    main_pamedge);"    >>$@
	echo "TRY(\"pnmenlarge\", main_pamenlarge);" >>$@
	echo "TRY(\"pnmcut\",     main_pamcut);"     >>$@
	echo "TRY(\"pnmscale\",   main_pamscale);"   >>$@
	echo "TRY(\"pnmcomp\",    main_pamcomp);"    >>$@
	echo "TRY(\"pnmcat\",     main_pamcat);"     >>$@

