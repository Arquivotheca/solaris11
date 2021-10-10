#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

include $(SRC)/Makefile.master
include $(SRC)/Makefile.buildnum

#
# Make sure we're getting a consistent execution environment for the
# embedded scripts.
#
SHELL= /usr/bin/ksh93

#
# To suppress package dependency generation on any system, regardless
# of how it was installed, set SUPPRESSPKGDEP=true in the build
# environment.
#
SUPPRESSPKGDEP= false

#
# Comment this line out or set "PKGDEBUG=" in your build environment
# to get more verbose output from the make processes in usr/src/pkg
#
PKGDEBUG= @

#
# Cross platform packaging notes
#
# By default, we package the proto area from the same architecture as
# the packaging build.  In other words, if you're running nightly or
# bldenv on an x86 platform, it will take objects from the x86 proto
# area and use them to create x86 repositories.
#
# If you want to create repositories for an architecture that's
# different from $(uname -p), you do so by setting PKGMACH in your
# build environment.
#
# For this to work correctly, the following must all happen:
#
#   1. You need the desired proto area, which you can get either by
#      doing a gatekeeper-style build with the -U option to
#      nightly(1), or by using rsync.  If you don't do this, you will
#      get packaging failures building all packages, because pkgsend
#      is unable to find the required binaries.
#   2. You need the desired tools proto area, which you can get in the
#      same ways as the normal proto area.  If you don't do this, you
#      will get packaging failures building onbld, because pkgsend is
#      unable to find the tools binaries.
#   3. The remainder of this Makefile should never refer directly to
#      $(MACH).  Instead, $(PKGMACH) should be used whenever an
#      architecture-specific path or token is needed.  If this is done
#      incorrectly, then packaging will fail, and you will see the
#      value of $(uname -p) instead of the value of $(PKGMACH) in the
#      commands that fail.
#   4. Each time a rule in this Makefile invokes $(MAKE), it should
#      pass PKGMACH=$(PKGMACH) explicitly on the command line.  If
#      this is done incorrectly, then packaging will fail, and you
#      will see the value of $(uname -p) instead of the value of
#      $(PKGMACH) in the commands that fail.
#
# Refer also to the convenience targets defined later in this
# Makefile.
#
PKGMACH=	$(MACH)

#
# ROOT, TOOLS_PROTO, and PKGARCHIVE should be set by nightly or
# bldenv.  These macros translate them into terms of $PKGMACH, instead
# of $ARCH.
#
PKGROOT.cmd=	print $(ROOT) | \
	$(SED) -e s:/root_$(MACH):/root_$(PKGMACH):
PKGROOT=	$(PKGROOT.cmd:sh)

TOOLSROOT.cmd=	print $(TOOLS_PROTO) | \
	$(SED) -e s:/root_$(MACH):/root_$(PKGMACH):
TOOLSROOT=	$(TOOLSROOT.cmd:sh)

PKGDEST.cmd=	print $(PKGARCHIVE) | \
	$(SED) -e s:/$(MACH)/:/$(PKGMACH)/:
PKGDEST=	$(PKGDEST.cmd:sh)

