AC_PREREQ(2.61)

AC_INIT([frameworks-av],1.0.0)

AM_INIT_AUTOMAKE([-Werror -Wall gnu foreign subdir-objects])

AM_MAINTAINER_MODE

AC_CONFIG_HEADER([configure.h])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AM_PROG_AS
AM_PROG_AR
AC_PROG_CC
AC_PROG_CXX
AM_PROG_CC_C_O
AC_PROG_LIBTOOL
AC_PROG_AWK
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

AC_CHECK_TOOL(OBJCOPY, objcopy, false)

DEBUG_CPPFLAGS=
DEBUG_CFLAGS=
DEBUG_LDFLAGS=

AC_ARG_WITH([sanitized-headers],
	[AS_HELP_STRING([--with-sanitized-headers=DIR],
	[location of the sanitized Linux kernel headers])],
	[CPPFLAGS="$CPPFLAGS -I$withval"])

AC_ARG_WITH([additional-include-directives],
        [AS_HELP_STRING([--with-additional-include-directives=DIR],
	[additional -I include paths for the C pre-processor])],
        [CPPFLAGS="$CPPFLAGS -I$withval"])


AC_OUTPUT([ \
  Makefile \
  camera/Makefile \
  media/mtp/Makefile \
])

