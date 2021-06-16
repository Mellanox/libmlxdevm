AC_PREREQ([2.69])
AC_INIT([libmlxdevm], [1.0.0], [support@nvidia.com])
AC_CONFIG_HEADERS([config.h])

if test -z "$CFLAGS" ; then
    CFLAGS="-O2 -g"
fi

AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_PROG_LIBTOOL

# Init libtool
LT_INIT([shared disable-static])

if test -n "$ghprbPullId" ; then
    AC_SUBST([PACKAGE_REVISION], [$(echo pr${ghprbPullId})])
else
    AC_SUBST([PACKAGE_REVISION], [$(echo ${BUILD_NUMBER:-1})])
fi
AC_SUBST([GIT_BRANCH], [$(git rev-parse --abbrev-ref HEAD)])
AC_SUBST([GIT_SHA1], [$(git rev-parse HEAD |cut -c -8)])
dateopt=""
if test -n "$SOURCE_DATE_EPOCH" ; then
    dateopt="-u -d @$SOURCE_DATE_EPOCH"
fi
AC_SUBST([BUILD_DATE_CHANGELOG], [$(date $dateopt +'%a, %d %b %Y %T %z')])

# Use C and C++
AC_LANG([C])
AC_LANG([C++])

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL

BASE_CFLAGS="-pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions \
-fstack-protector-strong --param=ssp-buffer-size=4 -grecord-gcc-switches"
AC_SUBST([BASE_CFLAGS])

# Checks for header files.
AC_CHECK_HEADER([stdlib.h])

AC_SEARCH_LIBS([pthread_create],[pthread])

PKG_CHECK_MODULES([libmnl], [libmnl >= 1.0], [mnl=1], [mnl=0])
if test "$mnl" = 0;
then
	echo "*** Error: No suitable libmnl-devel found. ***"
	echo "    Please install the 'libmnl-devel' package"
	exit 1
else
	echo "*** libmnl-devel found. ***"
fi

AC_PREFIX_DEFAULT(/usr/local)

AC_CONFIG_FILES([Makefile
		 debian/changelog
		 libmlxdevm.spec])

AC_OUTPUT