$(MAN_INCLUDE)MAN_MANIFESTS.cmd=	print $(MAN_SRC)/pkg/*.man.p5m | \
	$(SED) -e 's/.man.p5m//g' -e 's|$(MAN_SRC)/pkg/||g'
$(MAN_INCLUDE)MAN_MANIFESTS=		$(MAN_MANIFESTS.cmd:sh)

PKGPROTODIRS=	$(PKGROOT) $(TOOLSROOT)
# Conditionally set up man directories.
$(MAN_INCLUDE)PKGPROTODIRS += $(MAN_SRC)/man $(MAN_SRC)/man_$(PKGMACH)
$(MAN_INCLUDE)MAN_PKGMOGRIFY_DIRS= $(MAN_SRC:%=-I %/pkg)


#
# The publish transforms, EXCEPTIONS list, and some manifests need to
# know when we're building open-only and when we're using internal
# crypto bits.
#
# We only use internal crypto when we're doing a closed build, the
# CODESIGN_USER env variable is not set, and ON_CRYPTO_BINS is not set.
# This matches the conditions under which the internal key and cert
# are needed for the packaged objects.
#
$(CLOSED_BUILD)OPEN_ONLY_BUILD= $(POUND_SIGN)

USE_INTERNAL_CRYPTO= $(POUND_SIGN)
UI1= $(CODESIGN_USER:%=$(POUND_SIGN))
$(UI1)UI2= $(ON_CRYPTO_BINS:%=$(POUND_SIGN))
$(CLOSED_BUILD)USE_INTERNAL_CRYPTO= $(UI1)$(UI2)
USE_SIGNED_CRYPTO=
$(USE_INTERNAL_CRYPTO)USE_SIGNED_CRYPTO= $(POUND_SIGN)

EXCEPTIONS= packaging
$(CLOSED_BUILD)EXCEPTIONS += packaging.closed
$(OPEN_ONLY_BUILD)EXCEPTIONS += packaging.open

PKGLINTRC = on-pkglintrc
LINT_PKGLINTRC = on-lint-pkglintrc

#
# Always build the redistributable repository, but only build the
# nonredistributable bits if we have access to closed source.
#
# Some objects that result from the closed build are still
# redistributable, and should be packaged as part of an open-only
# build.  Access to those objects is provided via the closed-bins
# tarball.  See usr/src/tools/scripts/bindrop.sh for details.
#
REPOS= redist
$(CLOSED_BUILD)REPOS += extra

#
# The packages directory will contain the processed manifests as
# direct build targets and subdirectories for package metadata extracted
# incidentally during manifest processing.
#
# Nothing underneath $(PDIR) should ever be managed by SCM.
#
PDIR= packages.$(PKGMACH)

PKGPUBLISHER= $(PKGPUBLISHER_REDIST)
$(PKGDEST)/repo.extra:= PKGPUBLISHER= $(PKGPUBLISHER_NONREDIST)

#
# To get these defaults, manifests should simply refer to $(PKGVERS).
#
PKGVERS_COMPONENT= 0.$(RELEASE)
PKGVERS_BUILTON= $(RELEASE)

#
# The PKGVERS_BRANCH is determined by the values in the Makefile.buildnum
#
PKGVERS_BRANCH_MAIN= 0.$(TRUNKID).$(UPDATENUM).$(SRUNUM).$(PLATNUM)
PKGVERS_BRANCH= $(PKGVERS_BRANCH_MAIN).$(BUILDID).$(NIGHTLYID)

PKGVERS= $(PKGVERS_COMPONENT),$(PKGVERS_BUILTON)-$(PKGVERS_BRANCH)

#
# The ARCH32 and ARCH64 macros are used in the manifests to express
# architecture-specific subdirectories in the installation paths
# for isaexec'd commands.
#
# We can't simply use $(MACH32) and $(MACH64) here, because they're
# only defined for the build architecture.  To do cross-platform
# packaging, we need both values.
#
i386_ARCH32= i86
sparc_ARCH32= sparcv7
i386_ARCH64= amd64
sparc_ARCH64= sparcv9

#
# macros and transforms needed by pkgmogrify
#
# If you append to this list using target-specific assignments (:=),
# be very careful that the targets are of the form $(PDIR)/pkgname.  If
# you use a higher level target, or a package list, you'll trigger a
# complete reprocessing of all manifests because they'll fail command
# dependency checking.
#
PM_TRANSFORMS= common_actions publish restart_fmri defaults extract_metadata
PM_INC= transforms

#
# It's hard to manage the relationships between transform files and the
# manifests they affect, so we choose a broader rule: .mog files should be
# rebuilt if any transforms change.  So we collect up the names of all of the
# transforms here and add them as dependencies in .mog rules below.
#
ALLTRANSFORMS :sh= (cd transforms; print *)
TRANSFORM_PATHS=$(ALLTRANSFORMS:%=$(PM_INC)/%)

PKGMOG_DEFINES= \
	i386_ONLY=$(POUND_SIGN) \
	sparc_ONLY=$(POUND_SIGN) \
	$(PKGMACH)_ONLY= \
	ARCH=$(PKGMACH) \
	ARCH32=$($(PKGMACH)_ARCH32) \
	ARCH64=$($(PKGMACH)_ARCH64) \
	MAN_INCLUDE=$(MAN_INCLUDE) \
	MAN_INCLUDE_i386_ONLY=$(POUND_SIGN) \
	MAN_INCLUDE_SPARC_ONLY=$(POUND_SIGN) \
	MAN_INCLUDE_$(PKGMACH)_ONLY=$(MAN_INCLUDE) \
	PKGVERS_COMPONENT=$(PKGVERS_COMPONENT) \
	PKGVERS_BUILTON=$(PKGVERS_BUILTON) \
	PKGVERS_BRANCH=$(PKGVERS_BRANCH) \
	PKGVERS=$(PKGVERS) \
	CLOSED_BUILD=$(CLOSED_BUILD) \
	OPEN_BUILD=$(OPEN_ONLY_BUILD) \
	USE_INTERNAL_CRYPTO=$(USE_INTERNAL_CRYPTO) \
	USE_SIGNED_CRYPTO=$(USE_SIGNED_CRYPTO)

PKGDEP_TOKENS_i386= \
	'PLATFORM=i86hvm' \
	'PLATFORM=i86pc' \
	'PLATFORM=i86xpv' \
	'ISALIST=amd64' \
	'ISALIST=i386'
PKGDEP_TOKENS_sparc= \
	'PLATFORM=sun4u' \
	'PLATFORM=sun4v' \
	'ISALIST=sparcv9' \
	'ISALIST=sparc'
PKGDEP_TOKENS= $(PKGDEP_TOKENS_$(PKGMACH))

#
# The package lists are generated with $(PKGDEP_TYPE) as their
# dependency types, so that they can be included by either an
# incorporation or a group package.
#
$(PDIR)/osnet-redist.mog $(PDIR)/osnet-extra.mog:= PKGDEP_TYPE= require
$(PDIR)/osnet-incorporation.mog:= PKGDEP_TYPE= incorporate

PKGDEP_INCORP= \
	depend fmri=consolidation/osnet/osnet-incorporation type=require

#
# All packaging build products should go into $(PDIR), so they don't
# need to be included separately in CLOBBERFILES.
#
CLOBBERFILES= $(PDIR) proto_list_$(PKGMACH)

#
# By default, PKGS will list all manifests.  To build and/or publish a
# subset of packages, override this on the command line or in the
# build environment and then reference (implicitly or explicitly) the all
# or install targets.
#
MANIFESTS :sh= (cd manifests; print *.mf; cd ..; cd manifests.defunct; print *.mf)
PKGS= $(MANIFESTS:%.mf=%)
PKGSCP= $(MANIFESTS:%.mf=$(PDIR)/%.mf)
DEP_PKGS= $(PKGS:%=$(PDIR)/%.dep)
PROC_PKGS= $(PKGS:%=$(PDIR)/%.mog)

#
# Track the synthetic manifests separately so we can properly express
# build rules and dependencies.  The synthetic and real packages use
# different sets of transforms and macros for pkgmogrify.
#
SYNTH_PKGS= osnet-incorporation osnet-redist
$(CLOSED_BUILD)SYNTH_PKGS += osnet-extra
DEP_SYNTH_PKGS= $(SYNTH_PKGS:%=$(PDIR)/%.dep)
PROC_SYNTH_PKGS= $(SYNTH_PKGS:%=$(PDIR)/%.mog)

#
# For each package, we determine the target repository based on
# manifest-embedded metadata.  Because we make that determination on
# the fly, the publication target cannot be expressed as a
# subdirectory inside the unknown-by-the-makefile target repository.
#
# In order to limit the target set to real files in known locations,
# we use a ".pub" file in $(PDIR) for each processed manifest, regardless
# of content or target repository.
#
PUB_PKGS= $(SYNTH_PKGS:%=$(PDIR)/%.pub) $(PKGS:%=$(PDIR)/%.pub)

#
# For each package, we concatenate the individual license
# action payloads to create a single, monolithic license action.
#
LIC_PAYLOADS= $(PUB_PKGS:%.pub=%.l)

#
# Any given repository- and status-specific package list may be empty,
# but we can only determine that dynamically, so we always generate all
# lists for each repository we're building.
#
# The meanings of each package status are as follows:
#
# 	PKGSTAT		meaning
# 	----------	----------------------------------------------------
# 	noincorp	Do not include in incorporation or group package
#	obsolete	Include in incorporation, but not group package
#	renamed		Include in incorporation, but not group package
#	current		Include in incorporation and group package
#	unlockable	Like current, but unlockable
#
# Since the semantics of the "noincorp" package status dictate that
# such packages are not included in the incorporation or group packages,
# there is no need to build noincorp package lists.
#
PKGLISTS= \
	$(REPOS:%=$(PDIR)/packages.%.current) \
	$(REPOS:%=$(PDIR)/packages.%.unlockable) \
	$(REPOS:%=$(PDIR)/packages.%.renamed) \
	$(REPOS:%=$(PDIR)/packages.%.obsolete)

.KEEP_STATE:

.PARALLEL: $(PKGS) $(PKGSCP) $(PROC_PKGS) $(DEP_PKGS) \
	$(PROC_SYNTH_PKGS) $(DEP_SYNTH_PKGS) $(PUB_PKGS) \
	$(LIC_PAYLOADS) protocmp pmodes

#
# For a single manifest, the dependency chain looks like this:
#
#	raw manifest (mypkg.mf)
#		|
#		| copy from manifests / manifests.defunct into packages.$(ARCH)
#		|
#	copied manifest (mypkg.mf)
#		|
#		| use pkgmogrify to process manifest
#		|
#	processed manifest (mypkg.mog)
#		|
#	   *    | use pkgdepend generate to generate dependencies
#		|
#	manifest with TBD dependencies (mypkg.dep)
#		|
#	   %    | use pkgdepend resolve to resolve dependencies
#		|
#	manifest with dependencies resolved (mypkg.res)
#		|
#		| use pkglint to check manifest for errors before publishing
#		| ("ERROR" messages returned by pkglint will prevent
#		|  publishing and will cause the build to fail)
#		|
#	checked manifest (mypkg.res)
#		|
#		| use pkgsend to publish the package
#		|
#	placeholder to indicate successful publication (mypkg.pub)
#	
# * This may be suppressed via SUPPRESSPKGDEP.  The resulting
#   packages will install correctly, but care must be taken to
#   install all dependencies, because pkg will not have the input
#   it needs to determine this automatically.
#
# % This is included in this diagram to make the picture complete, but
#   this is a point of synchronization in the build process.
#   Dependency resolution is actually done once on the entire set of
#   manifests, not on a per-package basis.
#
# The full dependency chain for generating everything that needs to be
# published, without actually publishing it, looks like this:
#
#	processed synthetic packages
#		|		|
#       package lists	    synthetic package manifests
#		|
#	processed real packages
#	    |		|
#	package dir	real package manifests
#			|
#			( linted package manifests )
#
# Here, each item is a set of real or synthetic packages.  For this
# portion of the build, no reference is made to the proto area.  It is
# therefore suitable for the "all" target, as opposed to "install."
#
# Since each of these steps is expressed explicitly, "all" need only
# depend on the head of the chain.
#
# From the end of manifest processing, the publication dependency
# chain looks like this:
#
#		repository metadata (catalogs and search indices)
#			|
#			| pkg.depotd
#			|
#		published packages
#		 |		|
#		 |		| pkgsend publish
#		 |		|
#	  repositories 	    checked packages
#		|		|
# pkgrepo create|		| pkglint
# pkgrepo 	| 		|
# add-publisher	|		|
#		|	resolved dependencies
#	repo directories	|
#				| pkgdepend resolve
#				|
#		 	generated dependencies
#	 			|
#				| pkgdepend
#				|
#			processed manifests
#

ALL_TARGETS= $(PROC_SYNTH_PKGS) proto_list_$(PKGMACH)

$(MAN_INCLUDE)ALL_TARGETS += man_chmod

all: $(ALL_TARGETS) 

# Mercurial does not honor file permissions.  We want all man pages to be mode
# 0444, so we force them that way here, to prevent protocmp warnings later of
# 0444 in the manifest but 0644 in the proto area.
man_chmod: FRC
	$(FIND) $(MAN_SRC)/man* -type f | $(XARGS) $(CHMOD) 0444

#
# This will build the directory to contain the processed manifests
# and the metadata symlinks.
#
$(PDIR):
	@print "info: Creating $(@)"
	$(PKGDEBUG)$(INS.dir)

$(PDIR)/dep-stem-pkgs.txt:
	$(PKGDEBUG)$(RM) dep-stem-pkgs.txt
	$(PKGDEBUG)echo $(DEP_SYNTH_PKGS:%.dep=%) $(DEP_PKGS:%.dep=%) > \
	    dep-stem-pkgs.txt

$(PDIR)/dep-pkgs.txt:
	$(PKGDEBUG)$(RM) dep-pkgs.txt
	$(PKGDEBUG)echo $(DEP_SYNTH_PKGS) $(DEP_PKGS) > dep-pkgs.txt

#
# This rule resolves dependencies across all published manifests.  We use
# subsidiary rules to put the list of deps into files so that when this rule
# fails we don't have to have all of the pkg names spewed out to the log.
#
$(PDIR)/gendeps: $(DEP_SYNTH_PKGS) $(DEP_PKGS) \
    $(PDIR)/dep-pkgs.txt $(PDIR)/dep-stem-pkgs.txt
	@print "info: Resolving dependencies"
	$(PKGDEBUG)if [ "$(SUPPRESSPKGDEP)" = "true" ]; then \
		print "NOTICE: Suppressing dependency resolution " \
		    "(you set SUPPRESSPKGDEP)."; \
		for p in $$(cat dep-stem-pkgs.txt); do \
			$(CP) $$p.dep $$p.res; \
		done; \
	else \
		print "Begin Package Build Errors (if any):"; \
		$(PKGDEPEND) -R / resolve -m $$(cat dep-pkgs.txt) > \
	            pkgdepend-res.out 2>&1 || \
		    ( \
			cat pkgdepend-res.out; \
			print "End Package Build Errors"; \
		       	exit 1; ); \
		for p in $$(cat dep-stem-pkgs.txt); do \
			if [ "$$(print $$p.metadata.*)" = \
			     "$$(print $$p.metadata.noincorp.*)" ]; \
			then \
				print "info: Removing dependency versions" \
			       	    "from $$p"; \
				$(PKGMOGRIFY) $(PKGMOG_VERBOSE) \
				    -O $$p.res -I transforms \
				    strip_versions $$p.dep.res; \
				$(RM) $$p.dep.res; \
			else \
				$(MV) $$p.dep.res $$p.res; \
			fi; \
		done; \
		print "End Package Build Errors"; \
	fi
	$(PKGDEBUG)$(TOUCH) $(@)

#
# This rule runs pkglint across all manifests to be published.
#
# We require that packages be free from lint ERRORs before allowing them
# to be published.  Lint WARNINGs are acceptable (though unpleasant)
# The list of packages is the list of packages that did not resolve to
# 0-byte files, $CHECK_PKGS
#
$(PDIR)/checkmf: $(PDIR)/gendeps $(DEP_SYNTH_PKGS) $(DEP_PKGS)
	@print "info: Checking packages"
	@$(TOUCH) $(PDIR)/pkglint-output.txt;
	-$(PKGDEBUG)for p in $(DEP_SYNTH_PKGS:%.dep=%) $(DEP_PKGS:%.dep=%); do \
		if [ -s $$p.res ]; then \
			CHECK_PKGS="$$p.res $${CHECK_PKGS}"; \
		fi; \
	done; \
	if [ -n "$${CHECK_PKGS}" ]; then \
		$(PKGLINT) -f $(PKGLINTRC) $${CHECK_PKGS} > /dev/null \
		    2> $(PDIR)/pkglint-output.txt; \
	fi;
	$(PKGDEBUG)if $(GREP) ^ERROR $(PDIR)/pkglint-output.txt; then \
		return 1; \
	fi;

install: $(ALL_TARGETS) repository-metadata

repository-metadata: publish_pkgs
	@print "info: Creating repository metadata"
	$(PKGDEBUG)for r in $(REPOS); do \
		/usr/lib/pkg.depotd -d $(PKGDEST)/repo.$$r \
			--add-content --exit-ready; \
	done

#
# Perform a pkglint run on the published repositories.  We remove package
# version information from the lint output so that it is comparable across
# lint runs.
#
lint:
	-$(PKGDEBUG)for r in $(REPOS); do \
		print "info: Running pkglint on the $$r repository"; \
		$(PKGLINT) -f $(LINT_PKGLINTRC) \
		    -l file://$(PKGDEST)/repo.$$r \
		    -c /tmp/pkglint_cache.$$$$ > /dev/null \
		    2> $(PDIR)/pkglint.$$r.lint; \
		$(RM) -rf /tmp/pkglint_cache.$$$$; \
		$(SED) -e 's/@[0-9TZ.:,-]*//g' $(PDIR)/pkglint.$$r.lint; \
	done

