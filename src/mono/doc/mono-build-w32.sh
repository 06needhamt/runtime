#!/bin/bash

# Script to automate the building of mono and its dependencies on
# cygwin.  Relies on wget being installed (could make it fall back to
# using lynx, links, w3, curl etc), assumes that gcc, make, tar,
# automake, etc are already installed too (may be worth testing for
# all that right at the top and bailing out if missing/too old/too new
# etc).


# See where we are.  This will become the top level directory for the
# installation, unless we are given an alternative location
here=$1
test -z "$here" && here=`pwd`

echo "Building Mono and dependencies in $here, installing to $here/install"

PATH=$here/install/bin:$here/install/lib:$PATH

# Check mono out first, so we can run aclocal from inside the mono dir (it
# needs to see which version of the real aclocal to run)
test -z "$CVSROOT" && CVSROOT=:pserver:anonymous@reypastor.hispalinux.es:/mono
export CVSROOT

echo "Updating mono"

# cvs checkout does the same as cvs update, except that it copes with
# new modules being added

# Older versions of cvs insist on a cvs login for :pserver: methods
# Make sure cvs is using ssh for :ext: methods

if [ ${CVSROOT:0:5} = ":ext:" ]; then
    CVS_RSH=ssh
    export CVS_RSH
elif [ ${CVSROOT:0:9} = ":pserver:" ]; then
    if ! grep $CVSROOT ~/.cvspass > /dev/null 2>&1 ; then
	echo "Logging into CVS server.  Anonymous CVS password is probably empty"
	cvs login || exit -1
    fi
fi

cvs checkout mono || exit -1

# Need to install pkgconfig and set ACLOCAL_FLAGS if there is not a
# pkgconfig installed already.  Otherwise set PKG_CONFIG_PATH to the
# glib we're about to install in $here/install.  This script could
# attempt to be clever and see if glib 2 is already installed, too.


# --print-ac-dir was added in 1.2h according to the ChangeLog.  This
# should mean that any automake new enough for us has it.

# This sets ACLOCAL_FLAGS to point to the freshly installed pkgconfig
# if it doesnt already exist on the system (otherwise auto* breaks if
# it finds two copies of the m4 macros).  The GIMP for Windows
# pkgconfig sets its prefix based on the location of its binary, so we
# dont need PKG_CONFIG_PATH (the internal pkgconfig config file
# $prefix is handled similarly).

if [ ! -f `(cd mono; aclocal --print-ac-dir)`/pkg.m4 ]; then
    ACLOCAL_FLAGS="-I $here/install/share/aclocal $ACLOCAL_FLAGS"
fi

export PATH
export ACLOCAL_FLAGS

# Grab pkg-config, glib etc
if [ ! -d $here/install ]; then
    mkdir $here/install || exit -1

    # Fetch and install pkg-config, glib, iconv, intl
    for i in pkgconfig-0.80-tml-20020101.zip glib-1.3.12-20020101.zip glib-dev-1.3.12-20020101.zip libiconv-1.7.zip libiconv-dev-1.7.zip libintl-0.10.40-20020101.zip
    do
	wget http://www.go-mono.org/archive/$i
	(cd $here/install || exit -1; unzip -o ../$i || exit -1) || exit -1
    done
fi

# Needed to find the libiconv bits
CPPFLAGS="-I$here/install/include"
LDFLAGS="-L$here/install/lib"
export CPPFLAGS
export LDFLAGS

# Make sure we build native w32, not cygwin
CC="gcc -mno-cygwin"
export CC

# --prefix is used to set the class library dir in mono, and it needs
# to be in windows-native form.  It also needs to have '\' turned into
# '/' to avoid quoting issues during the build.
prefix=`cygpath -w $here/install | sed -e 's@\\\\@/@g'`

# Build and install mono
echo "Building and installing mono"

(cd $here/mono; ./autogen.sh --prefix=$prefix || exit -1; make || exit -1; make install || exit -1) || exit -1


echo ""
echo ""
echo "All done."
echo "Add $here/install/bin and $here/install/lib to \$PATH"
echo "Don't forget to copy the class libraries to $here/install/lib"

