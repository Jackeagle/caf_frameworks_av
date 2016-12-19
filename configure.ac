dnl required version of autoconf
AC_PREREQ([2.53])

AC_INIT([av-amrwbenc], [1.0.0])

AC_CONFIG_SRCDIR([media/libstagefright/codecs/amrwbenc/src/voAMRWBEnc.c])
AC_CONFIG_HEADERS([config.h])

dnl required version of automake
AM_INIT_AUTOMAKE([-Wno-portability 1.11 no-dist-gzip dist-xz tar-ustar subdir-objects]) 

dnl enable mainainer mode by default
AM_MAINTAINER_MODE([enable])

dnl check for tools (compiler etc.)
AC_PROG_CC
AM_PROG_AS

dnl determine c++ compiler
AC_PROG_CXX
dnl determine if c++ is available on this system
AC_CHECK_PROG(HAVE_CXX, $CXX, yes, no)

dnl determine c++ preprocessor
dnl FIXME: do we need this ?
# AC_PROG_CXXCPP

dnl required version of libtool
LT_PREREQ([2.2.6])
LT_INIT([disable-static])
# LT_INIT([disable-shared])
LT_LANG([C++])
AC_SUBST([LIBTOOL_DEPS])
LT_OUTPUT 

dnl give error and exit if we don't have pkgconfig
AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, [ ], [
  AC_MSG_ERROR([You need to have pkg-config installed!])
])

# Checks for libraries.
AC_CHECK_LIB([m], [main])

# Checks for header files.
AC_CHECK_HEADERS([stdint.h stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memset])

# Various options for configure
AC_ARG_ENABLE([compile-c],
              [AS_HELP_STRING([--enable-compile-c],
              [enable compiling as C program (default is yes)])],
 							[compile_as_c=$enableval],
 							[compile_as_c=yes])
AC_ARG_ENABLE([gcc-armv5],
            	[AS_HELP_STRING([--enable-gcc-armv5],
              [enable GCC specific ARMv5 assembler (default is no)])],
            	[gcc_armv5=$enableval],
            	[gcc_armv5=no])
AC_ARG_ENABLE([gcc-armv7-neon],
            	[AS_HELP_STRING([--enable-gcc-armv7-neon],
              [enable GCC specific ARMv7 Neon assembler (default is no)])],
            	[gcc_armv7_neon=$enableval],
            	[gcc_armv7_neon=no])
            	
# Automake conditionals to set
AM_CONDITIONAL(COMPILE_AS_C, test x$compile_as_c = xyes)
AM_CONDITIONAL(GCC_ARMV5, test x$gcc_armv5 = xyes)
AM_CONDITIONAL(GCC_ARMV7_NEON, test x$gcc_armv7_neon = xyes)

# Google Frameworks AV CODEC AMRWBENC soname version to use
# goes by ‘current[:revision[:age]]’ with the soname ending up as
# current.age.revision.
AV_CODEC_AMRWBENC_VERSION=0:3:0
AC_SUBST(AV_CODEC_AMRWBENC_VERSION)

AC_CONFIG_FILES([Makefile media/libstagefright/codecs/amrwbenc/Makefile])
AC_OUTPUT