#
# Since we create zero-length processed manifests for a graceful abort
# from pkgmogrify, we need to detect that here and make no effort to
# publish the package.
#
# For all other packages, we publish them regardless of status.  We
# derive the target repository as a component of the metadata-derived
# symlink for each package.
#
publish_pkgs: $(REPOS:%=$(PKGDEST)/repo.%) $(PDIR)/checkmf .WAIT $(PUB_PKGS)

#
# Before publishing, we want to pull the license files from $CODEMGR_WS
# into the proto area.  This allows us to NOT pass $SRC (or
# $CODEMGR_WS) as a basedir for publication.
#
$(PUB_PKGS): $$(@:%.pub=%.l)

$(LIC_PAYLOADS): $$(@:%.l=%.mog) stage-licenses 
	@print "info: Generating license payload for $$(basename $(@F) .l)"
	$(PKGDEBUG)$(RM) $@;
	$(PKGDEBUG)$(TOUCH) $@;
	$(PKGDEBUG)hascr=$$(egrep '^cr_Sun$$' $(PDIR)/$(@F)ics || true); \
	tpl=$$(egrep -v '^lic_CDDL$$|^lic_OSBL$$|/lic_OTN$$|^cr_Sun$$' \
		$(PDIR)/$(@F)ics || true); \
	if [ -n "$$hascr" ]; then \
		$(CAT) $(SRC)/pkg/license_files/cr_Sun >> $(@); \
		[ -n "$$tpl" ] && print "" >> $(@); \
	fi; \
	if [[ -n "$$tpl" ]]; then \
		print "This package may include technology" \
			"which may be subject to one" >> $(@); \
		print "or more of the following additional notices:" >> $(@); \
		lnum=1; \
		for t in $$tpl; do \
			print "\n$$lnum)" \
				$$(cat $(PKGROOT)/licenses/$$t.descrip) \
				"\n" >> $@; \
			$(SED) -e 's/^/    /' \
				< $(PKGROOT)/licenses/$$t >> $(@); \
			lnum=$$(($$lnum + 1)); \
		done; \
	fi



