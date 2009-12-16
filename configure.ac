AC_INIT([wpeditor], [0.1])
AM_INIT_AUTOMAKE([foreign])
AM_MAINTAINER_MODE
AM_CONFIG_HEADER([config.h])

AC_ENABLE_SHARED(yes)
AC_ENABLE_STATIC(no)
AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_STDC
AC_HEADER_STDC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AM_PROG_LIBTOOL


PKG_CHECK_MODULES(PACKAGE, [gtk+-2.0 >= 2.0.0 glib-2.0 >= 2.0.0])
AC_SUBST(PACKAGE_CFLAGS)
AC_SUBST(PACKAGE_LIBS)

PACKAGE_INC=wpeditor
AC_SUBST(PACKAGE_INC)
AC_DEFINE_UNQUOTED(PACKAGE_INC,"$PACKAGE_INC",[Name of the include directory])

dnl Set PACKAGE_LOCALE_DIR in config.h
DATADIRNAME="share"
if test "x${prefix}" = "xNONE"; then
  AC_DEFINE_UNQUOTED([PACKAGE_LOCALE_DIR], ["${ac_default_prefix}/${DATADIRNAME}/locale"],[Locale directory])
else
  AC_DEFINE_UNQUOTED([PACKAGE_LOCALE_DIR], ["${prefix}/${DATADIRNAME}/locale"], [Locale directory])
fi
AC_SUBST(PACKAGE_LOCALE_DIR)

AC_CHECK_LIB([gtk-x11-2.0], [hildon_gtk_im_context_filter_event], 
	AC_DEFINE(HAVE_HILDON, 1, [Define to 1 if you have hildon_gtk_im_context_filter_event]),,)

AC_OUTPUT([
Makefile
src/Makefile
wpeditor.pc
debian/wpeditor-dev.install
])
