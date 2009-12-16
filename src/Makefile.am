wpeditorincludedir=$(includedir)/$(PACKAGE_INC)
wpeditorinclude_HEADERS = \
	wptextbuffer.h \
	wptextview.h \
	gtksourceiter.h

wpeditor_LTLIBRARIES = libwpeditor.la
wpeditordir = $(libdir)

libwpeditor_la_SOURCES = \
	wptextbuffer.c \
	wptextbuffer.h \
	wptextbuffer-private.h \
	wptextview.c \
	wptextview.h \
	wpundo.c \
	wpundo.h \
	wphtmlparser.c \
	wphtmlparser.h \
	color_buffer.c \
	color_buffer.h \
	gtksourceiter.h \
	gtksourceiter.c

libwpeditor_la_CFLAGS = $(PACKAGE_CFLAGS) -DMAEMO_CHANGES
libwpeditor_la_LIBADD = $(PACKAGE_LIBS)