#
# Initialize the empty on-disk repositories
#
$(REPOS:%=$(PKGDEST)/repo.%):
	@print "info: Initializing $(@F)"
	$(PKGDEBUG)$(PKGREPO) create file://$(@)
	$(PKGDEBUG)$(PKGREPO) add-publisher -s file://$(@) $(PKGPUBLISHER)

#
# rule to process real manifests
#
# To allow redistributability and package status to change, we must
# remove not only the actual build target (the processed manifest), but
# also the incidental ones (the metadata-derived symlinks).
#
# If pkgmogrify exits cleanly but fails to create the specified output
# file, it means that it encountered an abort directive.  That means
# that this package should not be published for this particular build
# environment.  Since we can't prune such packages from $(PKGS)
# retroactively, we need to create an empty target file to keep make
# from trying to rebuild it every time.  For these empty targets, we
# do not create metadata symlinks.
#
# Automatic dependency resolution to files is also done at this phase of
# processing.  The skipped packages are skipped due to existing bugs
# in pkgdepend.
#
# The incorporation dependency is tricky: it needs to go into all
# current and renamed manifests (ie all incorporated packages), but we
# don't know which those are until after we run pkgmogrify.  So
# instead of expressing it as a transform, we tack it on ex post facto.
#
# Implementation notes:
#
# - The first $(RM) must not match other manifests, or we'll run into
#   race conditions with parallel manifest processing.
#
# - The make macros [ie $(MACRO)] are evaluated when the makefile is
#   read in, and will result in a fixed, macro-expanded rule for each
#   target enumerated in $(PROC_PKGS).
#
# - The shell variables (ie $$VAR) are assigned on the fly, as the rule
#   is executed.  The results may only be referenced in the shell in
#   which they are assigned, so from the perspective of make, all code
#   that needs these variables needs to be part of the same line of
#   code.  Hence the use of command separators and line continuation
#   characters.
#
# - The extract_metadata transforms are designed to spit out shell
#   variable assignments to stdout.  Those are published to the
#   .vars temporary files, and then used as input to the eval
#   statement.  This is done in stages specifically so that pkgmogrify
#   can signal failure if the manifest has a syntactic or other error.
#   The eval statement should begin with the default values, and the
#   output from pkgmogrify (if any) should be in the form of a
#   variable assignment to override those defaults.
#
# - When this rule completes execution, it must leave an updated
#   target file ($@) in place, or make will reprocess the package
#   every time it encounters it as a dependency.  Hence the "touch"
#   statement to ensure that the target is created, even when
#   pkgmogrify encounters an abort in the publish transforms.  This
#   will not cause publication failures when switching build
#   environments, because $(CLOSED_BUILD) and $(OPEN_ONLY) are
#   referenced in $(PKGMOG_DEFINES), and changes will therefore
#   trigger a rebuild for command dependency failure.  (Command
#   dependency checking is turned on by .KEEP_STATE: above.)
#

.SUFFIXES: .mf .mog .dep .res .pub

$(PDIR)/%.mf: manifests/%.mf
	@print "info: Checking and copying manifest $(<F)"
	$(PKGDEBUG)$(RM) $(@)
	$(PKGDEBUG)$(PKGFMT) -c $<
	$(PKGDEBUG)$(PKGMOGRIFY) $(PKGMOG_VERBOSE) $(PM_INC:%= -I %) \
		$(MAN_PKGMOGRIFY_DIRS) $(PKGMOG_DEFINES:%=-D %) \
		-O /dev/null $(<) test_not_defunct || \
		    (print "ERROR: $(<F) is obsolete or renamed;" \
		       "must be moved to ../manifests.defunct."; exit 1)
	$(PKGDEBUG)$(CP) $(<) $(@)

$(PDIR)/%.mf: manifests.defunct/%.mf
	@print "info: Checking and copying manifest $(<F)"
	$(PKGDEBUG)$(RM) $(@)
	$(PKGDEBUG)$(PKGFMT) -c $<
	$(PKGDEBUG)$(PKGMOGRIFY) $(PKGMOG_VERBOSE) $(PM_INC:%= -I %) \
		$(MAN_PKGMOGRIFY_DIRS) $(PKGMOG_DEFINES:%=-D %) \
		-O /dev/null $(<) test_defunct || \
		    (print "ERROR: $(<F) is NOT obsolete or renamed;" \
		       "must be corrected or moved to ../manifests."; exit 1)
	$(PKGDEBUG)$(CP) $(<) $(@)

# Each .mog file depends on the corresponding .man.p5m file (if it exists) in
# the pkg sub-dir of the usr/man gate.
$(MAN_INCLUDE)$(MAN_MANIFESTS:%=$(PDIR)/%.mog):	\
	$(MAN_SRC)/pkg/$$(@F:%.mog=%.man.p5m)

$(PDIR)/%.mog: $(PDIR)/%.mf $(TRANSFORM_PATHS)
	@print "info: Processing manifest $(<F)"
	$(PKGDEBUG)$(RM) $(@) $(@:%.mog=%.nodepend) \
		$(@:%.mog=%.lics) $(@:%.mog=%).metadata.* $(@).vars
	$(PKGDEBUG)$(PKGMOGRIFY) $(PKGMOG_VERBOSE) $(PM_INC:%= -I %) \
		$(MAN_PKGMOGRIFY_DIRS) \
		$(PKGMOG_DEFINES:%=-D %) -P $(@).vars -O $(@) \
		$(<) $(PM_TRANSFORMS)
	$(PKGDEBUG)eval REPO=redist PKGSTAT=current NODEPEND=$(SUPPRESSPKGDEP) \
		`$(CAT) -s $(@).vars`; \
	$(TOUCH) $(@:%.mog=%.lics); \
	if [ -f $(@) ]; then \
		if [ "$$NODEPEND" != "false" ]; then \
			 $(TOUCH) $(@:%.mog=%.nodepend); \
		fi; \
		$(LN) -s $(@F) \
			$(PDIR)/$(@F:%.mog=%).metadata.$$PKGSTAT.$$REPO; \
		if [ \( "$$PKGSTAT" = "current" \) -o \
		     \( "$$PKGSTAT" = "unlockable" \) -o \
		     \( "$$PKGSTAT" = "renamed" \) ]; \
			then print $(PKGDEP_INCORP) >> $(@); \
		fi; \
		if [ -n "$$LICS" ]; then \
			print "license $(@F:%.mog=%.l)" \
				"license=$(@F:%.mog=%.l)" >> $(@); \
			for l in $$LICS; do \
				print $$l >> $(@:%.mog=%.lics); \
			done; \
		fi; \
	else \
		$(TOUCH) $(@); \
	fi
	$(PKGDEBUG)$(RM) $(@).vars

$(PDIR)/%.dep: $(PDIR)/%.mog
	@print "info: Generating dependencies for $(<F)"
	$(PKGDEBUG)$(RM) $(@)
	$(PKGDEBUG)if [ ! -f $(@:%.dep=%.nodepend) ]; then \
		$(PKGDEPEND) generate -m $(PKGDEP_TOKENS:%=-D %) \
			$(PKGPROTODIRS:%=-d %) \
			$(<) > $(@); \
	else \
		$(CP) $(<) $(@); \
	fi

#
# The full chain implies that there should be a .dep.res suffix rule,
# but dependency generation is done on a set of manifests, rather than
# on a per-manifest basis.  Instead, see the gendeps rule above.
#

$(PDIR)/%.pub: $(PDIR)/%.res $(PDIR)/%.l
	$(PKGDEBUG)m=$$(basename $(@:%.pub=%).metadata.*); \
	r=$${m#$(@F:%.pub=%.metadata.)+(?).}; \
	if [ -s $(<) ]; then \
		print "info: Publishing $(@F:%.pub=%) to $$r repository"; \
		$(PKGSEND) -s file://$(PKGDEST)/repo.$$r publish \
		    $(PKGPROTODIRS:%=-d %) \
		    -d $(PDIR) -d $(PKGROOT)/licenses \
		    --fmri-in-manifest --no-index --no-catalog $(<) \
		    > /dev/null; \
	fi; \
	$(TOUCH) $(@);

#
# rule to build the synthetic manifests
#
# This rule necessarily has PKGDEP_TYPE that changes according to
# the specific synthetic manifest.  Rather than escape command
# dependency checking for the real manifest processing, or failing to
# express the (indirect) dependency of synthetic manifests on real
# manifests, we simply split this rule out from the one above.
#
# The implementation notes from the previous rule are applicable
# here, too.
#
$(PROC_SYNTH_PKGS): $(PKGLISTS) $$(@F:%.mog=%.mf) $(TRANSFORM_PATHS)
	@print "info: Processing synthetic manifest $(@F:%.mog=%.mf)"
	$(PKGDEBUG)$(RM) $(@) $(@:%.mog=%.nodepend) \
		$(@:%.mog=%.lics) $(@:%.mog=%).metadata.* $(@).vars
	$(PKGDEBUG)$(PKGMOGRIFY) $(PKGMOG_VERBOSE) -I transforms -I $(PDIR) \
		$(PKGMOG_DEFINES:%=-D %) -D PKGDEP_TYPE=$(PKGDEP_TYPE) \
		-P $(@).vars -O $(@) $(@F:%.mog=%.mf) \
		$(PM_TRANSFORMS) synthetic
	$(PKGDEBUG)eval REPO=redist PKGSTAT=current `$(CAT) -s $(@).vars`; \
	$(TOUCH) $(@:%.mog=%.lics); \
	if [ -f $(@) ]; then \
		$(LN) -s $(@F) \
			$(PDIR)/$(@F:%.mog=%).metadata.$$PKGSTAT.$$REPO; \
		if [ -n "$$LICS" ]; then \
			for l in $$LICS; do \
				print $$l >> $(@:%.mog=%.lics); \
			done; \
		fi; \
	else \
		$(TOUCH) $(@); \
	fi
	$(PKGDEBUG)$(RM) $(@).vars

$(DEP_SYNTH_PKGS): $$(@:%.dep=%.mog)
	@print "info: Skipping dependency generation for $(@F:%.dep=%)"
	$(PKGDEBUG)$(CP) $(@:%.dep=%.mog) $(@)

clean:

clobber: clean
	$(RM) -r $(CLOBBERFILES)

#
# This rule assumes that all links in the $PKGSTAT directories
# point to valid manifests, and will fail the make run if one
# does not contain an fmri.
#
# We do this in the BEGIN action instead of using pattern matching
# because we expect the fmri to be at or near the first line of each input
# file, and this way lets us avoid reading the rest of the file after we
# find what we need.
#
# We keep track of a failure to locate an fmri, so we can fail the
# make run, but we still attempt to process each package in the
# repo/pkgstat-specific subdir, in hopes of maybe giving some
# additional useful info.
#
# The protolist is used for bfu archive creation, which may be invoked
# interactively by the user.  Both protolist and PKGLISTS targets
# depend on $(PROC_PKGS), but protolist builds them recursively.
# To avoid collisions, we insert protolist into the dependency chain
# here.  This has two somewhat subtle benefits: it allows bfu archive
# creation to work correctly, even when -a was not part of NIGHTLY_OPTIONS,
# and it ensures that a protolist file here will always correspond to the
# contents of the processed manifests, which can vary depending on build
# environment.
#
# For unlockable packages, we want a facet tag with neither the version nor
# the "pkg:/" at the beginning of the FMRI; hence the split(,,"@") below
# (to remove the version) and the substr(,6) (to remove the prefix).
#
$(PKGLISTS): $(PROC_PKGS)
	$(PKGDEBUG)sdotr=$(@F:packages.%=%); \
	r=$${sdotr%.+(?)}; s=$${sdotr#+(?).}; \
	print "info: Generating $$r $$s package list"; \
	$(RM) $(@); $(TOUCH) $(@); \
	$(NAWK) -v s=$$s 'BEGIN { \
		if (ARGC < 2) { \
			exit; \
		} \
		retcode = 0; \
		for (i = 1; i < ARGC; i++) { \
			do { \
				e = getline f < ARGV[i]; \
			} while ((e == 1) && (f !~ /name=pkg.fmri/)); \
			close(ARGV[i]); \
			if (e == 1) { \
				l = split(f, a, "="); \
				if (s == "unlockable") { \
					split(a[l], aa, "@"); \
					unlock_arg = substr(aa[1], 6); \
					facet_txt = " facet.version-lock." unlock_arg "=true" ; \
				} \
				print "depend fmri=" a[l], \
					"type=$$(PKGDEP_TYPE)" facet_txt; \
			} else { \
				print "no fmri in " ARGV[i] >> "/dev/stderr"; \
				retcode = 2; \
			} \
		} \
		exit retcode; \
	}' `find $(PDIR) -type l -a \( $(PKGS:%=-name %.metadata.$$s.$$r -o) \
		-name NOSUCHFILE \)` >> $(@)

#
# rules to validate proto area against manifests, check for safe
# file permission modes, and generate a faux proto list
#
# For the check targets, the dependencies on $(PROC_PKGS) is specified
# as a subordinate make process in order to suppress output.
#
makesilent:
	@$(MAKE) -e $(PROC_PKGS) $(PROC_SYNTH_PKGS) PKGMACH=$(PKGMACH) \
		SUPPRESSPKGDEP=$(SUPPRESSPKGDEP) > /dev/null

#
# The .lics files were created during pkgmogrification, and list the
# set of licenses to pull from $SRC for each package.  Because
# licenses may be duplicated between packages, we uniquify them as
# well as aggregating them here.
#
license-list: makesilent
	@print "info: Listing third party licenses"
	$(PKGDEBUG)$(CAT) $(PROC_PKGS:%.mog=%.lics) \
		$(PROC_SYNTH_PKGS:%.mog=%.lics) | \
		egrep -v '^lic_CDDL$$|^lic_OSBL$$|^cr_Sun$$' | sort -u > $@

#
# Staging the license and description files in the proto area allows
# us to do proper unreferenced file checking of both license and
# description files without blanket exceptions, and to pull license
# content without reference to $CODEMGR_WS during publication.
#
$(PKGDEBUG:%=$(POUND_SIGN))DOSILENT= $(POUND_SIGN)
$(DOSILENT)SILENTOPT= -s
stage-licenses: license-list FRC
	@print "info: Staging licenses in proto area"
	$(PKGDEBUG)if [ -s license-list ]; then \
		$(MAKE) -e $(SILENTOPT) -f Makefile.lic \
			LICROOT=$(PKGROOT)/licenses \
			`$(NAWK) '{ \
				print "$(PKGROOT)/licenses/" $$0; \
				print "$(PKGROOT)/licenses/" $$0 ".descrip"; \
			}' license-list`; \
	fi

protocmp: makesilent
	@$(VALIDATE_PKG) -a $(PKGMACH) -v \
		$(EXCEPTIONS:%=-e $(CODEMGR_WS)/exception_lists/%) \
		-m $(PDIR) $(PKGPROTODIRS:%=-p %)

pmodes: makesilent
	@$(VALIDATE_PKG) -a $(PKGMACH) -M -m $(PDIR) \
		-e $(CODEMGR_WS)/exception_lists/pmodes 

check: protocmp pmodes

protolist: proto_list_$(PKGMACH)

proto_list_$(PKGMACH): $(PROC_PKGS)
	@$(VALIDATE_PKG) -a $(PKGMACH) -L -m $(PDIR) > $(@)

$(PROC_PKGS): $(PDIR)

#
# This is a convenience target to allow package names to function as
# build targets.  Generally, using it is only useful when iterating on
# development of a manifest.
#
# When processing a manifest, use the basename (without extension) of
# the package.  When publishing, use the basename with a ".pub"
# extension.
#
# Other than during manifest development, the preferred usage is to
# avoid these targets and override PKGS on the make command line and
# use the provided all and install targets.
#
$(PKGS) $(SYNTH_PKGS): $(PDIR)/$$(@:%=%.mog)

$(PKGS:%=%.pub) $(SYNTH_PKGS:%=%.pub): $(PDIR)/$$(@)

#
# This is a convenience target to resolve dependencies without publishing
# packages.
#
gendeps: $(PDIR)/gendeps

#
# This is a convenience target to check manifests without publishing
# packages.
#
checkmf: $(PDIR)/checkmf

#
# These are convenience targets for cross-platform packaging.  If you
# want to build any of "the normal" targets for a different
# architecture, simply use "arch/target" as your build target.
#
# Since the most common use case for this is "install," the architecture
# specific install targets have been further abbreviated to elide "/install."
#
i386/% sparc/%:
	$(MAKE) -e $(@F) PKGMACH=$(@D) SUPPRESSPKGDEP=$(SUPPRESSPKGDEP)

i386 sparc: $$(@)/install

FRC:

# EXPORT DELETE START

#
# In order to drop a manifest during the export source build, insert the 
# tag: "# EXPORT DELETE FILE" on a line by itself in the manifest you
# wish to drop.
#
XMOD.cmd=	egrep -l '^\# EXPORT DELETE FILE' \
	manifests/*.mf manifests.defunct/*.mf
XMOD_PKGS=	$(XMOD.cmd:sh)

EXPORT_SRC: CRYPT_SRC
	# Delete package manifests which must be dropped.
	$(RM) $(XMOD_PKGS)
	# Rewrite this makefile with the EXPORT section removed.
	$(RM) Makefile.tmp
	$(SED) -e "/^# EXPORT DELETE START/,/^# EXPORT DELETE END/d" \
		< Makefile > Makefile.tmp
	$(MV) -f Makefile.tmp Makefile
	$(CHMOD) 444 Makefile

CRYPTOMFST=manifests/driver-crypto-dprov.mf

CRYPT_SRC:
	$(RM) $(CRYPTOMFST).tmp
	$(SED) -e "/^# CRYPT DELETE START/,/^# CRYPT DELETE END/d" \
		< $(CRYPTOMFST) > $(CRYPTOMFST).tmp
	$(MV) -f $(CRYPTOMFST).tmp $(CRYPTOMFST)
	$(CHMOD) 444 $(CRYPTOMFST)

# EXPORT DELETE END
