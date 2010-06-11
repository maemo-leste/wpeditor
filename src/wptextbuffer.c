/**
 * @file wptextbuffer.c
 *
 * Implementation file for WordPad Text Buffer
 */

/* 
 * Osso Notes
 * Copyright (c) 2005-06 Nokia Corporation. All rights reserved.
 * Contact: Ouyang Qi <qi.ouyang@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Initial developer(s): Zsolt Simon
 */

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include "wptextbuffer.h"
#include "color_buffer.h"
#include "wptextbuffer-private.h"
#include "wpundo.h"
#include "wphtmlparser.h"

#define WPT_ID "wpt-id"

#define MIN_FONT_SCALE 0.1
#define MAX_FONT_SCALE 5
#define DEF_FONT_SCALE 1.5
#define DEF_FONT "Sans"
#define DEF_FONT_SIZE 3
#define DEF_PLAIN_FONT "Monospace"
#define DEF_PLAIN_FONT_SIZE 3

const gint wp_font_size[] = { 6, 8, 10, 12, 16, 24, 32 };

/** The object's private variables */
struct _WPTextBufferPrivate {
    /** <b>TRUE</b> if the buffer hold a selection */
    gint has_selection:1;
    /** <b>TRUE</b> if the buffer is empty */
    gint is_empty:1;
    /** <b>TRUE</b> if the buffer contains rich text */
    gint is_rich_text:1;

    /** <b>TRUE</b> if the tags should be copied automatically at insert */
    gint insert_preserve_tags:1;
    /** <b>TRUE</b> if the buffer buffer is in fast mode. In this mode the
     * insert, delete, apply_tag, remove_tag are skipped */
    gint fast_mode:1;

    /** > 0 if the cursor is frozen, and the refresh_attributes signal is not 
     * emmited */
    gint cursor_moved_frozen;
    /** <b>TRUE</b> if the cursor has changed it's location */
    gint cursor_moved:1;
    /** offset of the last cursor position */
    gint last_cursor_pos;

    /** Font scaling factor */
    gdouble font_scaling_factor;

    /** Background color */
    GdkColor *background_color;

    /** Holds the current attributes, default attributes for rich text, and
     * default attributes for plain text */
    WPTextBufferFormat fmt, default_fmt, default_plain_fmt;

    /** Pointer to the undo object */
    WPUndo *undo;

    /** Undo reset is queued for next end user action */
    gboolean queue_undo_reset;

    /** Holds the deleted tags, when a selection was deleted */
    GSList *delete_tags;
    /** Last line justification from the deleted text */
    gint delete_last_line_justification;

    /** #GtkTextTag array of tags less then WPT_LASTTAG */
    GtkTextTag *tags[WPT_LASTTAG];
    /** Pointer to the ColorBuffer */
    ColorBuffer *color_tags;
    /** #GtkTextTag array of font size tags */
    GtkTextTag *font_size_tags[WP_FONT_SIZE_COUNT];
    /** #GtkTextTag array of superscript tags */
    GtkTextTag *font_size_sup_tags[WP_FONT_SIZE_COUNT];
    /** #GtkTextTag array of subscript tags */
    GtkTextTag *font_size_sub_tags[WP_FONT_SIZE_COUNT];

    /** #GtkTextTag array of font face tags */
    GtkTextTag **fonts;

    /** Idle id, used to emit refresh_attributes signal */
    gint source_refresh_attributes;

    /** Last line justification */
    gint last_line_justification;

    /** Temporarly start and end of the removed justification tag */
    gint just_start, just_end;
    /** Temporarly removed justification #GtkTextTag */
    GtkTextTag *tmp_just;

    /** Buffer to hold the last invalid utf8 character used at plain file opening */
    gchar last_utf8_invalid_char[13];
    /** The saved incomplete utf8 character size */
    gint last_utf8_size;

    /** Pointer to the #WPHTMLParser object */
    WPHTMLParser *parser;

    /** Tag rememberence status. It is needed for special IM cases */
    gint remember_tag:1;

    /** True is insert was the last operation */
    gint last_is_insert:1;
    gint force_copy:1;
    gint convert_tag:1;
    GSList *copy_insert_tags;
    GtkTextIter copy_start, copy_end;
    GHashTable *tag_hash;
};

/** HTML tag types */
typedef enum {
    TP_FONTNAME = 0,
    TP_FONTSIZE,
    TP_FONTCOLOR,
    TP_BOLD,
    TP_UNDERLINE,
    TP_ITALIC,
    TP_STRIKE,
    TP_SUBSCRIPT,
    TP_SUPERSCRIPT,
    TP_LAST
} HTMLTag;

/** HTML opening tags */
const gchar *html_open_tags[TP_LAST] = { "<font face=\"%s\">",
    "<font size=%d>",
    "<font color=\"#%02x%02x%02x\">",
    "<b>",
    "<u>",
    "<i>",
    "<s>",
    "<sub>",
    "<sup>"
};

/** HTML closing tags */
const gchar *html_close_tags[TP_LAST] = { "</font>",
    "</font>",
    "</font>",
    "</b>",
    "</u>",
    "</i>",
    "</s>",
    "</sub>",
    "</sup>"
};

/** HTML header */
const gchar *html_header =
    "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">\n"
    "<html><head>\n"
    "    <meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n"
    "    <meta name=\"generator\" content=\"Osso Notes\">\n"
    "    <title></title>" "</head>\n";
const gchar *body_start = "<body>\n";
const gchar *body_bgcolor_start = "<body bgcolor=\"%s\">\n";
/** HTML footer */
const gchar *html_footer = "</body>\n" "</html>\n";

static GObject *wp_text_buffer_constructor(GType type,
                                           guint n_construct_properties,
                                           GObjectConstructParam *
                                           construct_param);

static void wp_text_buffer_finalize(GObject * object);

static void wp_text_buffer_set_property(GObject * object,
                                        guint prop_id,
                                        const GValue * value,
                                        GParamSpec * pspec);
static void wp_text_buffer_get_property(GObject * object,
                                        guint prop_id,
                                        GValue * value, GParamSpec * pspec);

/**
 * Callback to notify when a mark has changed it's position. The selection and
 * cursor position is intrested only
 * @param buffer is a #GtkTextBuffer
 * @param iter a position in the buffer
 * @param mark the #GtkTextMark which has been set
 */
static void wp_text_buffer_mark_set(GtkTextBuffer * buffer,
                                    const GtkTextIter * iter,
                                    GtkTextMark * mark);

static void wp_text_buffer_check_apply_tag(WPTextBuffer * buffer);

/**
 * Callback to insert <i>text</i> into the <i>buffer</i> at the given <i>pos</i>
 * @param buffer is a #GtkTextBuffer
 * @param pos a position in the buffer
 * @param text contains the text to be inserted
 * @param length contains the length of the <i>text</i>
 */
static void wp_text_buffer_insert_text(GtkTextBuffer * buffer,
                                       GtkTextIter * pos,
                                       const gchar * text, gint length);
/**
 * Callback to apply a <i>tag</i> in the <i>buffer</i> between the <i>start</i>
 * and <i>end</i> interval
 * @param buffer is a #GtkTextBuffer
 * @param tag is a #GtkTextTag
 * @param start a position in the buffer
 * @param end a position in the buffer
 */
static void wp_text_buffer_apply_tag(GtkTextBuffer * buffer,
                                     GtkTextTag * tag,
                                     const GtkTextIter * start,
                                     const GtkTextIter * end);
/**
 * Callback to remove a <i>tag</i> in the <i>buffer</i> between the <i>start</i>
 * and <i>end</i> interval
 * @param buffer is a #GtkTextBuffer
 * @param tag is a #GtkTextTag
 * @param start a position in the buffer
 * @param end a position in the buffer
 */
static void wp_text_buffer_remove_tag(GtkTextBuffer * buffer,
                                      GtkTextTag * tag,
                                      const GtkTextIter * start,
                                      const GtkTextIter * end);
/**
 * Callback to delete the text in the <i>buffer</i> between the <i>start</i>
 * and <i>end</i> interval
 * @param buffer is a #GtkTextBuffer
 * @param start a position in the buffer
 * @param end a position in the buffer
 */
static void wp_text_buffer_delete_range(GtkTextBuffer * buffer,
                                        GtkTextIter * start,
                                        GtkTextIter * end);
/**
 * Callback to notify a new group has been started in the <i>buffer</i>. It is
 * used to group undo/redo functionality
 * @param buffer is a #GtkTextBuffer
 */
static void wp_text_buffer_begin_user_action(GtkTextBuffer * buffer);

/**
 * Callback to notify a the group action has been ended
 * @param buffer is a #GtkTextBuffer
 */
static void wp_text_buffer_end_user_action(GtkTextBuffer * buffer);

/**
 * Send the refresh_attributes signal only if the last cursor position differs
 * from the position pointer by <i>iter</i>
 * @param buffer is a #WPTextBuffer
 * @param a position in the buffer
 */
static void emit_refresh_attributes(WPTextBuffer * buffer,
                                    const GtkTextIter * iter);
/**
 * Send the default_font_changed signal
 * @param buffer is a #WPTextBuffer
 */
static void emit_default_font_changed(WPTextBuffer * buffer);

/**
 * Send the default_justification_changed signal, only if the <i>justification</i>
 * is different from the old one, and updates the old justification.
 * @param buffer is a #WPTextBuffer
 * @param justification is on of the #GTK_JUSTIFY_LEFT, #GTK_JUSTIFY_CENTER,
 *                      #GTK_JUSTIFY_RIGHT
 */
static void emit_default_justification_changed(WPTextBuffer * buffer,
                                               gint justification);

/**
 * Freeze the cursor movement. If the cursor is frozen there will be no emition of
 * refresh_attributes signal.
 * @param buffer is a #WPTextBuffer
 */
static void freeze_cursor_moved(WPTextBuffer * buffer);
/**
 * Unfreeze the cursor movement. If there was cursor movement while the cursor
 * was frozen, a refresh_attributes signal will be emited
 * @param buffer is a #WPTextBuffer
 */
static void thaw_cursor_moved(WPTextBuffer * buffer);

/**
 * Initialize the #GtkTextTag's used by the buffer
 * @param buffer is a #WPTextBuffer
 */
static void wp_text_buffer_init_tags(WPTextBuffer * buffer);

/**
 * Copies the <i>tags</i> in the <i>buffer</i> only if the 
 * <i>buffer->priv->fmt->cs</i> doesn't contain them
 * @param buffer is a #GtkTextBuffer
 * @param tags is a #GSList of #GtkTextTag's
 * @param start a position in the buffer
 * @param end a position in the buffer
 */
static void wp_text_buffer_copy_tag_attributes(WPTextBuffer * buffer,
                                               GSList * tags,
                                               GtkTextIter * start,
                                               GtkTextIter * end);
/**
 * Apply <i>fmt</i> to the <i>buffer</i> between <i>start</i> and <i>end</i>
 * interval
 * @param buffer is a #GtkTextBuffer
 * @param start a position in the buffer
 * @param end a position in the buffer
 * @param undo is <b>TRUE</b> if the operations should be saved in undo
 * @param fmt is a #WPTextBufferFormat which contains the formatting tags. A tag
 *              is applied/removed only if is present in the changeset (fmt.cs)
 * @return <b>TRUE</b> if a tag was applied
 */
static gboolean wp_text_buffer_apply_attributes(WPTextBuffer * buffer,
                                                GtkTextIter * start,
                                                GtkTextIter * end,
                                                gboolean undo,
                                                WPTextBufferFormat * fmt);
/**
 * Get the formatting attributes from the text.
 * @param buffer pointer to a #WPTextBuffer
 * @param pointer to a #WPTextBufferFormat which will be filled with the current
 *                attributes from the cursor position
 * @param set_changed is <b>TRUE</b> if the fmt->cs need to be cleared
 * @param parse_selection means that the selection should be parsed, to detect
 *                if the tag is toggled multiple times. If it is toggled multiple
 *                times, then the fmt->cs.* will be set accordingly.
 */
static gboolean _wp_text_buffer_get_attributes(WPTextBuffer * buffer,
                                               WPTextBufferFormat * fmt,
                                               gboolean set_changed,
                                               gboolean parse_selection);
/**
 * Put bullets in front of the selected area or the actual line
 * @param buffer pointer to a #WPTextBuffer
 */
static void _wp_text_buffer_put_bullet(WPTextBuffer * buffer);

/**
 * Remove bullets from the front of the selected area or the actual line
 * @param buffer pointer to a #WPTextBuffer
 */
static void _wp_text_buffer_remove_bullet(WPTextBuffer * buffer);

/**
 * Callback called when the undo's object redo state has been changed
 * @param undo pointer to a #WPUndo
 * @param enable set if redo is enabled
 * @param buffer pointer to a #WPTextBuffer
 */
static void wp_text_buffer_can_redo_cb(WPUndo * undo, gboolean enable,
                                       gpointer buffer);
/**
 * Callback called when the undo's object undo state has been changed
 * @param undo pointer to a #WPUndo
 * @param enable set if undo is enabled
 * @param buffer pointer to a #WPTextBuffer
 */
static void wp_text_buffer_can_undo_cb(WPUndo * undo, gboolean enable,
                                       gpointer buffer);
/**
 * Callback called when the undo's object changed the format of the buffer
 * @param undo pointer to a #WPUndo
 * @param rich_text set if the buffer contains rich text
 * @param buffer pointer to a #WPTextBuffer
 */
static void wp_text_buffer_format_changed_cb(WPUndo * undo,
                                             gboolean rich_text,
                                             WPTextBuffer * buffer);
/**
 * Callback called when the undo's object last line justification changed
 * @param undo pointer to a #WPUndo
 * @param last_line_justification contains the new last line justification
 * @param buffer pointer to a #WPTextBuffer
 */
static void wp_text_buffer_last_line_justify_cb(WPUndo * undo,
                                                gint last_line_justification,
                                                WPTextBuffer * buffer);
/**
 * Callback called when undo's object doesn't have enough memory for the current
 * operation.
 * @param undo pointer to a #WPUndo
 * @param last_line_justification contains the new last line justification
 * @param buffer pointer to a #WPTextBuffer
 */
static void wp_text_buffer_no_memory_cb(WPUndo * undo, WPTextBuffer * buffer);

/**
 * Resize the fonts to the new scaling factor.
 * @param buffer pointer to a #WPTextBuffer
 */
static void wp_text_buffer_resize_font(WPTextBuffer * buffer);

/**
 * Marks the user action to reset the buffer
 */
static void wp_text_buffer_insert_pixbuf (GtkTextBuffer *buffer,
					  GtkTextIter *location,
					  GdkPixbuf *pixbuf);

/** Signals */
enum {
    /** Sent when the attributes need to be refreshed */
    REFRESH_ATTRIBUTES,
    /** Sent when redo state has changed */
    CAN_REDO,
    /** Sent when undo state has changed */
    CAN_UNDO,
    /** Sent when formatting has changed (rich text<->plain text) */
    FMT_CHANGED,
    /** Sent when the default font has been changed */
    DEF_FONT_CHANGED,
    /** Sent when the default justification has been changed */
    DEF_JUSTIFICATION_CHANGED,
    /** Sent when background color has been changed */
    BACKGROUND_COLOR_CHANGED,
    /** Sent when there is not enough memory to perform the operation */
    NO_MEMORY,
    LAST_SIGNAL
};

/** Properties */
enum {
    PROP_0,
    /** R/W. Boolean. Specify if the buffer contains rich text */
    PROP_RICH_TEXT,
    /** R. Boolean. Specify if there buffer has selection */
    PROP_HAS_SELECTION,
    /** R. Boolean. Specify if the buffer is empty */
    PROP_IS_EMPTY,
    /** R/W. Double. Specify the font scaling factor */
    PROP_FONT_SCALING_FACTOR,
    /** R/W. String. Specify the default font for rich text */
    PROP_DEF_FONT,
    /** R/W. Integer. Specify the default font size for rich text */
    PROP_DEF_FONT_SIZE,
    /** R/W. String. Specify the default font for plain text */
    PROP_DEF_PLAIN_FONT,
    /** R/W. Integer. Specify the default font size for plain text */
    PROP_DEF_PLAIN_FONT_SIZE,
    /** R. Integer. Specify the default attributes */
    PROP_DEF_ATTR,
    /** R/W. Color. Specify the background color */
    PROP_BACKGROUND_COLOR,
    /** R/W. Boolean. Specify if there is a low memory situation */
    PROP_LOW_MEM
};

static guint signals[LAST_SIGNAL];


/* WP_TYPE_TEXT_BUFFER */
G_DEFINE_TYPE(WPTextBuffer, wp_text_buffer, GTK_TYPE_TEXT_BUFFER)
/** Name of the tags */
  const gchar *tagnames[] = {
      "wp-text-bold",
      "wp-text-italic",
      "wp-text-underline",
      "wp-text-strike",
      "wp-text-left",
      "wp-text-center",
      "wp-text-right",
      "wp-text-bullet",
      "wp-text-forecolor",
      "wp-text-font",
      "wp-text-fontsize",
      "wp-text-sup-srpt",
      "wp-text-sub-srpt",
      "wp-text-backcolor",
      "\0"
  };

/**
 * Inline function to round a double value to integer
 * @param value is a double value
 * @return the rounded <i>value</i>
 */
static inline gint
iround(double value)
{
    return (gint) (value + 0.5);
}

static void
wp_text_buffer_class_init(WPTextBufferClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkTextBufferClass *buffer_class = GTK_TEXT_BUFFER_CLASS(klass);

    object_class->set_property = wp_text_buffer_set_property;
    object_class->get_property = wp_text_buffer_get_property;
    object_class->constructor = wp_text_buffer_constructor;
    object_class->finalize = wp_text_buffer_finalize;

    buffer_class->mark_set = wp_text_buffer_mark_set;
    buffer_class->insert_text = wp_text_buffer_insert_text;
    buffer_class->delete_range = wp_text_buffer_delete_range;
    buffer_class->apply_tag = wp_text_buffer_apply_tag;
    buffer_class->remove_tag = wp_text_buffer_remove_tag;
    buffer_class->begin_user_action = wp_text_buffer_begin_user_action;
    buffer_class->end_user_action = wp_text_buffer_end_user_action;
    buffer_class->insert_pixbuf = wp_text_buffer_insert_pixbuf;

    klass->refresh_attributes = NULL;
    klass->can_redo = NULL;
    klass->can_undo = NULL;
    klass->fmt_changed = NULL;
    klass->def_font_changed = NULL;
    klass->def_justification_changed = NULL;
    klass->background_color_changed = NULL;
    klass->no_memory = NULL;

    g_object_class_install_property(object_class, PROP_RICH_TEXT,
                                    g_param_spec_boolean("rich_text",
                                                         "rich_text",
                                                         "The buffer can contain rich text",
                                                         TRUE,
                                                         G_PARAM_CONSTRUCT |
                                                         G_PARAM_READWRITE));
    g_object_class_install_property(object_class, PROP_HAS_SELECTION,
                                    g_param_spec_boolean("has_selection",
                                                         "has_selection",
                                                         "True if something is selected",
                                                         FALSE,
                                                         G_PARAM_READABLE));
    g_object_class_install_property(object_class, PROP_IS_EMPTY,
                                    g_param_spec_boolean("is_empty",
                                                         "is_empty",
                                                         "True if there is no text in the buffer",
                                                         TRUE,
                                                         G_PARAM_READABLE));
    g_object_class_install_property(object_class, PROP_FONT_SCALING_FACTOR,
                                    g_param_spec_double("font_scale",
                                                        "font_scale",
                                                        "The font scaling factor for the buffer",
                                                        MIN_FONT_SCALE,
                                                        MAX_FONT_SCALE,
                                                        DEF_FONT_SCALE,
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_READWRITE));
    g_object_class_install_property(object_class, PROP_DEF_FONT,
                                    g_param_spec_string("def_font",
                                                        "def_font",
                                                        "Default font for the buffer",
                                                        DEF_FONT,
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_READWRITE));
    g_object_class_install_property(object_class, PROP_DEF_FONT_SIZE,
                                    g_param_spec_int("def_font_size",
                                                     "def_font_size",
                                                     "Default font size for the buffer",
                                                     0,
                                                     wp_font_size
                                                     [WP_FONT_SIZE_COUNT - 1],
                                                     wp_font_size
                                                     [DEF_FONT_SIZE],
                                                     G_PARAM_CONSTRUCT |
                                                     G_PARAM_READWRITE));
    g_object_class_install_property(object_class, PROP_DEF_PLAIN_FONT,
                                    g_param_spec_string("def_plain_font",
                                                        "def_plain_font",
                                                        "Default font for the plain text",
                                                        DEF_PLAIN_FONT,
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_READWRITE));
    g_object_class_install_property(object_class, PROP_DEF_PLAIN_FONT_SIZE,
                                    g_param_spec_int("def_plain_font_size",
                                                     "def_plain_font_size",
                                                     "Default font size for plain text",
                                                     0,
                                                     wp_font_size
                                                     [WP_FONT_SIZE_COUNT - 1],
                                                     wp_font_size
                                                     [DEF_PLAIN_FONT_SIZE],
                                                     G_PARAM_CONSTRUCT |
                                                     G_PARAM_READWRITE));
    g_object_class_install_property(object_class, PROP_DEF_ATTR,
                                    g_param_spec_pointer("def_attr",
                                                         "def_attr",
                                                         "Default attributes",
                                                         G_PARAM_READABLE));
    g_object_class_install_property(object_class, PROP_BACKGROUND_COLOR,
                                    g_param_spec_pointer("background_color",
                                                         "backgroun_color",
                                                         "Background color",
                                                         G_PARAM_READWRITE));
    g_object_class_install_property(object_class, PROP_LOW_MEM,
                                    g_param_spec_boolean("low_memory",
                                                         "low_memory",
                                                         "Low memory situation (undo disabled)",
                                                         FALSE,
                                                         G_PARAM_READWRITE));

    signals[REFRESH_ATTRIBUTES] =
        g_signal_new("refresh_attributes",
                     G_OBJECT_CLASS_TYPE(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(WPTextBufferClass,
                                     refresh_attributes), NULL, NULL,
                     g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
    signals[CAN_UNDO] =
        g_signal_new("can_undo", G_OBJECT_CLASS_TYPE(object_class),
                     G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(WPTextBufferClass,
                                                        can_undo), NULL,
                     NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE,
                     1, G_TYPE_BOOLEAN);

    signals[CAN_REDO] =
        g_signal_new("can_redo",
                     G_OBJECT_CLASS_TYPE(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(WPTextBufferClass, can_redo),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__BOOLEAN,
                     G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

    signals[FMT_CHANGED] =
        g_signal_new("fmt_changed",
                     G_OBJECT_CLASS_TYPE(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(WPTextBufferClass, fmt_changed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__BOOLEAN,
                     G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

    signals[DEF_FONT_CHANGED] =
        g_signal_new("def_font_changed",
                     G_OBJECT_CLASS_TYPE(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(WPTextBufferClass, def_font_changed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER,
                     G_TYPE_NONE, 1, G_TYPE_POINTER);

    signals[DEF_JUSTIFICATION_CHANGED] =
        g_signal_new("def_justification_changed",
                     G_OBJECT_CLASS_TYPE(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(WPTextBufferClass,
                                     def_justification_changed), NULL, NULL,
                     g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1,
                     G_TYPE_INT);
    signals[BACKGROUND_COLOR_CHANGED] =
        g_signal_new("background_color_changed",
                     G_OBJECT_CLASS_TYPE(object_class), G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(WPTextBufferClass,
                                     background_color_changed), NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                     G_TYPE_POINTER);
    signals[NO_MEMORY] =
        g_signal_new("no_memory", G_OBJECT_CLASS_TYPE(object_class),
                     G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(WPTextBufferClass,
                                                        no_memory), NULL,
                     NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}


static void
wp_text_buffer_init(WPTextBuffer * buffer)
{
    WPTextBufferPrivate *priv;

    buffer->priv = priv = g_new0(WPTextBufferPrivate, 1);
    priv->last_cursor_pos = 0;

    /* Default font properties */
    memset(&priv->default_fmt, 0, sizeof(WPTextBufferFormat));
    priv->default_fmt.cs.justification = TRUE;
    priv->default_fmt.justification = GTK_JUSTIFY_LEFT;
    priv->default_fmt.cs.font = TRUE;
    priv->default_fmt.font = 1;
    priv->default_fmt.cs.font_size = TRUE;
    priv->default_fmt.font_size = 3;
    priv->default_fmt.cs.text_position = TRUE;
    priv->default_fmt.text_position = TEXT_POSITION_NORMAL;
    priv->default_plain_fmt = priv->default_fmt;

    priv->fmt = priv->default_fmt;

    priv->is_rich_text = TRUE;

    priv->insert_preserve_tags = TRUE;
    priv->font_scaling_factor = 1.5;
    priv->background_color = NULL;
    priv->is_empty = TRUE;

    priv->last_line_justification = GTK_JUSTIFY_LEFT;

    priv->undo = wp_undo_new(GTK_TEXT_BUFFER(buffer));
    priv->queue_undo_reset = FALSE;
    g_signal_connect(G_OBJECT(priv->undo), "can_redo",
                     G_CALLBACK(wp_text_buffer_can_redo_cb), buffer);
    g_signal_connect(G_OBJECT(priv->undo), "can_undo",
                     G_CALLBACK(wp_text_buffer_can_undo_cb), buffer);
    g_signal_connect(G_OBJECT(priv->undo), "fmt_changed",
                     G_CALLBACK(wp_text_buffer_format_changed_cb), buffer);
    g_signal_connect(G_OBJECT(priv->undo), "last_line_justify",
                     G_CALLBACK(wp_text_buffer_last_line_justify_cb), buffer);
    g_signal_connect(G_OBJECT(priv->undo), "no_memory",
                     G_CALLBACK(wp_text_buffer_no_memory_cb), buffer);

    priv->color_tags =
        color_buffer_create(GTK_TEXT_BUFFER(buffer), "foreground_gdk", 500);
    priv->parser = wp_html_parser_new(buffer);
    priv->tag_hash = g_hash_table_new(NULL, NULL);
}


static GObject *
wp_text_buffer_constructor(GType type,
                           guint n_construct_properties,
                           GObjectConstructParam * construct_param)
{
    GObject *object;
    WPTextBuffer *buffer;

    object =
        G_OBJECT_CLASS(wp_text_buffer_parent_class)->constructor(type,
                                                                 n_construct_properties,
                                                                 construct_param);

    buffer = WP_TEXT_BUFFER(object);

    wp_text_buffer_init_tags(buffer);

    return object;
}


static void
wp_text_buffer_finalize(GObject * object)
{
    WPTextBuffer *buffer = WP_TEXT_BUFFER(object);
    WPTextBufferPrivate *priv = buffer->priv;

    g_free(priv->fonts);
    g_slist_free(priv->delete_tags);

    color_buffer_destroy(priv->color_tags);

    g_hash_table_destroy(priv->tag_hash);
    g_object_unref(priv->undo);

    if (priv->background_color)
        gdk_color_free(priv->background_color);

    g_free(priv);
    buffer->priv = NULL;

    G_OBJECT_CLASS(wp_text_buffer_parent_class)->finalize(object);
}

static void
wp_text_buffer_set_property(GObject * object,
                            guint prop_id,
                            const GValue * value, GParamSpec * pspec)
{
    WPTextBuffer *buffer = WP_TEXT_BUFFER(object);
    WPTextBufferPrivate *priv = buffer->priv;
    gint idx;

    // printf("Set property: %d\n", prop_id);
    switch (prop_id)
    {
        case PROP_RICH_TEXT:
            wp_text_buffer_enable_rich_text(buffer,
                                            g_value_get_boolean(value));
            break;
        case PROP_FONT_SCALING_FACTOR:
            wp_text_buffer_set_font_scaling_factor(buffer,
                                                   g_value_get_double(value));
            break;
        case PROP_DEF_FONT:
            idx = wp_get_font_index(g_value_get_string(value),
                                    priv->default_fmt.font);
            if (idx != priv->default_fmt.font)
            {
                priv->default_fmt.font = idx;
                if (priv->is_rich_text)
                    emit_default_font_changed(buffer);
            }
            break;
        case PROP_DEF_FONT_SIZE:
            idx = wp_get_font_size_index(g_value_get_int(value),
                                         priv->default_fmt.font_size);
            if (idx != priv->default_fmt.font_size)
            {
                priv->default_fmt.font_size = idx;
                if (priv->is_rich_text)
                    emit_default_font_changed(buffer);
            }
            break;
        case PROP_DEF_PLAIN_FONT:
            idx = wp_get_font_index(g_value_get_string(value),
                                    priv->default_plain_fmt.font);
            if (idx != priv->default_plain_fmt.font)
            {
                priv->default_plain_fmt.font = idx;
                if (!priv->is_rich_text)
                    emit_default_font_changed(buffer);
            }
            break;
        case PROP_DEF_PLAIN_FONT_SIZE:
            idx = wp_get_font_size_index(g_value_get_int(value),
                                         priv->default_plain_fmt.font_size);
            if (idx != priv->default_plain_fmt.font_size)
            {
                priv->default_plain_fmt.font_size = idx;
                if (!priv->is_rich_text)
                    emit_default_font_changed(buffer);
            }
            break;
        case PROP_BACKGROUND_COLOR:
            wp_text_buffer_set_background_color(buffer,
                                                g_value_get_pointer(value));
            break;
        case PROP_LOW_MEM:
            if (priv->undo)
                g_object_set(priv->undo, "low_memory",
                             g_value_get_boolean(value), NULL);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void
wp_text_buffer_get_property(GObject * object,
                            guint prop_id, GValue * value, GParamSpec * pspec)
{
    WPTextBuffer *buffer = WP_TEXT_BUFFER(object);

    switch (prop_id)
    {
        case PROP_RICH_TEXT:
            g_value_set_boolean(value, buffer->priv->is_rich_text);
            break;
        case PROP_HAS_SELECTION:
            g_value_set_boolean(value, buffer->priv->has_selection);
            break;
        case PROP_IS_EMPTY:
            g_value_set_boolean(value, buffer->priv->is_empty);
            break;
        case PROP_FONT_SCALING_FACTOR:
            g_value_set_double(value, buffer->priv->font_scaling_factor);
            break;
        case PROP_DEF_FONT:
            g_value_set_string(value,
            	wp_get_font_name(buffer->priv->default_fmt.font));
            break;
        case PROP_DEF_FONT_SIZE:
            g_value_set_int(value, buffer->priv->default_fmt.font_size);
            break;
        case PROP_DEF_PLAIN_FONT:
            g_value_set_string(value,
                               wp_get_font_name(buffer->priv->
                                                default_plain_fmt.font));
            break;
        case PROP_DEF_PLAIN_FONT_SIZE:
            g_value_set_int(value, buffer->priv->default_plain_fmt.font_size);
            break;
        case PROP_DEF_ATTR:
            g_value_set_pointer(value, &buffer->priv->default_fmt);
            break;
        case PROP_BACKGROUND_COLOR:
            g_value_set_pointer(value, buffer->priv->background_color);
            break;
        case PROP_LOW_MEM:
            if (buffer->priv->undo)
            {
                gboolean low_mem;
                g_object_get(buffer->priv->undo, "low_memory", &low_mem,
                             NULL);
                g_value_set_boolean(value, low_mem);
            }
            else
                g_value_set_boolean(value, TRUE);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

WPTextBuffer *
wp_text_buffer_new(GtkTextTagTable * table)
{
    return g_object_new(WP_TYPE_TEXT_BUFFER, "tag-table", table, NULL);
}


static void
wp_text_buffer_begin_user_action(GtkTextBuffer * text_buffer)
{
    WPTextBuffer *buffer = WP_TEXT_BUFFER(text_buffer);

    if (buffer->priv->fast_mode)
        return;

    freeze_cursor_moved(buffer);
    wp_undo_start_group(buffer->priv->undo);
    buffer->priv->queue_undo_reset = FALSE;
}


static void
wp_text_buffer_end_user_action(GtkTextBuffer * text_buffer)
{
    WPTextBuffer *buffer = WP_TEXT_BUFFER(text_buffer);
    WPTextBufferPrivate *priv = buffer->priv;
    GtkTextIter start, end;

    if (buffer->priv->queue_undo_reset) {
	wp_undo_reset (priv->undo);
	buffer->priv->queue_undo_reset = FALSE;
    }

    if (priv->fast_mode)
        return;

    // printf("End user action: %p, %d\n", priv->delete_tags,
    // priv->remember_tag);
    if (priv->delete_tags)
    {
        g_slist_free(priv->delete_tags);
        priv->delete_tags = NULL;
    }

    wp_text_buffer_check_apply_tag(buffer);

    if (!priv->insert_preserve_tags && priv->tmp_just)
    {
        gtk_text_buffer_get_iter_at_offset(text_buffer,
                                           &start, priv->just_start);
        gtk_text_buffer_get_iter_at_offset(text_buffer, &end, priv->just_end);
        gtk_text_buffer_apply_tag(text_buffer, priv->tmp_just, &start, &end);
        priv->tmp_just = NULL;
    }

    thaw_cursor_moved(buffer);
    wp_undo_end_group(priv->undo);
}


/**
 * Update the selection in the <i>buffer</i> and store also in undo
 * @param buffer is a #GtkTextBuffer
 */
static void
wp_text_buffer_update_selection(WPTextBuffer * buffer)
{
    GtkTextBuffer *text_buffer = GTK_TEXT_BUFFER(buffer);
    gboolean has_selection, old_selection =
        buffer->priv->has_selection != FALSE;
    GtkTextIter start, end;

    has_selection =
        gtk_text_buffer_get_selection_bounds(text_buffer, &start, &end);

    wp_undo_selection_changed(buffer->priv->undo, &start, &end);

    buffer->priv->has_selection = has_selection;

    if (old_selection != has_selection)
    {
        buffer->priv->last_cursor_pos = -1;
        emit_refresh_attributes(buffer, NULL);
    }
}


static void
wp_text_buffer_mark_set(GtkTextBuffer * text_buffer,
                        const GtkTextIter * iter, GtkTextMark * mark)
{
    WPTextBuffer *buffer = WP_TEXT_BUFFER(text_buffer);
    if (buffer->priv->fast_mode)
        return;

    GtkTextMark *insert = gtk_text_buffer_get_insert(text_buffer);
    GtkTextMark *sel_bound = gtk_text_buffer_get_selection_bound(text_buffer);

    if (GTK_TEXT_BUFFER_CLASS(wp_text_buffer_parent_class)->mark_set)
        GTK_TEXT_BUFFER_CLASS(wp_text_buffer_parent_class)->
            mark_set(text_buffer, iter, mark);

    if (mark == insert || mark == sel_bound)
        wp_text_buffer_update_selection(buffer);

    if (mark == insert)
        emit_refresh_attributes(buffer, iter);
}


/**
 * Find a justification tag from the <i>head</i> tag list
 * @param head is a #GSList of #GtkTextTag
 * @param free is set if the <i>head</i> should be freed.
 * @return the found justification tag or <b>NULL</b> if it not found
 */
static GtkTextTag *
find_justification_tag(GSList * head, gboolean free)
{
    GtkTextTag *result = NULL, *tmp;
    GSList *iter = head;

    while (iter)
    {
        tmp = GTK_TEXT_TAG(iter->data);
        if (tmp->justification_set)
        {
            result = tmp;
            break;
        }
        iter = iter->next;
    }
    if (free)
        g_slist_free(head);

    return result;
}

static void
wp_text_buffer_check_apply_tag(WPTextBuffer * buffer)
{
    WPTextBufferPrivate *priv = buffer->priv;

    // printf("Check apply tags: %d\n", priv->last_is_insert);

    if (priv->last_is_insert)
    {
        priv->last_is_insert = FALSE;
        GSList *tmp = priv->copy_insert_tags;
        priv->copy_insert_tags = NULL;
        wp_text_buffer_copy_tag_attributes(buffer, tmp,
                                           &priv->copy_start,
                                           &priv->copy_end);
        g_slist_free(tmp);
    }
}

static void
wp_text_buffer_insert_text(GtkTextBuffer * text_buffer,
                           GtkTextIter * pos, const gchar * text, gint length)
{
    WPTextBuffer *buffer = WP_TEXT_BUFFER(text_buffer);
    WPTextBufferPrivate *priv = buffer->priv;
    GSList *tags = NULL;
    gint start_offset = 0;
    GtkTextIter start;
    gboolean selection_deleted = buffer->priv->delete_tags != NULL;
    gboolean copy_tag;
    gchar pixbuf_str [6];
    gboolean has_image;
    
    pixbuf_str[g_unichar_to_utf8 (0xfffc, pixbuf_str)] = '\0';
    has_image = (strstr (text, pixbuf_str) != NULL);

    if (!text[0])
        return;

    if (priv->fast_mode)
    {
        GTK_TEXT_BUFFER_CLASS(wp_text_buffer_parent_class)->
            insert_text(text_buffer, pos, text, length);
        priv->is_empty = FALSE;
        return;
    }

    wp_text_buffer_check_apply_tag(buffer);

    wp_undo_insert_text(priv->undo, pos, text, length);

    priv->is_empty = FALSE;

    copy_tag = wp_undo_is_enabled(priv->undo) &&
        priv->insert_preserve_tags && priv->is_rich_text;

    /* printf("Insert text: %s, %p, %d, %d\n", text, priv->delete_tags,
     * priv->insert_preserve_tags, copy_tag); */

    if (copy_tag)
    {
        if (buffer->priv->delete_tags)
        {
            tags = buffer->priv->delete_tags;
            buffer->priv->delete_tags = NULL;
        }
        else if ((gtk_text_iter_starts_line(pos)
                  && !gtk_text_iter_ends_line(pos)
                  && !gtk_text_iter_is_end(pos))
                 || gtk_text_iter_is_start(pos))
            tags = gtk_text_iter_get_toggled_tags(pos, TRUE);
        else
            tags = gtk_text_iter_get_toggled_tags(pos, FALSE);

    }
    start_offset = gtk_text_iter_get_offset(pos);

    GTK_TEXT_BUFFER_CLASS(wp_text_buffer_parent_class)->
        insert_text(text_buffer, pos, text, length);

    start = *pos;
    gtk_text_iter_set_offset(&start, start_offset);

    priv->convert_tag = FALSE;
    if (!priv->insert_preserve_tags)
    {
        gtk_text_buffer_remove_all_tags(text_buffer, &start, pos);
    }
    else if (priv->force_copy)
    {
        priv->force_copy = FALSE;
        wp_text_buffer_copy_tag_attributes(buffer, tags, &start, pos);
        g_slist_free(tags);
    }
    else if (copy_tag)
    {
        priv->last_is_insert = TRUE;
        priv->copy_insert_tags = tags;
        priv->copy_start = start;
        priv->copy_end = *pos;
    }
    else
    {
        g_slist_free(priv->copy_insert_tags);
        priv->copy_insert_tags = NULL;

        /* debug_print_tags(&start, 0); debug_print_tags(pos, 0); */
        if (!priv->tmp_just)
        {
            priv->tmp_just =
                find_justification_tag(gtk_text_iter_get_tags(pos), TRUE);
            if (priv->tmp_just)
            {
                priv->just_start = start_offset;
                gtk_text_buffer_remove_tag(text_buffer, priv->tmp_just,
                                           &start, pos);
            }
        }
        if (priv->tmp_just)
        {
            priv->just_end = gtk_text_iter_get_offset(pos);
            /* printf("Temporarly removing justification tag: %d-%d\n",
             * priv->just_start, priv->just_end); */
        }
    }

    if (priv->insert_preserve_tags && !selection_deleted
        && priv->is_rich_text)
        emit_refresh_attributes(buffer, pos);

    if (has_image)
	    buffer->priv->queue_undo_reset = TRUE;
}

/**
 * Remove <i>orig_tag</i> and apply <i>tag</i> for the interval between
 * <i>start</i> and <i>end</i> in <i>buffer</i>
 * @param buffer is a #GtkTextBuffer
 * @param start a position in the buffer
 * @param end a position in the buffer
 * @param orig_tag is a #GtkTextTag
 * @param tag is a #GtkTextTag
 */
static void
apply_justification_tag(WPTextBuffer * buffer,
                        GtkTextIter * start, GtkTextIter * end,
                        GtkTextTag * orig_tag, GtkTextTag * tag)
{
    GtkTextBuffer *text_buffer = GTK_TEXT_BUFFER(buffer);
    WPTextBufferPrivate *priv = buffer->priv;

    wp_undo_freeze(priv->undo);
    wp_text_buffer_remove_tag(text_buffer, orig_tag, start, end);
    if (tag)
        wp_text_buffer_apply_tag(text_buffer, tag, start, end);
    wp_undo_thaw(priv->undo);

    if (tag && gtk_text_iter_is_end(end))
        emit_default_justification_changed(buffer,
                                           tag->values->justification);

    wp_undo_simple_justification(priv->undo, start, end, orig_tag, tag);
}

void
_wp_text_buffer_adjust_justification(WPTextBuffer * buffer,
                                     GtkTextIter * start, GtkTextIter * end,
                                     GtkTextTag * def_tag,
                                     gboolean align_to_right)
{
    GtkTextTag *orig_tag, *tag;
    GtkTextIter *tmp = start ? start : end, pos;

    orig_tag =
        find_justification_tag(gtk_text_iter_get_toggled_tags
                               (tmp, start != NULL), TRUE);
    if (!orig_tag)
    {
        if (!def_tag && gtk_text_iter_is_end(tmp))
        {
            tag =
                find_justification_tag(gtk_text_iter_get_toggled_tags
                                       (tmp, FALSE), TRUE);
            if (tag)
                emit_default_justification_changed(buffer,
                                                   tag->values->
                                                   justification);
        }
        return;
    }
    tag =
        find_justification_tag(gtk_text_iter_get_toggled_tags(tmp, !start),
                               TRUE);
    if (!tag && def_tag)
        tag = def_tag;

    if (start && !end)
    {
        pos = *start;
        gtk_text_iter_forward_to_line_end(&pos);

        apply_justification_tag(buffer, start, &pos, orig_tag, tag);
    }
    else if (start)
        apply_justification_tag(buffer, start, end, orig_tag, tag);
    else
        /* if (!align_to_right) { pos = *end;
         * gtk_text_iter_forward_to_line_end(&pos);
         * apply_justification_tag(buffer, end, &pos, orig_tag, tag); } else */
    {
        pos = *end;
        gtk_text_iter_set_line_offset(&pos, 0);
        apply_justification_tag(buffer, &pos, end, orig_tag, tag);
    }
}

static void
wp_text_buffer_delete_range(GtkTextBuffer * text_buffer,
                            GtkTextIter * start, GtkTextIter * end)
{
    WPTextBuffer *buffer = WP_TEXT_BUFFER(text_buffer);
    WPTextBufferPrivate *priv = buffer->priv;
    GtkTextTag *tag = NULL;
    gboolean undo, copy_tag, iter_end, different_line;
    gboolean has_image;
    gchar pixbuf_char[6];

    pixbuf_char[g_unichar_to_utf8 (0xfffc, pixbuf_char)] = 0;

    if (priv->fast_mode)
    {
        GTK_TEXT_BUFFER_CLASS(wp_text_buffer_parent_class)->
            delete_range(text_buffer, start, end);
        return;
    }

    wp_text_buffer_check_apply_tag(buffer);

    if (priv->delete_tags)
    {
        g_slist_free(priv->delete_tags);
        priv->delete_tags = NULL;
    }

    // printf("Delete range: %d-%d\n", gtk_text_iter_get_offset(start),
    // gtk_text_iter_get_offset(end));
    has_image = gtk_text_iter_forward_search (start, pixbuf_char, 0, NULL, NULL, end);

    undo = wp_undo_is_enabled(priv->undo);
    copy_tag = undo && priv->insert_preserve_tags;
    /* if the start and end iterator is in different line we need to apply
     * the justification till the end of the newline */
    different_line = undo && (gtk_text_iter_get_line(start) !=
                              gtk_text_iter_get_line(end));

    priv->is_empty = (iter_end = gtk_text_iter_is_end(end)) &&
        gtk_text_iter_is_start(start);
    if (priv->is_empty && priv->insert_preserve_tags)
        _wp_text_buffer_get_attributes(buffer, &priv->fmt, TRUE, FALSE);

    wp_undo_delete_range(priv->undo, start, end);

    priv->convert_tag = FALSE;
    if (!priv->is_empty && copy_tag)
    {
        if (iter_end || different_line)
            tag = find_justification_tag(gtk_text_iter_get_tags(start), TRUE);

        if (priv->has_selection || priv->remember_tag)
        {
            priv->delete_tags =
                iter_end ?
                gtk_text_iter_get_tags(start) :
                gtk_text_iter_get_toggled_tags(start, TRUE);
        }

        if (iter_end && tag)
            emit_default_justification_changed(buffer,
                                               tag->values->justification);
    }

    /* Don't know why, but if is a large portion to delete, and the interval
     * contains several tag toggles, it takes eternity for the textbuffer to 
     * delete. So we add a little hack, first clear the tags */

    if (abs(gtk_text_iter_get_offset(end) - gtk_text_iter_get_offset(start)) >
        100)
    {
        if (undo)
            wp_undo_freeze(priv->undo);
        gtk_text_buffer_remove_all_tags(text_buffer, start, end);
        if (undo)
            wp_undo_thaw(priv->undo);
    }

    GTK_TEXT_BUFFER_CLASS(wp_text_buffer_parent_class)->
        delete_range(text_buffer, start, end);

    if (!priv->is_empty)
    {
        if (different_line)
            _wp_text_buffer_adjust_justification(buffer,
                                                 start, NULL, tag, FALSE);
    }
    else
    {
        emit_default_justification_changed(buffer, priv->fmt.justification);
    }
    // TODO: only emit cursor moved if the delete is not happend with the
    // backspace key
    buffer->priv->last_cursor_pos = -1;
    wp_text_buffer_update_selection(buffer);
    emit_refresh_attributes(buffer, start);
    if (has_image)
	buffer->priv->queue_undo_reset = TRUE;
}


gboolean
wp_text_buffer_has_selection(WPTextBuffer * buffer)
{
    g_return_val_if_fail(WP_IS_TEXT_BUFFER(buffer), FALSE);
    return buffer->priv->has_selection;
}

static void
_apply_tag(WPTextBufferPrivate * priv,
           GtkTextBuffer * buffer,
           GtkTextTag * tag,
           const GtkTextIter * start, const GtkTextIter * end)
{
    if (!priv->fast_mode)
    {
        wp_undo_apply_tag(priv->undo, start, end, tag, TRUE);
    }

    GTK_TEXT_BUFFER_CLASS(wp_text_buffer_parent_class)->apply_tag(buffer,
                                                                  tag,
                                                                  start, end);
    /* printf("Apply tag: %s, %d-%d\n", tag->name ? tag->name : "(null)",
     * gtk_text_iter_get_offset(start), gtk_text_iter_get_offset(end)); */
}

static void
wp_text_buffer_apply_tag(GtkTextBuffer * buffer,
                         GtkTextTag * tag,
                         const GtkTextIter * start, const GtkTextIter * end)
{
    if (tag == NULL) {
        return;
    }

    WPTextBufferPrivate *priv = WP_TEXT_BUFFER(buffer)->priv;

    if (!priv->is_rich_text && wp_undo_is_enabled(priv->undo))
        return;

    if (!priv->fast_mode && priv->last_is_insert)
    {
        // printf("Apply tag: ** removing insert tags **\n");
        priv->last_is_insert = FALSE;
        g_slist_free(priv->copy_insert_tags);
        priv->copy_insert_tags = NULL;
        gtk_text_buffer_remove_all_tags(buffer, &priv->copy_start,
                                        &priv->copy_end);
        priv->convert_tag = TRUE;
    }

    /* Dirty little hack, to fix the rich-text copy and paste problems. Fix
     * this, when a proper text_buffer deserializer is done in the Gtk. It is 
     * already there in the gtk 2.10 */
    if (priv->convert_tag)
    {
        // printf("=== Convert tag: %s ===\n", tag->name);
        if (!g_hash_table_lookup(priv->tag_hash, tag))
        {
            if (tag->name && strncmp(tag->name, "wp-text-bullet", 14) == 0)
                _apply_tag(priv, buffer, priv->tags[WPT_BULLET], start, end);
            else
            {
                if (tag->values->font)
                {
                    gint size;
                    const gchar *name;
                    PangoFontDescription *font = tag->values->font;
                    if (pango_font_description_get_style(font))
                        _apply_tag(priv, buffer, priv->tags[WPT_ITALIC],
                                   start, end);
                    if (pango_font_description_get_weight(font) !=
                        PANGO_WEIGHT_NORMAL)
                        _apply_tag(priv, buffer, priv->tags[WPT_BOLD], start,
                                   end);
                    if ((size = pango_font_description_get_size(font)))
                    {
                        if (tag->name
                            && strncmp(tag->name, "wp-text-", 8) == 0)
                        {
                            gchar no[2], *p;
                            no[1] = 0;
                            p = strrchr(tag->name, '-');
                            if (*(p - 1) >= '0' && *(p - 1) <= '9')
                                no[0] = *(p - 1);
                            else
                                no[0] = *(p + 1);
                            // printf("Text size: %s\n", no);
                            size = atoi(no);
                            if (tag->values->appearance.rise == 0)
                                _apply_tag(priv, buffer,
                                           priv->font_size_tags[size], start,
                                           end);
                            else if (tag->values->appearance.rise < 0)
                                _apply_tag(priv, buffer,
                                           priv->font_size_sub_tags[size],
                                           start, end);
                            else
                                _apply_tag(priv, buffer,
                                           priv->font_size_sup_tags[size],
                                           start, end);
                        }
                        else
                        {
                            size =
                                wp_get_font_size_index(iround
                                                       (size /
                                                        priv->
                                                        font_scaling_factor /
                                                        PANGO_SCALE),
                                                       priv->default_fmt.
                                                       font_size);
                            _apply_tag(priv, buffer,
                                       priv->font_size_tags[size], start,
                                       end);
                        }
                    }
                    if ((name = pango_font_description_get_family(font)))
                    {
                        gint idx =
                            wp_get_font_index(name, priv->default_fmt.font);
                        _apply_tag(priv, buffer, priv->fonts[idx], start,
                                   end);
                    }
                }
                if (tag->underline_set)
                    _apply_tag(priv, buffer, priv->tags[WPT_UNDERLINE], start,
                               end);
                if (tag->strikethrough_set)
                    _apply_tag(priv, buffer, priv->tags[WPT_STRIKE], start,
                               end);
                if (tag->justification_set)
                {
                    if (tag->values->justification == GTK_JUSTIFY_LEFT)
                        _apply_tag(priv, buffer, priv->tags[WPT_LEFT], start,
                                   end);
                    else if (tag->values->justification == GTK_JUSTIFY_CENTER)
                        _apply_tag(priv, buffer, priv->tags[WPT_CENTER],
                                   start, end);
                    else
                        _apply_tag(priv, buffer, priv->tags[WPT_RIGHT], start,
                                   end);
                }
                if (tag->fg_color_set)
                {
                    GtkTextTag *t = color_buffer_get_tag(priv->color_tags,
                                                         &tag->values->
                                                         appearance.fg_color,
                                                         priv->
                                                         tags[WPT_RIGHT]->
                                                         priority + 1);
                    _apply_tag(priv, buffer, t, start, end);
                    g_hash_table_insert(priv->tag_hash, t, NULL);
                }
            }
            tag = NULL;
        }
    }

    if (tag) {
        _apply_tag(priv, buffer, tag, start, end);
    }
    
    if (!priv->insert_preserve_tags && tag && tag->justification_set
        && priv->tmp_just)
    {
        priv->tmp_just = NULL;
        priv->just_start = 0;
    }
}


static void
wp_text_buffer_remove_tag(GtkTextBuffer * buffer,
                          GtkTextTag * tag,
                          const GtkTextIter * start, const GtkTextIter * end)
{
    if (!WP_TEXT_BUFFER(buffer)->priv->fast_mode)
        wp_undo_apply_tag(WP_TEXT_BUFFER(buffer)->priv->undo,
                          start, end, tag, FALSE);
    WPTextBufferPrivate *priv = WP_TEXT_BUFFER(buffer)->priv;

    if (priv->last_is_insert)
    {
        // printf("Remove tag: ** removing insert tags **\n");
        priv->last_is_insert = FALSE;
        g_slist_free(priv->copy_insert_tags);
        priv->copy_insert_tags = NULL;
    }

    GTK_TEXT_BUFFER_CLASS(wp_text_buffer_parent_class)->remove_tag(buffer,
                                                                   tag,
                                                                   start,
                                                                   end);

    /* printf("Remove tag: %s, %d-%d, %d\n", tag->name ? tag->name :
     * "(null)", gtk_text_iter_get_offset(start),
     * gtk_text_iter_get_offset(end),
     * wp_undo_is_enabled(WP_TEXT_BUFFER(buffer)->priv->undo)); */
}


void
wp_text_buffer_freeze(WPTextBuffer * buffer)
{
    g_return_if_fail(WP_IS_TEXT_BUFFER(buffer));
    freeze_cursor_moved(buffer);
    buffer->priv->insert_preserve_tags = FALSE;
}


void
wp_text_buffer_thaw(WPTextBuffer * buffer)
{
    g_return_if_fail(WP_IS_TEXT_BUFFER(buffer));
    thaw_cursor_moved(buffer);
    buffer->priv->insert_preserve_tags = TRUE;
}

/**
 * Emit background color change signal
 */
static void
emit_background_color_change(WPTextBuffer * buffer)
{
    g_signal_emit(buffer, signals[BACKGROUND_COLOR_CHANGED], 0,
                  buffer->priv->background_color);
}

/**
 * Callback from timeout, to send the refresh_attributes signal
 * @param data is a #GtkTextBuffer
 */
static gboolean
idle_emit_refresh_attributes(gpointer data)
{
    if ((data) && WP_IS_TEXT_BUFFER(data))
    {
        WP_TEXT_BUFFER(data)->priv->source_refresh_attributes = 0;

        g_signal_emit(data, signals[REFRESH_ATTRIBUTES], 0);
    }
    return FALSE;
}

static void
emit_refresh_attributes(WPTextBuffer * buffer, const GtkTextIter * where)
{

    if (where == NULL)
        return;
    /* As there is already a null check for where 
     * w donot have to test for null again here */
   // gint tmp = where ? gtk_text_iter_get_offset(where) : 0;
    gint tmp = gtk_text_iter_get_offset(where);

    if (!buffer->priv->cursor_moved_frozen)
    {
        if (tmp != buffer->priv->last_cursor_pos)
        {
            // if (1) {
            buffer->priv->last_cursor_pos = tmp;

            if (!gtk_text_iter_is_start(where)
                && !gtk_text_iter_is_end(where))
                *(int *) &buffer->priv->fmt.cs = 0;

            if (buffer->priv->source_refresh_attributes)
                g_source_remove(buffer->priv->source_refresh_attributes);

            buffer->priv->source_refresh_attributes =
                g_timeout_add(400, idle_emit_refresh_attributes, buffer);
        }
    }
    else
        buffer->priv->cursor_moved = TRUE;
}

static void
freeze_cursor_moved(WPTextBuffer * buffer)
{
    buffer->priv->cursor_moved_frozen++;
}

static void
thaw_cursor_moved(WPTextBuffer * buffer)
{
    g_return_if_fail(buffer->priv->cursor_moved_frozen > 0);

    if (!--buffer->priv->cursor_moved_frozen && buffer->priv->cursor_moved)
    {
        GtkTextIter iter;
        GtkTextMark *insert =
            gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(buffer));
        gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(buffer), &iter,
                                         insert);
        emit_refresh_attributes(buffer, &iter);
        buffer->priv->cursor_moved = FALSE;
    }
}


/**
 * Check if a <i>tag</i> is a tag from the <i>base</i>, and find the
 * difference number between the <i>tag</i>'s id and the base. It is used
 * to retrieve the font size from a font tag
 * @param tag is a #GtkTextTag
 * @param base is a number identifying the base (#WPT_FONT_SIZE, #WPT_SUP_SRPT, 
 *              #WPT_SUB_SRPT, #WPT_FONT)
 * @param nr pointer to a number which will hold the difference
 * @return <b>TRUE</b> if the <i>tag</i> is of type <i>base</i>
 */
static gboolean
check_tag_type(GtkTextTag * tag, gint base, gint * nr)
{
    gint val = (gint) g_object_get_data(G_OBJECT(tag), WPT_ID);
    gboolean result = val >= base && val <= base + 999;
    if (result && nr)
        *nr = val - base;
    return result;
}

/**
 * Check if a <i>tag</i> is modifying the font size
 * @param tag is a #GtkTextTag
 * @return <b>TRUE</b> if the <i>tag</i> is modifying the size
 */
static gboolean
check_tag_fontsize_type(GtkTextTag * tag)
{
    gint val = (gint) g_object_get_data(G_OBJECT(tag), WPT_ID);
    return (val >= WPT_FONT_SIZE && val <= WPT_FONT_SIZE + 999) ||
        (val >= WPT_SUB_SRPT && val <= WPT_SUB_SRPT + 999) ||
        (val >= WPT_SUP_SRPT && val <= WPT_SUP_SRPT + 999);
}

#define HILDON_BASE_COLOR_NUM 15

static void
wp_text_buffer_init_tags(WPTextBuffer * buffer)
{
    int i;
    GtkTextBuffer *b = GTK_TEXT_BUFFER(buffer);
    gchar *tmp;
    WPTextBufferPrivate *priv = buffer->priv;
    static char *base_colours[HILDON_BASE_COLOR_NUM] = {
        "#FFFFFF", "#FF0000", "#660000", "#0000FF", "#000066",
        "#FF33FF", "#660066", "#33CC33", "#006600", "#FFCC00",
        "#CC9900", "#999999", "#666666", "#00CCCC", "#006666"
    };
    GdkColor color = { 0 };

    priv->tags[WPT_BOLD] =
        gtk_text_buffer_create_tag(b, tagnames[WPT_BOLD], "weight",
                                   PANGO_WEIGHT_BOLD, NULL);
    g_hash_table_insert(priv->tag_hash, priv->tags[WPT_BOLD], NULL);
    priv->tags[WPT_ITALIC] =
        gtk_text_buffer_create_tag(b, tagnames[WPT_ITALIC], "style",
                                   PANGO_STYLE_ITALIC, NULL);
    g_hash_table_insert(priv->tag_hash, priv->tags[WPT_ITALIC], NULL);
    priv->tags[WPT_UNDERLINE] =
        gtk_text_buffer_create_tag(b, tagnames[WPT_UNDERLINE], "underline",
                                   PANGO_UNDERLINE_SINGLE, NULL);
    g_hash_table_insert(priv->tag_hash, priv->tags[WPT_UNDERLINE], NULL);

    priv->tags[WPT_STRIKE] =
        gtk_text_buffer_create_tag(b, tagnames[WPT_STRIKE],
                                   "strikethrough", TRUE, NULL);
    g_hash_table_insert(priv->tag_hash, priv->tags[WPT_STRIKE], NULL);

    priv->tags[WPT_LEFT] =
        gtk_text_buffer_create_tag(b, tagnames[WPT_LEFT],
                                   "justification", GTK_JUSTIFY_LEFT, NULL);
    g_hash_table_insert(priv->tag_hash, priv->tags[WPT_LEFT], NULL);

    priv->tags[WPT_CENTER] =
        gtk_text_buffer_create_tag(b, tagnames[WPT_CENTER],
                                   "justification", GTK_JUSTIFY_CENTER, NULL);
    g_hash_table_insert(priv->tag_hash, priv->tags[WPT_CENTER], NULL);

    priv->tags[WPT_RIGHT] =
        gtk_text_buffer_create_tag(b, tagnames[WPT_RIGHT],
                                   "justification", GTK_JUSTIFY_RIGHT, NULL);
    g_hash_table_insert(priv->tag_hash, priv->tags[WPT_RIGHT], NULL);

    for (i = 0; i < WP_FONT_SIZE_COUNT; i++)
    {
        /* Normal size */
        tmp = g_strdup_printf("wp-text-font-size-%d", i);
        priv->font_size_tags[i] = gtk_text_buffer_create_tag(b, tmp, NULL);
        g_object_set_data(G_OBJECT(priv->font_size_tags[i]), WPT_ID,
                          GINT_TO_POINTER(WPT_FONT_SIZE + i));
        g_free(tmp);
        g_hash_table_insert(priv->tag_hash, priv->font_size_tags[i], NULL);

        /* Superscript size */
        tmp = g_strdup_printf("wp-text-sup-%d", i);
        priv->font_size_sup_tags[i] =
            gtk_text_buffer_create_tag(b, tmp, NULL);
        g_object_set_data(G_OBJECT(priv->font_size_sup_tags[i]), WPT_ID,
                          GINT_TO_POINTER(WPT_SUP_SRPT + i));
        g_free(tmp);
        g_hash_table_insert(priv->tag_hash, priv->font_size_sup_tags[i],
                            NULL);

        /* Subscript size */
        tmp = g_strdup_printf("wp-text-sub-%d", i);
        priv->font_size_sub_tags[i] =
            gtk_text_buffer_create_tag(b, tmp, NULL);
        g_object_set_data(G_OBJECT(priv->font_size_sub_tags[i]), WPT_ID,
                          GINT_TO_POINTER(WPT_SUB_SRPT + i));
        g_free(tmp);
        g_hash_table_insert(priv->tag_hash, priv->font_size_sub_tags[i],
                            NULL);
    }

    wp_text_buffer_resize_font(buffer);

    /* Create font tags from all available fonts */

    priv->fonts = g_new(GtkTextTag *, wp_get_font_count());
    for (i = 0; i < wp_get_font_count(); i++)
    {
        tmp = g_strdup_printf("wp-text-font-%s", wp_get_font_name(i));
        priv->fonts[i] = gtk_text_buffer_create_tag(b, tmp,
                                                    "family",
                                                    wp_get_font_name(i),
                                                    NULL);
        g_free(tmp);
        g_object_set_data(G_OBJECT(priv->fonts[i]), WPT_ID,
                          GINT_TO_POINTER(WPT_FONT + i));
        g_hash_table_insert(priv->tag_hash, priv->fonts, NULL);
        // printf("Ordered font: %s\n", priv->font_name_list[i]);
    }

    // Create the bullet last to have the highest priority
    priv->tags[WPT_BULLET] =
        gtk_text_buffer_create_tag(b, tagnames[WPT_BULLET],
                                   "weight", PANGO_WEIGHT_NORMAL,
                                   "style", PANGO_STYLE_NORMAL,
                                   "underline", PANGO_UNDERLINE_NONE,
                                   "font", "fixed",
                                   "strikethrough", FALSE, "indent", 8, NULL);
    g_hash_table_insert(priv->tag_hash, priv->tags[WPT_BULLET], NULL);

    for (i = 0; i < HILDON_BASE_COLOR_NUM; i++)
    {
        gdk_color_parse(base_colours[i], &color);
        g_hash_table_insert(priv->tag_hash,
                            color_buffer_get_tag(priv->color_tags, &color,
                                                 priv->tags[WPT_RIGHT]->
                                                 priority + 1), NULL);
    }

}


GtkTextTag *
wp_text_buffer_get_tag(WPTextBuffer * buffer, gint tagno)
{
    g_return_val_if_fail(WP_IS_TEXT_BUFFER(buffer) && tagno < WPT_LASTTAG,
                         NULL);

    return buffer->priv->tags[tagno];
}

/* TODO optimization if a font-size type is copied and cs.font_size is set,
 * because does an unnecessary apply/remove attributes (also the case of
 * sub/superscript) */
static void
wp_text_buffer_copy_tag_attributes(WPTextBuffer * buffer,
                                   GSList * tags, GtkTextIter * start,
                                   GtkTextIter * end)
{
    GtkTextBuffer *text_buffer;
    GtkTextTag *tag;
    WPTextBufferFormatChangeSet cs;
    GtkTextTag **ttags;

    // printf("wp_text_buffer_copy_tag_attributes\n");
    // debug_print_tags(start, 0);
    text_buffer = GTK_TEXT_BUFFER(buffer);
    cs = buffer->priv->fmt.cs;
    ttags = buffer->priv->tags;
    while (tags)
    {
        tag = GTK_TEXT_TAG(tags->data);
        tags = tags->next;

        // printf("Tag: %p, %s\n", tag, tag->name ? tag->name : "(null)");
        // if (tag != ttags[WPT_BULLET])
        {
            if ((tag == ttags[WPT_BOLD] && !cs.bold) ||
                (tag == ttags[WPT_ITALIC] && !cs.italic) ||
                (tag == ttags[WPT_UNDERLINE] && !cs.underline) ||
                (tag == ttags[WPT_STRIKE] && !cs.strikethrough) ||
                (tag->rise_set && (!cs.font_size || !cs.text_position)) ||
                (tag->justification_set && !cs.justification) ||
                (tag->fg_color_set && !cs.color) ||
                (!cs.font && tag->values->font
                 && check_tag_type(tag, WPT_FONT, NULL)) || ((!cs.font_size
                                                              || !cs.
                                                              text_position)
                                                             && tag->values->
                                                             font
                                                             &&
                                                             check_tag_type
                                                             (tag,
                                                              WPT_FONT_SIZE,
                                                              NULL)))
            {
                if (tag->justification_set && gtk_text_iter_is_end(end) &&
                    tag->values->justification !=
                    buffer->priv->last_line_justification)
                {
                    buffer->priv->fmt.justification =
                        buffer->priv->last_line_justification;
                    buffer->priv->fmt.cs.justification = TRUE;
                }
                else
                    gtk_text_buffer_apply_tag(text_buffer, tag, start, end);
            }
        }
    }

    wp_text_buffer_apply_attributes(buffer, start, end, FALSE, NULL);
}

/**
 * Remove all the tags with id <i>tagid</i> between <i>start</i> and
 * <i>end</i> interval.
 * @param buffer is a #GtkTextBuffer
 * @param start a position in the buffer
 * @param end a position in the buffer
 * @param tagid is the id identifying the tag to be removed (WPT_x)
 */
static void
remove_tags_with_id(WPTextBuffer * buffer, GtkTextIter * start,
                    GtkTextIter * end, gint tagid)
{
    GSList *tags, *tags_head;
    GtkTextTag *tag = NULL;
    GtkTextIter tmp;
    GtkTextBuffer *text_buffer;

    g_return_if_fail(buffer);

    tags_head = tags = gtk_text_iter_get_tags(start);
    tmp = *start;
    text_buffer = GTK_TEXT_BUFFER(buffer);

    do
    {
        while (tags)
        {
            tag = GTK_TEXT_TAG(tags->data);
            tags = tags->next;
            switch (tagid)
            {
                case WPT_FORECOLOR:
                    if (tag->fg_color_set)
                        gtk_text_buffer_remove_tag(text_buffer, tag, start,
                                                   end);
                    break;
                case WPT_FONT:
                    if (tag->values->font
                        && check_tag_type(tag, WPT_FONT, NULL))
                        gtk_text_buffer_remove_tag(text_buffer, tag, start,
                                                   end);
                    break;
                case WPT_FONT_SIZE:
                    if (tag->values->font
                        && check_tag_type(tag, WPT_FONT_SIZE, NULL))
                        gtk_text_buffer_remove_tag(text_buffer, tag, start,
                                                   end);
                    break;
                case WPT_SUB_SRPT:
                    if (tag->values->font
                        && check_tag_type(tag, WPT_SUB_SRPT, NULL))
                        gtk_text_buffer_remove_tag(text_buffer, tag, start,
                                                   end);
                    break;
                case WPT_SUP_SRPT:
                    if (tag->values->font
                        && check_tag_type(tag, WPT_SUP_SRPT, NULL))
                        gtk_text_buffer_remove_tag(text_buffer, tag, start,
                                                   end);
                    break;
                case WPT_ALL_FONT_SIZE:
                    if (tag->values->font && check_tag_fontsize_type(tag))
                        gtk_text_buffer_remove_tag(text_buffer, tag, start,
                                                   end);
                    break;
            }
        }

        g_slist_free(tags_head);

        if (!gtk_text_iter_forward_to_tag_toggle(&tmp, NULL) ||
            (gtk_text_iter_compare(&tmp, end) >= 0))
            break;

        tags_head = tags = gtk_text_iter_get_toggled_tags(&tmp, TRUE);
    } while (1);
}

/**
 * Remove <i>tag</i> from <i>buffer</i>, and sets <i>end</i> to the tag
 * end or <i>buffer_end</i> if the tag toggle is after the end
 * @param buffer is a #GtkTextBuffer
 * @param tag is a #GtkTextTag
 * @param start a position in the buffer
 * @param end a position in the buffer, this is return parameter
 * @param buffer_end a position in the buffer
 */
static void
remove_buffer_tag(GtkTextBuffer * text_buffer, GtkTextTag * tag,
                  GtkTextIter * start, GtkTextIter * end,
                  GtkTextIter * buffer_end)
{
    *end = *start;
    if (gtk_text_iter_forward_to_tag_toggle(end, tag))
    {
        if (gtk_text_iter_compare(end, buffer_end) > 0)
            *end = *buffer_end;
    }
    else
        *end = *buffer_end;

    gtk_text_buffer_remove_tag(text_buffer, tag, start, end);
}

/**
 * Change the font tags in <i>buffer</i> between the <i>start</i> and
 * <i>end</i> interval to the new size <i>size</i> and position <i>pos</i>
 * @param buffer is a #GtkTextBuffer
 * @param start a position in the buffer
 * @param end a position in the buffer, this is return parameter
 * @param size new font size
 * @param size new text position or <b>NULL</b>
 */
static void
change_font_tags(WPTextBuffer * buffer, GtkTextIter * start,
                 GtkTextIter * end, gint size, TextPosition * pos)
{
    GSList *tags, *tags_head;
    GtkTextTag *tag = NULL;
    GtkTextIter tmp, tmp_end;
    GtkTextBuffer *text_buffer;
    struct _WPTextBufferPrivate *priv;
    gint n;
    gboolean font_size_tag_found = FALSE;

    g_return_if_fail(buffer);

    tags_head = tags = gtk_text_iter_get_tags(start);
    tmp = *start;
    text_buffer = GTK_TEXT_BUFFER(buffer);
    priv = buffer->priv;

    do
    {
        while (tags)
        {
            tag = GTK_TEXT_TAG(tags->data);
            tags = tags->next;

            if (tag->values->font)
            {
                if (check_tag_type(tag, WPT_FONT_SIZE, &n) &&
                    (!pos || *pos != TEXT_POSITION_NORMAL))
                {
                    remove_buffer_tag(text_buffer, tag, &tmp, &tmp_end, end);
                    font_size_tag_found = TRUE;

                    if (!pos)
                        gtk_text_buffer_apply_tag(text_buffer,
                                                  priv->
                                                  font_size_tags[size],
                                                  &tmp, &tmp_end);
                    else if (*pos == TEXT_POSITION_SUBSCRIPT)
                        gtk_text_buffer_apply_tag(text_buffer,
                                                  priv->
                                                  font_size_sub_tags[n],
                                                  &tmp, &tmp_end);
                    else
                        gtk_text_buffer_apply_tag(text_buffer,
                                                  priv->
                                                  font_size_sup_tags[n],
                                                  &tmp, &tmp_end);
                }
                else if (check_tag_type(tag, WPT_SUB_SRPT, &n)
                         && (!pos || *pos != TEXT_POSITION_SUBSCRIPT))
                {
                    remove_buffer_tag(text_buffer, tag, &tmp, &tmp_end, end);
                    font_size_tag_found = TRUE;

                    if (!pos)
                        gtk_text_buffer_apply_tag(text_buffer,
                                                  priv->
                                                  font_size_sub_tags[size],
                                                  &tmp, &tmp_end);
                    else if (*pos == TEXT_POSITION_NORMAL)
                        gtk_text_buffer_apply_tag(text_buffer,
                                                  priv->font_size_tags[n],
                                                  &tmp, &tmp_end);
                    else
                        gtk_text_buffer_apply_tag(text_buffer,
                                                  priv->
                                                  font_size_sup_tags[n],
                                                  &tmp, &tmp_end);
                }
                else if (check_tag_type(tag, WPT_SUP_SRPT, &n)
                         && (!pos || *pos != TEXT_POSITION_SUPERSCRIPT))
                {
                    remove_buffer_tag(text_buffer, tag, &tmp, &tmp_end, end);
                    font_size_tag_found = TRUE;

                    if (!pos)
                        gtk_text_buffer_apply_tag(text_buffer,
                                                  priv->
                                                  font_size_sup_tags[size],
                                                  &tmp, &tmp_end);
                    else if (*pos == TEXT_POSITION_NORMAL)
                        gtk_text_buffer_apply_tag(text_buffer,
                                                  priv->font_size_tags[n],
                                                  &tmp, &tmp_end);
                    else
                        gtk_text_buffer_apply_tag(text_buffer,
                                                  priv->
                                                  font_size_sub_tags[n],
                                                  &tmp, &tmp_end);
                }
            }
        }

        g_slist_free(tags_head);

        if (!gtk_text_iter_forward_to_tag_toggle(&tmp, NULL) ||
            (gtk_text_iter_compare(&tmp, end) >= 0))
            break;

        tags_head = tags = gtk_text_iter_get_toggled_tags(&tmp, TRUE);
    } while (1);

    /* If no font size tag was found, we need to set at least one new font
     * size tag */
    if (!font_size_tag_found)
    {
        tmp = *start;

        if (gtk_text_iter_compare (start, end) == 0)
        {
            gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(buffer), &tmp_end);
        }
        else
        {
              tmp_end = *end;
        }

        if (!pos || *pos == TEXT_POSITION_NORMAL)
            gtk_text_buffer_apply_tag(text_buffer,
                                      priv->
                                      font_size_tags[size], &tmp, &tmp_end);
        else if (*pos == TEXT_POSITION_SUBSCRIPT)
            gtk_text_buffer_apply_tag(text_buffer,
                                      priv->
                                      font_size_sub_tags[size],
                                      &tmp, &tmp_end);
        else
            gtk_text_buffer_apply_tag(text_buffer,
                                      priv->
                                      font_size_sup_tags[size],
                                      &tmp, &tmp_end);
    }
}

static gboolean
wp_text_buffer_apply_attributes(WPTextBuffer * buffer, GtkTextIter * start,
                                GtkTextIter * end, gboolean undo,
                                WPTextBufferFormat * fmt)
{
    WPTextBufferFormatChangeSet cs;
    GtkTextTag **ttags;
    GtkTextIter siter, eiter;
    GtkTextBuffer *text_buffer;
    gboolean set_justification = FALSE;
    gboolean clear_set = FALSE;
    gboolean buffer_end = FALSE;
    gboolean result = FALSE;
    WPTextBufferPrivate *priv = buffer->priv;

    gtk_text_buffer_begin_user_action(GTK_TEXT_BUFFER(buffer));

    wp_text_buffer_check_apply_tag(buffer);

    if (!fmt)
    {
        fmt = &buffer->priv->fmt;
        clear_set = TRUE;
    }
    cs = fmt->cs;
    if (*(gint *) & cs != 0)
    {
        ttags = buffer->priv->tags;
        text_buffer = GTK_TEXT_BUFFER(buffer);

        if (gtk_text_iter_ends_tag(start, buffer->priv->tags[WPT_BULLET]))
        {
            gtk_text_iter_backward_char(start);
            _wp_text_iter_skip_bullet(start, buffer->priv->tags[WPT_BULLET],
                                      FALSE);
        }

        if (cs.justification)
        {
            siter = *start;
            eiter = *end;

            if (undo || fmt->bullet)
                gtk_text_iter_set_line_offset(&siter, 0);
            if (undo)
            {
                if (!gtk_text_iter_ends_line(&eiter))
                {
                    gtk_text_iter_forward_to_line_end(&eiter);
                    gtk_text_iter_forward_char(&eiter);
                }			
            }

            if ((buffer_end = gtk_text_iter_is_end(&eiter)))
                emit_default_justification_changed(buffer,
                                                   fmt->justification);

            if (gtk_text_iter_equal(&siter, &eiter))
            {
                if (buffer_end)
                {
                    gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(buffer));
                    return result;
                }
                else
                {
                    set_justification = TRUE;
		     result = TRUE;
		     gtk_text_iter_forward_char(&eiter);
		    	 
                     if (undo)
                         wp_undo_apply_tag(buffer->priv->undo, &siter, &eiter,
                                           NULL, FALSE);
     
                     gtk_text_buffer_set_modified(text_buffer, TRUE);
     
                     cs.justification = FALSE;
                     /* printf("Line: %d-%d\n", gtk_text_iter_get_offset(&siter),
                      * gtk_text_iter_get_offset(&eiter)); */
                     if (fmt->justification == GTK_JUSTIFY_LEFT)
                         gtk_text_buffer_apply_tag(text_buffer,
                                                   ttags[WPT_LEFT], &siter,
                                                   &eiter);
                     else if (undo)
                         gtk_text_buffer_remove_tag(text_buffer,
                                                    ttags[WPT_LEFT], &siter,
                                                    &eiter);
                     if (fmt->justification == GTK_JUSTIFY_CENTER)
                         gtk_text_buffer_apply_tag(text_buffer,
                                                   ttags[WPT_CENTER], &siter,
                                                   &eiter);
                     else if (undo)
                         gtk_text_buffer_remove_tag(text_buffer,
                                                    ttags[WPT_CENTER], &siter,
                                                    &eiter);
                     if (fmt->justification == GTK_JUSTIFY_RIGHT)
                         gtk_text_buffer_apply_tag(text_buffer,
                                                   ttags[WPT_RIGHT], &siter,
                                                   &eiter);
                     else if (undo)
                         gtk_text_buffer_remove_tag(text_buffer,
                                                    ttags[WPT_RIGHT], &siter,
                                                    &eiter);			
                }			
            }
            else
            {
                // TODO: It would be better if we only save the justification
                result = TRUE;

                if (undo)
                    wp_undo_apply_tag(buffer->priv->undo, &siter, &eiter,
                                      NULL, FALSE);

                gtk_text_buffer_set_modified(text_buffer, TRUE);

                cs.justification = FALSE;
                /* printf("Line: %d-%d\n", gtk_text_iter_get_offset(&siter),
                 * gtk_text_iter_get_offset(&eiter)); */
                if (fmt->justification == GTK_JUSTIFY_LEFT)
                    gtk_text_buffer_apply_tag(text_buffer,
                                              ttags[WPT_LEFT], &siter,
                                              &eiter);
                else if (undo)
                    gtk_text_buffer_remove_tag(text_buffer,
                                               ttags[WPT_LEFT], &siter,
                                               &eiter);
                if (fmt->justification == GTK_JUSTIFY_CENTER)
                    gtk_text_buffer_apply_tag(text_buffer,
                                              ttags[WPT_CENTER], &siter,
                                              &eiter);
                else if (undo)
                    gtk_text_buffer_remove_tag(text_buffer,
                                               ttags[WPT_CENTER], &siter,
                                               &eiter);
                if (fmt->justification == GTK_JUSTIFY_RIGHT)
                    gtk_text_buffer_apply_tag(text_buffer,
                                              ttags[WPT_RIGHT], &siter,
                                              &eiter);
                else if (undo)
                    gtk_text_buffer_remove_tag(text_buffer,
                                               ttags[WPT_RIGHT], &siter,
                                               &eiter);
            }
        }

        if ((*(gint *) & cs) != 0 && !gtk_text_iter_equal(start, end))
        {
            gtk_text_buffer_set_modified(text_buffer, TRUE);
            result = TRUE;

            if (undo)
                wp_undo_apply_tag(buffer->priv->undo, start, end, NULL,
                                  FALSE);

            if (cs.bold)
            {
                if (fmt->bold)
                    gtk_text_buffer_apply_tag(text_buffer, ttags[WPT_BOLD],
                                              start, end);
                else
                    gtk_text_buffer_remove_tag(text_buffer,
                                               ttags[WPT_BOLD], start, end);
            }
            if (cs.italic)
            {
                if (fmt->italic)
                    gtk_text_buffer_apply_tag(text_buffer,
                                              ttags[WPT_ITALIC], start, end);
                else
                    gtk_text_buffer_remove_tag(text_buffer,
                                               ttags[WPT_ITALIC], start, end);
            }
            if (cs.underline)
            {
                if (fmt->underline)
                    gtk_text_buffer_apply_tag(text_buffer,
                                              ttags[WPT_UNDERLINE], start,
                                              end);
                else
                    gtk_text_buffer_remove_tag(text_buffer,
                                               ttags[WPT_UNDERLINE], start,
                                               end);
            }
            if (cs.strikethrough)
            {
                if (fmt->strikethrough)
                    gtk_text_buffer_apply_tag(text_buffer,
                                              ttags[WPT_STRIKE], start, end);
                else
                    gtk_text_buffer_remove_tag(text_buffer,
                                               ttags[WPT_STRIKE], start, end);
            }
            // maybe for the black color don't need to have a separate tag!
            if (cs.color)
            {
                if (undo)
                    remove_tags_with_id(buffer, start, end, WPT_FORECOLOR);

                if (fmt->color.red || fmt->color.blue || fmt->color.green)
                {
                    GtkTextTag *tag =
                        color_buffer_get_tag(buffer->priv->color_tags,
                                             &fmt->color,
                                             ttags[WPT_RIGHT]->priority + 1);

                    gtk_text_buffer_apply_tag(text_buffer, tag, start, end);
                    g_hash_table_insert(priv->tag_hash, tag, NULL);
                }
            }
            if (cs.font)
            {
                if (undo)
                    remove_tags_with_id(buffer, start, end, WPT_FONT);
                gtk_text_buffer_apply_tag(text_buffer,
                                          buffer->priv->fonts[fmt->font],
                                          start, end);
            }
            if (cs.font_size && cs.text_position)
            {
                if (undo)
                    remove_tags_with_id(buffer, start, end,
                                        WPT_ALL_FONT_SIZE);
                switch (fmt->text_position)
                {
                    case TEXT_POSITION_NORMAL:
                        gtk_text_buffer_apply_tag(text_buffer,
                                                  buffer->priv->
                                                  font_size_tags[fmt->
                                                                 font_size],
                                                  start, end);
                        break;
                    case TEXT_POSITION_SUPERSCRIPT:
                        gtk_text_buffer_apply_tag(text_buffer,
                                                  buffer->priv->
                                                  font_size_sup_tags[fmt->
                                                                     font_size],
                                                  start, end);
                        break;
                    case TEXT_POSITION_SUBSCRIPT:
                        gtk_text_buffer_apply_tag(text_buffer,
                                                  buffer->priv->
                                                  font_size_sub_tags[fmt->
                                                                     font_size],
                                                  start, end);
                        break;
                }
            }
            else if ((cs.font_size && !cs.text_position) ||
                     (!cs.font_size && cs.text_position))
            {
                change_font_tags(buffer, start, end, fmt->font_size,
                                 cs.font_size ? NULL : &fmt->text_position);
            }
        }

        if (clear_set)
        {
            *(gint *) & buffer->priv->fmt.cs = 0;
            if (set_justification)
                buffer->priv->fmt.cs.justification = TRUE;
        }
    }
    gtk_text_buffer_end_user_action(GTK_TEXT_BUFFER(buffer));

    return result;
}

void
wp_text_buffer_insert_with_attribute(WPTextBuffer * buffer,
                                     GtkTextIter * pos, gchar * text,
                                     gint len, WPTextBufferFormat * fmt,
                                     gboolean disable_undo)
{
    g_return_if_fail(WP_IS_TEXT_BUFFER(buffer));
    gint offset;
    GtkTextIter start, end;
    GtkTextBuffer *text_buffer = GTK_TEXT_BUFFER(buffer);
    WPTextBufferPrivate *priv = buffer->priv;

    if (disable_undo)
        wp_undo_freeze(priv->undo);

    gtk_text_buffer_begin_user_action(text_buffer);

    offset = gtk_text_iter_get_offset(pos);
    gtk_text_buffer_insert(text_buffer, pos, text, len);
    gtk_text_buffer_get_iter_at_offset(text_buffer, &start, offset);

    wp_text_buffer_apply_attributes(buffer, &start, pos, !disable_undo, fmt);

    if (fmt->bullet)
    {
        GtkTextTag *tag = _wp_text_buffer_get_bullet_tag(buffer);
        if (_wp_text_iter_put_bullet_line(&start, tag))
        {
            end = start;
            gtk_text_iter_set_line_offset(&start, 0);
            wp_text_buffer_apply_attributes(buffer, &start, &end,
                                            !disable_undo, fmt);
        }
    }

    gtk_text_buffer_end_user_action(text_buffer);

    if (disable_undo)
        wp_undo_thaw(buffer->priv->undo);
}

void
wp_text_buffer_insert_image(WPTextBuffer * buffer,
                            GtkTextIter * pos,
                            const gchar * image_id, GdkPixbuf * pixbuf)
{
    GtkTextTag *pixbuf_tag;
    GtkTextTagTable *tag_table;
    GtkTextIter iter2;
    gchar *tag_id;
    gchar *img_id_copy = NULL;
    g_return_if_fail(WP_IS_TEXT_BUFFER(buffer));
    g_return_if_fail(image_id);

    img_id_copy = g_strdup(image_id);

    tag_id = g_strdup_printf("image-tag-%s", image_id);
    tag_table = gtk_text_buffer_get_tag_table (GTK_TEXT_BUFFER (buffer));
    pixbuf_tag = gtk_text_tag_table_lookup (tag_table, tag_id);
    if (pixbuf_tag == NULL)
        pixbuf_tag = gtk_text_buffer_create_tag (GTK_TEXT_BUFFER (buffer), tag_id, NULL);
    g_object_set_data_full (G_OBJECT (pixbuf_tag), "image-index", img_id_copy, (GDestroyNotify) g_free);

    g_object_set_data(G_OBJECT(pixbuf_tag), "image-set",
                      GINT_TO_POINTER(TRUE));
    gtk_text_buffer_insert_pixbuf(GTK_TEXT_BUFFER(buffer), pos, pixbuf);
    iter2 = *pos;
    gtk_text_iter_backward_char(&iter2);
    gtk_text_buffer_apply_tag(GTK_TEXT_BUFFER(buffer), pixbuf_tag, &iter2,
                              pos);
    g_free(tag_id);
    buffer->priv->queue_undo_reset = TRUE;
    wp_undo_reset (buffer->priv->undo);

}

void wp_text_buffer_replace_image (WPTextBuffer *buffer,
                                  const gchar *image_id,
                                  GdkPixbuf *pixbuf)
{
    GtkTextIter iter, start, end;
    gchar *replace_tag_id;
    g_return_if_fail (WP_IS_TEXT_BUFFER(buffer));
    g_return_if_fail (image_id);
    GtkTextTagTable *tag_table;
    GtkTextTag *tag;

    replace_tag_id = g_strdup_printf ("image-tag-replace-%s", image_id);

    tag_table = gtk_text_buffer_get_tag_table (GTK_TEXT_BUFFER (buffer));
    tag = gtk_text_tag_table_lookup (tag_table, (const gchar *) replace_tag_id);

    if (tag) {
       gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (buffer), &iter);

       wp_undo_freeze (buffer->priv->undo);
       while (!gtk_text_iter_is_end (&iter)) {
           if (gtk_text_iter_begins_tag (&iter, tag)) {
               GtkTextIter end;
               wp_text_buffer_insert_image (buffer, &iter, image_id, pixbuf);
               end = iter;
               gtk_text_iter_forward_char (&iter);
               gtk_text_buffer_delete(GTK_TEXT_BUFFER (buffer), &iter, &end);
           }
           gtk_text_iter_forward_char (&iter);
       }
       wp_undo_thaw (buffer->priv->undo);
       gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (buffer), &start);
       gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (buffer), &end);
       gtk_text_buffer_remove_tag (GTK_TEXT_BUFFER (buffer), tag, &start, &end);
    }
}

void wp_text_buffer_insert_image_replacement (WPTextBuffer *buffer,
                                             GtkTextIter *pos,
                                             const gchar *image_id)
{
    GtkTextTag *pixbuf_tag;
    GtkTextTagTable *tag_table;
    gchar *tag_id;
    //gchar * img_id_copy = NULL;
    g_return_if_fail(WP_IS_TEXT_BUFFER(buffer));
    g_return_if_fail (image_id);
    
    //img_id_copy = g_strdup (image_id);

    tag_id = g_strdup_printf("image-tag-replace-%s", image_id);
    tag_table = gtk_text_buffer_get_tag_table (GTK_TEXT_BUFFER (buffer));
    pixbuf_tag = gtk_text_tag_table_lookup (tag_table, tag_id);
    if (pixbuf_tag == NULL)
        pixbuf_tag = gtk_text_buffer_create_tag (GTK_TEXT_BUFFER (buffer), tag_id, NULL);
    gtk_text_buffer_insert_with_tags (GTK_TEXT_BUFFER (buffer), pos, " ", 1, pixbuf_tag, NULL);
    g_free (tag_id);
}



void
wp_text_buffer_delete_image(WPTextBuffer * buffer, const gchar * image_id)
{
    GtkTextTagTable *tag_table;
    gchar *tag_id;
    GtkTextTag *tag;
    GtkTextIter start, end;
    g_return_if_fail(WP_IS_TEXT_BUFFER(buffer));

    tag_table = gtk_text_buffer_get_tag_table(GTK_TEXT_BUFFER(buffer));
    tag_id = g_strdup_printf("image-tag-%s", image_id);
    tag = gtk_text_tag_table_lookup(tag_table, tag_id);
    g_free(tag_id);

    if (tag != NULL)
    {
        gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(buffer), &start);
        gtk_text_iter_forward_to_tag_toggle(&start, tag);
        end = start;
        gtk_text_iter_forward_to_tag_toggle(&end, tag);
        gtk_text_buffer_remove_tag(GTK_TEXT_BUFFER(buffer), tag, &start,
                                   &end);
        gtk_text_buffer_delete(GTK_TEXT_BUFFER(buffer), &start, &end);
    }
    buffer->priv->queue_undo_reset = TRUE;
    wp_undo_reset (buffer->priv->undo);
}


gboolean
wp_text_buffer_set_attribute(WPTextBuffer * buffer, gint tagid, gpointer data)
{
    g_return_val_if_fail(WP_IS_TEXT_BUFFER(buffer), FALSE);
    WPTextBufferPrivate *priv = buffer->priv;
    gboolean enable = (gboolean) data;

    // wp_undo_reset_mergeable(buffer->priv->undo);

    switch (tagid)
    {
        case WPT_BOLD:
            priv->fmt.bold = enable;
            priv->fmt.cs.bold = TRUE;
            break;
        case WPT_ITALIC:
            priv->fmt.italic = enable;
            priv->fmt.cs.italic = TRUE;
            break;
        case WPT_UNDERLINE:
            priv->fmt.underline = enable;
            priv->fmt.cs.underline = TRUE;
            break;
        case WPT_STRIKE:
            priv->fmt.strikethrough = enable;
            priv->fmt.cs.strikethrough = TRUE;
            break;
        case WPT_SUP_SRPT:
            priv->fmt.text_position =
                enable ? TEXT_POSITION_SUPERSCRIPT : TEXT_POSITION_NORMAL;
            priv->fmt.cs.text_position = TRUE;
            break;
        case WPT_SUB_SRPT:
            priv->fmt.text_position =
                enable ? TEXT_POSITION_SUBSCRIPT : TEXT_POSITION_NORMAL;
            priv->fmt.cs.text_position = TRUE;
            break;
        case WPT_LEFT:
            priv->fmt.justification = GTK_JUSTIFY_LEFT;
            priv->fmt.cs.justification = TRUE;
            break;
        case WPT_RIGHT:
            priv->fmt.justification = GTK_JUSTIFY_RIGHT;
            priv->fmt.cs.justification = TRUE;
            break;
        case WPT_CENTER:
            priv->fmt.justification = GTK_JUSTIFY_CENTER;
            priv->fmt.cs.justification = TRUE;
            break;
        case WPT_BULLET:
            if (enable)
                _wp_text_buffer_put_bullet(buffer);
            else
                _wp_text_buffer_remove_bullet(buffer);
            return TRUE;
            break;
        case WPT_FONT:
            priv->fmt.font = (gint) data;
            priv->fmt.cs.font = TRUE;
            break;
        case WPT_FONT_SIZE:
            priv->fmt.font_size = (gint) data;
            priv->fmt.cs.font_size = TRUE;
            break;
        case WPT_FORECOLOR:
            priv->fmt.color = *(GdkColor *) data;
            priv->fmt.cs.color = TRUE;
            break;
        default:
            // unknown id
            return FALSE;
    }

    return wp_text_buffer_set_format(buffer, NULL);

}

gboolean
wp_text_buffer_set_format(WPTextBuffer * buffer, WPTextBufferFormat * fmt)
{
    g_return_val_if_fail(WP_IS_TEXT_BUFFER(buffer), FALSE);
    GtkTextIter start, end;
    WPTextBufferPrivate *priv = buffer->priv;
    gboolean send = TRUE;

    wp_undo_reset_mergeable(priv->undo);

    if (fmt)
        priv->fmt = *fmt;

    if (!priv->is_empty || priv->fmt.cs.justification)
    {
        if (gtk_text_buffer_get_selection_bounds
            (GTK_TEXT_BUFFER(buffer), &start, &end)
            || priv->fmt.cs.justification)
            send =
                !wp_text_buffer_apply_attributes(buffer, &start, &end, TRUE,
                                                 NULL);
        else if (gtk_text_iter_inside_word(&start)
                 && !gtk_text_iter_starts_word(&start))
        {
            end = start;
            // if (!gtk_text_iter_starts_word(&start))
            gtk_text_iter_backward_word_start(&start);
            gtk_text_iter_forward_word_end(&end);
            send =
                !wp_text_buffer_apply_attributes(buffer, &start, &end, TRUE,
                                                 NULL);
        }
    }

    // printf("Attributes: %d\n", send);
    if (fmt && send)
        g_signal_emit(buffer, signals[REFRESH_ATTRIBUTES], 0);

    return !send;
}

/**
 * Update the toggled attributes in <i>cs</i> for the selection
 * between <i>start</i> and <i>end</i> in the <i>buffer</i>
 * @param buffer is a #GtkTextBuffer
 * @param start a position in the buffer
 * @param end a position in the buffer
 * @param fmt is a #WPTextBufferFormat which contains the formatting tags.
 * @param cs is a #WPTextBufferFormatChangeSet which contains the formatting tags
 *                     change set. It will be set if there is a tag toggle.
 */
static void
update_toggled_attributes(WPTextBuffer * buffer, GtkTextIter * start,
                          GtkTextIter * end, WPTextBufferFormat * fmt,
                          WPTextBufferFormatChangeSet * cs)
{
    GtkTextTag *tag;
    GtkTextTag **ttags;
    GtkTextIter iter;
    GSList *tags, *tags_head;
    gint bullet_last_line = -1, line;

    // printf("update_toggled_attributes\n");
    if (fmt->bullet)
        bullet_last_line = gtk_text_iter_get_line(start);

    ttags = buffer->priv->tags;
    iter = *start;
    while (gtk_text_iter_forward_to_tag_toggle(&iter, NULL))
    {
        if (gtk_text_iter_compare(&iter, end) >= 0)
            break;
        // printf("Offset: %d\n", gtk_text_iter_get_offset(&iter));
        tags_head = gtk_text_iter_get_toggled_tags(&iter, FALSE);
        tags = tags_head =
            g_slist_concat(tags_head,
                           gtk_text_iter_get_toggled_tags(&iter, TRUE));

        while (tags)
        {
            tag = GTK_TEXT_TAG(tags->data);
            tags = tags->next;

            // printf("Tag toggle: %p, %s\n", tag, tag->name ? tag->name :
            // "(null)");
            if (tag == ttags[WPT_BOLD])
                cs->bold = TRUE;
            else if (tag == ttags[WPT_ITALIC])
                cs->italic = TRUE;
            else if (tag == ttags[WPT_UNDERLINE])
                cs->underline = TRUE;
            else if (tag == ttags[WPT_STRIKE])
                cs->strikethrough = TRUE;
            else if (tag == ttags[WPT_BULLET] && !cs->bullet)
            {
                if (!fmt->bullet)
                    cs->bullet = TRUE;
                else
                {
                    line = gtk_text_iter_get_line(&iter);
                    if (line - bullet_last_line > 1)
                        cs->bullet = TRUE;
                    else
                        bullet_last_line = line;
                }
            }
            else if (tag->rise_set)
                cs->text_position = TRUE;
            else if (tag->justification_set)
                cs->justification = TRUE;
            else if (tag->fg_color_set)
                cs->color = TRUE;
            else if (!cs->font && tag->values->font
                     && check_tag_type(tag, WPT_FONT, NULL))
                cs->font = TRUE;
            else if (!cs->font_size && tag->values->font
                     && check_tag_type(tag, WPT_FONT_SIZE, NULL))
                cs->font_size = TRUE;
        }
        g_slist_free(tags_head);
    }
    if (fmt->bullet && !cs->bullet)
    {
        iter = *end;
        cs->bullet = !_wp_text_iter_has_bullet(&iter, ttags[WPT_BULLET]);
    }
}

static gboolean
_wp_text_buffer_get_attributes(WPTextBuffer * buffer,
                               WPTextBufferFormat * fmt,
                               gboolean set_changed, gboolean parse_selection)
{
    GtkTextBuffer *text_buffer;
    GtkTextIter start, end, tag_place, tmp;
    GtkTextTag *tag;
    GtkTextTag **ttags;
    GSList *tags, *tags_head;
    gboolean selection;
    gint n;

    g_return_val_if_fail(WP_IS_TEXT_BUFFER(buffer), FALSE);

    if (!fmt)
        return FALSE;

    // printf("wp_text_buffer_get_attributes\n");
    text_buffer = GTK_TEXT_BUFFER(buffer);
    selection = gtk_text_buffer_get_selection_bounds(text_buffer, &start, &end);
    if(!selection){
        GtkTextMark *mark;
        mark = gtk_text_buffer_get_insert(text_buffer);
        gtk_text_buffer_get_iter_at_mark(text_buffer, &start, mark);
        tag_place = end = start;
        gtk_text_iter_backward_char(&tag_place);
    } else
        tag_place = start;
 
    tags = tags_head = gtk_text_iter_get_tags(&tag_place);
    ttags = buffer->priv->tags;

    memset(fmt, 0, sizeof(WPTextBufferFormat));
    fmt->font_size = buffer->priv->default_fmt.font_size;
    fmt->font = buffer->priv->default_fmt.font;
    while (tags)
    {
        tag = GTK_TEXT_TAG(tags->data);
        tags = tags->next;

        // printf("Tag: %p, %s\n", tag, tag->name ? tag->name : "(null)");
        if (tag == ttags[WPT_BOLD])
        {
            fmt->bold = TRUE;
            fmt->cs.bold = set_changed;
        }
        else if (tag == ttags[WPT_ITALIC])
        {
            fmt->italic = TRUE;
            fmt->cs.italic = set_changed;
        }
        else if (tag == ttags[WPT_UNDERLINE])
        {
            fmt->underline = TRUE;
            fmt->cs.underline = set_changed;
        }
        else if (tag == ttags[WPT_STRIKE])
        {
            fmt->strikethrough = TRUE;
            fmt->cs.strikethrough = set_changed;
        }
        else if (tag->rise_set)
        {
            n = (gint) g_object_get_data(G_OBJECT(tag), WPT_ID);
            if (n >= WPT_SUB_SRPT)
            {
                fmt->text_position = TEXT_POSITION_SUBSCRIPT;
                fmt->font_size = n - WPT_SUB_SRPT;
            }
            else if (n >= WPT_SUP_SRPT)
            {
                fmt->text_position = TEXT_POSITION_SUPERSCRIPT;
                fmt->font_size = n - WPT_SUP_SRPT;
            }
            else
                continue;

            fmt->cs.text_position = set_changed;
            fmt->cs.font_size = set_changed;
        }
        else if (tag->justification_set)
        {
            fmt->justification =
                tag == ttags[WPT_LEFT] ? GTK_JUSTIFY_LEFT : tag ==
                ttags[WPT_CENTER] ? GTK_JUSTIFY_CENTER : GTK_JUSTIFY_RIGHT;
            fmt->cs.justification = set_changed;
        }
        else if (tag->fg_color_set)
        {
            fmt->color = tag->values->appearance.fg_color;
            fmt->cs.color = set_changed;
        }
        else if (tag->values->font && check_tag_type(tag, WPT_FONT, &n))
        {
            fmt->font = n;
            fmt->cs.font = set_changed;
        }
        else if (tag->values->font && check_tag_type(tag, WPT_FONT_SIZE, &n))
        {
            fmt->font_size = n;
            fmt->cs.text_position = set_changed;
            fmt->cs.font_size = set_changed;
        }
    }
    g_slist_free(tags_head);

    tmp = start; /*iter will be modifyed by asking for bullet*/
    fmt->bullet = _wp_text_iter_has_bullet(&tmp, ttags[WPT_BULLET]);

    if (gtk_text_iter_is_end(&end))
        fmt->justification = buffer->priv->last_line_justification;

    /*Changed as a FIX to bug# 91231*/
    if (selection && parse_selection && set_changed/*!set_changed*/)
        update_toggled_attributes(buffer, &start, &end, fmt, &fmt->cs);

    return selection && parse_selection && !set_changed;
}


void
wp_text_buffer_get_attributes(WPTextBuffer * buffer,
                              WPTextBufferFormat * fmt,
                              gboolean parse_selection)
{
    g_return_if_fail(WP_IS_TEXT_BUFFER(buffer));

    if (buffer->priv->is_empty)
    {
        buffer->priv->fmt.bullet = FALSE;
        *fmt = buffer->priv->fmt;
        memset(&fmt->cs, 0, sizeof(WPTextBufferFormatChangeSet));
    }
    else
    {
        if (!_wp_text_buffer_get_attributes(buffer, fmt, FALSE,
                                            parse_selection))
        {
            WPTextBufferFormat *old = &buffer->priv->fmt;
            WPTextBufferFormatChangeSet cs = old->cs;

            if (cs.bold)
                fmt->bold = old->bold;
            if (cs.italic)
                fmt->italic = old->italic;
            if (cs.underline)
                fmt->underline = old->underline;
            if (cs.strikethrough)
                fmt->strikethrough = old->strikethrough;
            if (cs.text_position)
                fmt->text_position = old->text_position;
            if (cs.color)
                fmt->color = old->color;
            if (cs.font)
                fmt->font = old->font;
            if (cs.font_size)
                fmt->font_size = old->font_size;
        }
    }
    fmt->rich_text = buffer->priv->is_rich_text;
}

void
wp_text_buffer_get_current_state(WPTextBuffer * buffer,
                                 WPTextBufferFormat * fmt)
{
    g_return_if_fail(WP_IS_TEXT_BUFFER(buffer));

    _wp_text_buffer_get_attributes(buffer, fmt, FALSE, FALSE);
    memset(&fmt->cs, 0, sizeof(WPTextBufferFormatChangeSet));
}

/********
 * Bullets
 */

gboolean
_wp_text_iter_is_bullet(GtkTextIter * iter, GtkTextTag * tag)
{
    return gtk_text_iter_has_tag(iter, tag);
}


gboolean
_wp_text_iter_skip_bullet(GtkTextIter * iter, GtkTextTag * tag,
                          gboolean forward)
{
    gboolean result;
    if ((result = gtk_text_iter_has_tag(iter, tag)))
    {
        if (forward)
        {
            if (!gtk_text_iter_ends_tag(iter, tag))
                gtk_text_iter_forward_to_tag_toggle(iter, tag);
            /* while (!gtk_text_iter_ends_tag(iter, tag))
             * gtk_text_iter_forward_char(iter); */
        }
        else
        {
            if (!gtk_text_iter_begins_tag(iter, tag))
                gtk_text_iter_backward_to_tag_toggle(iter, tag);
            /* while (!gtk_text_iter_begins_tag(iter, tag))
             * gtk_text_iter_backward_char(iter); */
        }
    }
    return result;
}

gboolean
_wp_text_iter_has_bullet(GtkTextIter * iter, GtkTextTag * tag)
{
    if (!gtk_text_iter_starts_line(iter))
        gtk_text_iter_set_line_offset(iter, 0);

    // return _wp_text_iter_is_bullet(iter, tag);
    return gtk_text_iter_toggles_tag(iter, tag);
}

gboolean
_wp_text_iter_put_bullet_line(GtkTextIter * iter, GtkTextTag * tag)
{
    gboolean result;

    if ((result = !_wp_text_iter_has_bullet(iter, tag)))
    {
        GtkTextBuffer *buffer = gtk_text_iter_get_buffer(iter);
        WP_TEXT_BUFFER(buffer)->priv->force_copy = TRUE;
        gtk_text_buffer_insert_with_tags(buffer,
                                         iter, "\xe2\x80\xa2\xc2\xa0\xc2\xa0",
                                         -1, tag, NULL);
    }
    return result;
}

void
_wp_text_iter_remove_bullet_line(GtkTextIter * iter, GtkTextTag * tag)
{
    GtkTextIter end;

    if (_wp_text_iter_has_bullet(iter, tag))
    {
        end = *iter;
        _wp_text_iter_skip_bullet(&end, tag, TRUE);
        gtk_text_buffer_delete(gtk_text_iter_get_buffer(iter), iter, &end);
    }
}

static void
_wp_text_buffer_put_bullet(WPTextBuffer * buffer)
{
    GtkTextIter iter, start, end;
    GtkTextTag *bullet = _wp_text_buffer_get_bullet_tag(buffer);
    GtkTextBuffer *text_buffer = GTK_TEXT_BUFFER(buffer);
    gint count;

    freeze_cursor_moved(buffer);

    gtk_text_buffer_begin_user_action(text_buffer);
    if (gtk_text_buffer_get_selection_bounds(text_buffer, &start, &end))
    {
        iter = start;
        count = gtk_text_iter_get_line(&end) - gtk_text_iter_get_line(&start);
        while (count-- >= 0)
        {
            _wp_text_iter_put_bullet_line(&iter, bullet);
            if (!gtk_text_iter_forward_line(&iter))
                break;
        }
    }
    else
    {
        /* gtk_text_buffer_get_iter_at_mark (text_buffer, &iter,
         * gtk_text_buffer_get_insert (text_buffer)); */
        _wp_text_iter_put_bullet_line(&start, bullet);
    }
    gtk_text_buffer_end_user_action(text_buffer);

    thaw_cursor_moved(buffer);
}


static void
_wp_text_buffer_remove_bullet(WPTextBuffer * buffer)
{
    GtkTextIter iter, start, end;
    GtkTextTag *bullet = _wp_text_buffer_get_bullet_tag(buffer);
    GtkTextBuffer *text_buffer = GTK_TEXT_BUFFER(buffer);
    gint count;

    freeze_cursor_moved(buffer);

    gtk_text_buffer_begin_user_action(text_buffer);
    if (gtk_text_buffer_get_selection_bounds(text_buffer, &start, &end))
    {
        iter = start;
        count = gtk_text_iter_get_line(&end) - gtk_text_iter_get_line(&start);
        while (count-- >= 0)
        {
            _wp_text_iter_remove_bullet_line(&iter, bullet);
            if (!gtk_text_iter_forward_line(&iter))
                break;
        }
    }
    else
    {
        /* gtk_text_buffer_get_iter_at_mark (text_buffer, &iter,
         * gtk_text_buffer_get_insert (text_buffer)); */
        _wp_text_iter_remove_bullet_line(&start, bullet);
    }
    gtk_text_buffer_end_user_action(text_buffer);

    thaw_cursor_moved(buffer);
}

GtkTextTag *
_wp_text_buffer_get_bullet_tag(WPTextBuffer * buffer)
{
    g_return_val_if_fail(WP_IS_TEXT_BUFFER(buffer), NULL);

    return WP_TEXT_BUFFER(buffer)->priv->tags[WPT_BULLET];
}

void
wp_text_buffer_undo(WPTextBuffer * buffer)
{
    g_return_if_fail(WP_IS_TEXT_BUFFER(buffer));

    wp_undo_undo(buffer->priv->undo);
    g_signal_emit(buffer, signals[REFRESH_ATTRIBUTES], 0);
}

void
wp_text_buffer_redo(WPTextBuffer * buffer)
{
    g_return_if_fail(WP_IS_TEXT_BUFFER(buffer));

    wp_undo_redo(buffer->priv->undo);
    g_signal_emit(buffer, signals[REFRESH_ATTRIBUTES], 0);
}


static void
wp_text_buffer_can_redo_cb(WPUndo * undo, gboolean enable, gpointer buffer)
{
    g_signal_emit(buffer, signals[CAN_REDO], 0, enable);
}

static void
wp_text_buffer_can_undo_cb(WPUndo * undo, gboolean enable, gpointer buffer)
{
    g_signal_emit(buffer, signals[CAN_UNDO], 0, enable);
}

static void
wp_text_buffer_format_changed_cb(WPUndo * undo, gboolean rich_text,
                                 WPTextBuffer * buffer)
{
    // printf("FCB: %d\n", rich_text);
    buffer->priv->is_rich_text = rich_text;
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(buffer), TRUE);
    g_signal_emit(buffer, signals[FMT_CHANGED], 0, rich_text);
    emit_default_font_changed(buffer);
}

static void
wp_text_buffer_last_line_justify_cb(WPUndo * undo,
                                    gint last_line_justification,
                                    WPTextBuffer * buffer)
{
    // printf("LLJ: %d\n", last_line_justification);
    emit_default_justification_changed(buffer, last_line_justification);
}

static void
wp_text_buffer_no_memory_cb(WPUndo * undo, WPTextBuffer * buffer)
{
    g_signal_emit(buffer, signals[NO_MEMORY], 0);
}

void
wp_text_buffer_enable_rich_text(WPTextBuffer * buffer, gboolean enable)
{
    GtkTextIter start, end;
    GtkTextBuffer *text_buffer = GTK_TEXT_BUFFER(buffer);
    WPTextBufferPrivate *priv = buffer->priv;
    gboolean rich_text = priv->is_rich_text != FALSE;

    if (enable != rich_text)
    {
        gtk_text_buffer_begin_user_action(text_buffer);
        gtk_text_buffer_get_start_iter(text_buffer, &start);
        gtk_text_buffer_get_end_iter(text_buffer, &end);
        wp_undo_format_changed(priv->undo, enable);
        buffer->priv->is_rich_text = enable;
        if (enable)
        {
            if (priv->is_empty)
                priv->fmt = priv->default_fmt;
            else
                wp_text_buffer_apply_attributes(buffer, &start, &end,
                                                FALSE,
                                                &priv->default_plain_fmt);
        }
        else
        {
            GtkTextIter found1, found2, found3;
            gint foundbackup;

            gtk_text_buffer_get_start_iter(text_buffer, &start);

            // Change the unicode char of the bullets to " * "
            while (gtk_text_iter_forward_search
                   (&start, "\xe2\x80\xa2\xc2\xa0\xc2\xa0", 0, &found1,
                    &found2, NULL))
            {

                // delete the unicode character
                foundbackup = gtk_text_iter_get_offset(&found1);
                gtk_text_buffer_delete(text_buffer, &found1, &found2);

                // Re-initialize the iterator
                gtk_text_buffer_get_start_iter(text_buffer, &found1);
                gtk_text_buffer_get_iter_at_offset(text_buffer, &found3,
                                                   foundbackup);

                // Insert the new text
                // gtk_text_buffer_insert (text_buffer, &found3, " * ", 3);

                gtk_text_buffer_get_iter_at_offset(text_buffer, &start,
                                                   foundbackup);
            }

            gtk_text_buffer_get_start_iter(text_buffer, &start);

            // Remove all the images
            while (gtk_text_iter_forward_search
                   (&start, "\xef\xbf\xbc", 0, &found1, &found2, NULL))
            {

                // delete the unicode character
                foundbackup = gtk_text_iter_get_offset(&found1);
                gtk_text_buffer_delete(text_buffer, &found1, &found2);

                // Re-initialize the iterator
                gtk_text_buffer_get_start_iter(text_buffer, &found1);
                gtk_text_buffer_get_iter_at_offset(text_buffer, &found3,
                                                   foundbackup);

                gtk_text_buffer_get_iter_at_offset(text_buffer, &start,
                                                   foundbackup);
            }

            wp_undo_freeze(priv->undo);
            gtk_text_buffer_get_start_iter(text_buffer, &start);
            gtk_text_buffer_get_end_iter(text_buffer, &end);

            gtk_text_buffer_remove_all_tags(text_buffer, &start, &end);

            wp_undo_thaw(priv->undo);
            emit_default_justification_changed(buffer, GTK_JUSTIFY_LEFT);
        }
        g_signal_emit(buffer, signals[FMT_CHANGED], 0, enable);
        g_signal_emit(buffer, signals[REFRESH_ATTRIBUTES], 0);
        emit_default_font_changed(buffer);
        
        /* Only mark modified if there is content in text buffer */
        {
        	gboolean empty;
        	g_object_get( buffer, "is_empty", &empty, NULL );
        	if( !empty ) {
        		gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(buffer), TRUE);
        	}
        }
        
        gtk_text_buffer_end_user_action(text_buffer);
    }
}

gboolean
wp_text_buffer_is_rich_text(WPTextBuffer * buffer)
{
    g_return_val_if_fail(WP_IS_TEXT_BUFFER(buffer), FALSE);

    return buffer->priv->is_rich_text;
}

gboolean
wp_text_buffer_is_modified(WPTextBuffer * buffer)
{
    g_return_val_if_fail(WP_IS_TEXT_BUFFER(buffer), FALSE);

    return gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(buffer));
}

static void
wp_text_buffer_resize_font(WPTextBuffer * buffer)
{
    gint i;
    gint font_size;
    gint actual_size;
    gint rise;
    WPTextBufferPrivate *priv = buffer->priv;
    double scale = priv->font_scaling_factor;

    if (scale == -1)
        scale = priv->font_scaling_factor;
    else
        priv->font_scaling_factor = scale;

    for (i = 0; i < WP_FONT_SIZE_COUNT; i++)
    {
        /* Normal size */
        font_size = wp_font_size[i];
        actual_size = iround(scale * font_size * PANGO_SCALE);
        g_object_set(G_OBJECT(priv->font_size_tags[i]),
                     "size", actual_size, NULL);

        /* Superscript size */
        actual_size = iround((scale * font_size *
                              PANGO_SCALE * SUP_SUB_SIZE_MULT) /
                             SUP_SUB_SIZE_DIV);
        rise =
            iround((scale * font_size * PANGO_SCALE *
                    SUP_RISE_MULT) / SUP_RISE_DIV);
        g_object_set(G_OBJECT(priv->font_size_sup_tags[i]),
                     "rise", rise, "size", actual_size, NULL);

        /* Subscript size */
        rise =
            -iround((scale * font_size * PANGO_SCALE *
                     SUB_RISE_MULT) / SUB_RISE_DIV);
        g_object_set(G_OBJECT(priv->font_size_sub_tags[i]),
                     "rise", rise, "size", actual_size, NULL);
    }
}

void
wp_text_buffer_set_font_scaling_factor(WPTextBuffer * buffer, double scale)
{
    WPTextBufferPrivate *priv = buffer->priv;

    if (priv->font_scaling_factor != scale)
    {
        priv->font_scaling_factor = scale;
        emit_default_font_changed(buffer);

        // If the fonts tags are created, resize them
        if (priv->font_size_tags[0])
            wp_text_buffer_resize_font(buffer);
    }
}

void
wp_text_buffer_set_background_color(WPTextBuffer * buffer,
                                    const GdkColor * color)
{
    WPTextBufferPrivate *priv = buffer->priv;

    if (priv->background_color != NULL)
        gdk_color_free(priv->background_color);

    if (color != NULL)
        priv->background_color = gdk_color_copy(color);

    emit_background_color_change(buffer);
}

const GdkColor *
wp_text_buffer_get_background_color(WPTextBuffer * buffer)
{
    WPTextBufferPrivate *priv = buffer->priv;

    return priv->background_color;
}

static void
emit_default_font_changed(WPTextBuffer * buffer)
{
    PangoFontDescription *desc;
    WPTextBufferPrivate *priv = buffer->priv;

    desc = pango_font_description_new();
    if (priv->is_rich_text)
    {
        /* printf("Font: %s, size: %d\n",
         * wp_get_font_name(priv->default_fmt.font),
         * wp_font_size[priv->default_fmt.font_size]); */

        pango_font_description_set_family_static(desc,
                                                 wp_get_font_name(priv->
                                                                  default_fmt.
                                                                  font));
        pango_font_description_set_size(desc,
                                        iround(priv->font_scaling_factor *
                                               wp_font_size[priv->default_fmt.
                                                            font_size] *
                                               PANGO_SCALE));
    }
    else
    {
        /* printf("Font: %s, size: %d\n",
         * wp_get_font_name(priv->default_plain_fmt.font),
         * wp_font_size[priv->default_plain_fmt.font_size]); */

        pango_font_description_set_family_static(desc,
                                                 wp_get_font_name(priv->
                                                                  default_plain_fmt.
                                                                  font));
        pango_font_description_set_size(desc,
                                        iround(priv->font_scaling_factor *
                                               wp_font_size[priv->
                                                            default_plain_fmt.
                                                            font_size] *
                                               PANGO_SCALE));
    }
    g_signal_emit(G_OBJECT(buffer), signals[DEF_FONT_CHANGED], 0, desc);
    pango_font_description_free(desc);

    wp_html_parser_update_default_attributes(priv->parser,
                                             &priv->default_fmt);
}

static void
emit_default_justification_changed(WPTextBuffer * buffer, gint justification)
{
    WPTextBufferPrivate *priv = buffer->priv;

    if (!priv->fast_mode && priv->last_line_justification != justification)
    {
        wp_undo_last_line_justify(priv->undo, priv->last_line_justification,
                                  justification);
        buffer->priv->last_line_justification = justification;
	/*NB#98464*/
        buffer->priv->fmt.justification = justification;
	/**/
        g_signal_emit(G_OBJECT(buffer), signals[DEF_JUSTIFICATION_CHANGED], 0,
                      justification);
    }
}

void
wp_text_buffer_reset_buffer(WPTextBuffer * buffer, gboolean rich_text)
{
    g_return_if_fail(WP_IS_TEXT_BUFFER(buffer));

    GtkTextBuffer *text_buffer = GTK_TEXT_BUFFER(buffer);
    WPTextBufferPrivate *priv = buffer->priv;

    wp_undo_freeze(priv->undo);

    gtk_text_buffer_set_text(text_buffer, "", -1);
    gtk_text_buffer_set_modified(text_buffer, FALSE);
    priv->fmt = priv->default_fmt;

    g_object_set(G_OBJECT(buffer), "rich_text", rich_text, NULL);
    g_object_set(G_OBJECT(buffer), "background_color", NULL, NULL);
    wp_undo_reset(priv->undo);

    emit_default_font_changed(buffer);
    emit_default_justification_changed(buffer, GTK_JUSTIFY_LEFT);

    if (!priv->fast_mode)
        g_signal_emit(G_OBJECT(buffer), signals[REFRESH_ATTRIBUTES], 0);

    wp_undo_thaw(priv->undo);
}

/**********************************************
 * Document loading
 **********************************************/

void
wp_text_buffer_load_document_begin(WPTextBuffer * buffer, gboolean html)
{
    g_return_if_fail(WP_IS_TEXT_BUFFER(buffer));

    buffer->priv->fast_mode = TRUE;
    wp_undo_freeze(buffer->priv->undo);
    wp_text_buffer_freeze(buffer);

    wp_text_buffer_reset_buffer(buffer, html);
    if (html)
        wp_html_parser_begin(buffer->priv->parser);
    else
        buffer->priv->last_utf8_size = 0;
}

void
wp_text_buffer_load_document_write(WPTextBuffer * buffer, gchar * data,
                                   gint size)
{
    g_return_if_fail(WP_IS_TEXT_BUFFER(buffer));
    WPTextBufferPrivate *priv = buffer->priv;

    if (buffer->priv->is_rich_text)
        wp_html_parser_write(priv->parser, data, size);
    else
    {
        gchar *invalid_offset, *p;
        guint len;
        GtkTextIter pos;

        /* Strip the \r */
        p = data;
        while ((p = memchr(p, '\r', (guint) size - (p - data))))
        {
            memmove(p, p + 1, (guint) size - (p + 1 - data));
            size--;
        }

        if (priv->last_utf8_size)
        {
            len =
                (guint) wp_html_parser_validate_invalid_utf8(priv->
                                                             last_utf8_invalid_char,
                                                             priv->
                                                             last_utf8_size,
                                                             data, size);
            if (data)
            {
                data += len;
                size -= len;
            }
            if (*priv->last_utf8_invalid_char)
            {
                gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(buffer), &pos);
                gtk_text_buffer_insert(GTK_TEXT_BUFFER(buffer),
                                       &pos, priv->last_utf8_invalid_char,
                                       -1);
            }
            priv->last_utf8_size = 0;
        }

        p = data;

        while (size > 0 && !g_utf8_validate(p, data + size - p,
                                            (const gchar **) &invalid_offset))
        {
            g_assert(data + size >= invalid_offset);
            len = g_utf8_skip[*(guchar *) invalid_offset];
            if (invalid_offset + len < data + size)
            {
                memmove(invalid_offset, invalid_offset + 1,
                        (guint) size - (invalid_offset + 1 - data));
                size--;
            }
            else
            {
                priv->last_utf8_size = (data + size) - invalid_offset;
                memcpy(priv->last_utf8_invalid_char, invalid_offset,
                       priv->last_utf8_size);
                size -= priv->last_utf8_size;
                break;
            }
            p = invalid_offset;
        }

        if (size > 0)
        {
            gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(buffer), &pos);
            gtk_text_buffer_insert(GTK_TEXT_BUFFER(buffer), &pos, data, size);
        }
    }
}

void
wp_text_buffer_load_document_end(WPTextBuffer * buffer)
{
    GtkTextBuffer *text_buffer;
    GtkTextIter pos;
    gint last_line_justification;

    g_return_if_fail(WP_IS_TEXT_BUFFER(buffer));

    if (buffer->priv->is_rich_text)
        last_line_justification = wp_html_parser_end(buffer->priv->parser);
    else
    {
        if (buffer->priv->last_utf8_size)
            wp_text_buffer_load_document_write(buffer, NULL, 0);
        last_line_justification = GTK_JUSTIFY_LEFT;
    }

    text_buffer = GTK_TEXT_BUFFER(buffer);

    gtk_text_buffer_get_start_iter(text_buffer, &pos);
    if (buffer->priv->is_rich_text)
        _wp_text_iter_skip_bullet(&pos, buffer->priv->tags[WPT_BULLET], TRUE);
    gtk_text_buffer_place_cursor(text_buffer, &pos);
    gtk_text_buffer_set_modified(text_buffer, FALSE);
    buffer->priv->cursor_moved = FALSE;

    wp_text_buffer_thaw(buffer);
    wp_undo_thaw(buffer->priv->undo);
    buffer->priv->fast_mode = FALSE;
    memset(&buffer->priv->fmt.cs, 0, sizeof(WPTextBufferFormatChangeSet));

    emit_default_justification_changed(buffer, last_line_justification);
    g_signal_emit(buffer, signals[REFRESH_ATTRIBUTES], 0);
}

/**********************************************
 * Document saving
 **********************************************/

/**
 * Reallocate <i>buffer</i> if needed.
 * @param buffer is a pointer to a character buffer
 * @param buffer_size is the size of the <i>buffer</i>
 * @param addsize is the requested new size to be added
 * @param cur is the current cursor in the buffer
 * @param source is the start of the source buffer
 * @param slen is the length of the source buffer
 */
static void
buffer_resize_if_needed(gchar ** buffer, gint * buffer_size, gint addsize,
                        gchar ** cur, gchar * source, gint slen)
{
    if (!*buffer)
    {
        *buffer_size = MAX(strlen(source) * 2, addsize + slen + 1);
        *buffer = g_realloc(NULL, *buffer_size);
        memcpy(*buffer, source, slen);
        *cur = *buffer + slen;
    }
    else
    {
        gint blen = *cur - *buffer;
        if (*buffer_size < blen + addsize)
        {
            *buffer_size = MAX(*buffer_size * 2, addsize + blen + 1);
            *buffer = g_realloc(*buffer, *buffer_size);
            *cur = *buffer + blen;
        }
    }
}

/**
 * Encode special characters in the buffer between <i>start</i> and <i>end</i> 
 * interval and call the <i>save</i> callback for it.
 * @param buffer is a #WPTextBuffer
 * @param start a position in the buffer
 * @param end a position in the buffer
 * @param save is the #WPDocumentSaveCallback callback
 * @param user_data is the user supplied pointer
 */
static gint
encode_text(GtkTextIter * start,
            GtkTextIter * end, WPDocumentSaveCallback save,
            gpointer user_data)
{
    gchar *text, *p, *buffer = NULL, *cur, *encoded = NULL;
    gint result = 0, n = 0;
    gint buffer_size;
    gboolean space = FALSE;

    text = gtk_text_iter_get_text(start, end);

    /* Bug NB#94890:If the first character is a space*/
    if ( text && text[0] == ' ')
	    space = TRUE;

    if (text && *text)
    {
        buffer_size = 0;

        for (p = text; *p; p++)
        {
            switch ((guchar) * p)
            {
                case '&':
                    encoded = "&amp;";
                    space = FALSE;
                    break;
                case '<':
                    encoded = "&lt;";
                    break;
                case '>':
                    encoded = "&gt;";
                    break;
                case '\t':
                    encoded = "&#9;";
                    break;
                case ' ':
                    if (space)
                        encoded = "&#32;";
                    break;
                case 0xc2:
                    if ((guchar) * (p + 1) == 0xa0)
                    {
                        encoded = "&nbsp;";
                        n = 1;
                    }
                    break;
            }

            space = *p == ' ';
            if (encoded || buffer)
            {
                gint len = encoded ? strlen(encoded) : 1;
                buffer_resize_if_needed(&buffer, &buffer_size, len,
                                        &cur, text, p - text);
                if (encoded)
                {
                    memcpy(cur, encoded, len);
                    encoded = NULL;
                    if (n)
                    {
                        p += n;
                        n = 0;
                    }
                }
                else
                    *cur = *p + n;
                cur += len;
            }
        }

        if (buffer)
        {
            buffer_resize_if_needed(&buffer, &buffer_size, 1, &cur, NULL, 0);
            *cur = 0;
        }
        else
        {
            buffer = text;
            text = NULL;
        }

        result = save(buffer, user_data);
        g_free(buffer);
    }
    g_free(text);

    return result;
}

/**
 * Get's the <i>tag</i> id, and parameters (ex. font size or color)
 * @param pointer to an array of #GtkTextTag
 * @param tag is a #GtkTextTag
 * @param id will contain the font size or font number if needed
 * @param color will contain the color if needed
 * @return the id of the tag
 */
static gint
convert_tag(GtkTextTag ** ttags, GtkTextTag * tag, gint * id,
            GdkColor * color)
{
    if (tag == ttags[WPT_BOLD])
        return TP_BOLD;
    else if (tag == ttags[WPT_ITALIC])
        return TP_ITALIC;
    else if (tag == ttags[WPT_UNDERLINE])
        return TP_UNDERLINE;
    else if (tag == ttags[WPT_STRIKE])
        return TP_STRIKE;
    else if (tag->rise_set)
    {
        *id = (gint) g_object_get_data(G_OBJECT(tag), WPT_ID);
        if (*id >= WPT_SUB_SRPT)
        {
            *id -= WPT_SUB_SRPT;
            return TP_SUBSCRIPT;
        }
        else if (*id >= WPT_SUP_SRPT)
        {
            *id -= WPT_SUP_SRPT;
            return TP_SUPERSCRIPT;
        }
    }
    else if (tag->fg_color_set)
    {
        *color = tag->values->appearance.fg_color;
        return TP_FONTCOLOR;
    }
    else if (tag->values->font && check_tag_type(tag, WPT_FONT, id))
        return TP_FONTNAME;
    else if (tag->values->font && check_tag_type(tag, WPT_FONT_SIZE, id))
        return TP_FONTSIZE;

    return TP_BOLD;
}

/**
 * Converts the list of <i>tags</i> to HTML tags, and call the
 * <i>save</i> callback.
 * @param priv is a pointer to #WPTextBufferPrivate
 * @param tags is a #GSList of #GtkTextTag
 * @param htags is an array of number containing the opened tags
 * @param save contains the user save callback which will be called, when a new
 *             chunk is available and has to be written
 * @param user_data user supplied user data, which will be forwarded
 *             to the save callback
 * @param opened is <b>TRUE</b> if the tags are opened
 * @return the result from the <i>save</i> callback
 */
static gint
write_tags(WPTextBufferPrivate * priv,
           GSList * tags, guchar * htags,
           WPDocumentSaveCallback save, gpointer user_data, gboolean opened)
{
    GSList *tmp;
    GtkTextTag *tag;
    GdkColor color;
    gint id, info;
    gchar *s;
    gint result = 0;

    tmp = tags;
    memset(&color, 0x00, sizeof(GdkColor));
    while (tmp && !result)
    {
        gpointer is_image_ptr;
        gint is_image;
        tag = GTK_TEXT_TAG(tmp->data);
        tmp = tmp->next;
        is_image_ptr = g_object_get_data(G_OBJECT(tag), "image-set");
        if (is_image_ptr == NULL)
            is_image = FALSE;
        else
            is_image = GPOINTER_TO_INT(is_image_ptr);
        if (opened && is_image)
        {
            gchar *image_id;
            gchar *html_image;
            image_id = g_object_get_data(G_OBJECT(tag), "image-index");
            html_image = g_strdup_printf("<img src=\"cid:%s\">", image_id);
            save(html_image, user_data);
            g_free(html_image);
        }
        else if (!tag->justification_set && tag != priv->tags[WPT_BULLET])
        {
            id = convert_tag(priv->tags, tag, &info, &color);

            if (!opened)
            {
                switch (id)
                {
                    case TP_FONTNAME:
                        if (info != priv->default_fmt.font)
                        {
                            result = save(html_close_tags[id], user_data);
                            htags[TP_FONTNAME]--;
                        }
                        break;
                    case TP_FONTSIZE:
                    case TP_SUBSCRIPT:
                    case TP_SUPERSCRIPT:
                        if (info != priv->default_fmt.font_size)
                        {
		             // s is not used here anymore so no need of it		
                            //s = g_strdup_printf(html_open_tags[TP_FONTSIZE],
                            //                    info);
                            result =
                                save(html_close_tags[TP_FONTSIZE], user_data);
                            htags[TP_FONTSIZE]--;
			    //g_free(s);
                        }
                        if (id != TP_FONTSIZE && !result)
                        {
                            result = save(html_close_tags[id], user_data);
                            htags[id]--;
                        }
                        break;
                    default:
                        result = save(html_close_tags[id], user_data);
                        htags[id]--;
                }
            }
            else
            {
                switch (id)
                {
                    case TP_FONTNAME:
                        if (info != priv->default_fmt.font)
                        {
                            s = g_strdup_printf(html_open_tags[id],
                                                wp_get_font_name(info));
                            result = save(s, user_data);
                            htags[id]++;
                            g_free(s);
                        }
                        break;
                    case TP_FONTSIZE:
                    case TP_SUBSCRIPT:
                    case TP_SUPERSCRIPT:
                        if (info != priv->default_fmt.font_size)
                        {
                            s = g_strdup_printf(html_open_tags[TP_FONTSIZE],
                                                info + 1);
                            result = save(s, user_data);
                            g_free(s);
                            htags[TP_FONTSIZE]++;
                        }
                        if (id != TP_FONTSIZE && !result)
                        {
                            result = save(html_open_tags[id], user_data);
                            htags[id]++;
                        }
                        break;
                    case TP_FONTCOLOR:
                        s = g_strdup_printf(html_open_tags[id],
                                            color.red >> 8,
                                            color.green >> 8,
                                            color.blue >> 8);
                        result = save(s, user_data);
                        htags[id]++;
                        g_free(s);
                        break;
                    default:
                        result = save(html_open_tags[id], user_data);
                        htags[id]++;
                }
            }
        }
    }
    g_slist_free(tags);
    return result;
}

/**
 * Begins a new paragraph. Writes all the opened tags at the <i>start</i> position
 * @param priv is a pointer to #WPTextBufferPrivate
 * @param start is a position in a buffer
 * @param htags is an array of number containing the opened tags
 * @param save contains the user save callback which will be called, when a new
 *             chunk is available and has to be written
 * @param user_data user supplied user data, which will be forwarded
 *             to the save callback
 * @return the result from the <i>save</i> callback
 */
static gint
begin_paragraph(WPTextBufferPrivate * priv,
                //GtkTextIter * start, guchar * htags,
		GtkTextIter * start, guchar * htags, gboolean *p_opened, gboolean *close_p,
                WPDocumentSaveCallback save, gpointer user_data)
{
    GSList *tags;
    GtkTextTag *tag;
    gint result = 0;

    tags = gtk_text_iter_get_tags(start);
    // tags = g_slist_reverse(tags);
    tag = find_justification_tag(tags, FALSE);

    if (tag && tag->values->justification != GTK_JUSTIFY_LEFT)
    {
        if (tag->values->justification == GTK_JUSTIFY_CENTER)
        //    result = save("<p align=center>", user_data);
	result = save(*p_opened?"</p><p align=center>":"<p align=center>", user_data);
        else
          //  result = save("<p align=right>", user_data);
       	      result = save(*p_opened?"</p><p align=right>":"<p align=right>", user_data);
	*p_opened = TRUE;
	*close_p = TRUE;
    }
   // else
    else if (*p_opened) {
        result = save("\n<br>", user_data);
	*close_p = FALSE;
    } else {
        result = save("<p>", user_data);
	*close_p = FALSE;
	*p_opened = TRUE;
    }

    if (!result)
        result = write_tags(priv, tags, htags, save, user_data, TRUE);
    return result;
}

/**
 * Ends a paragraph. Close all the opened tags.
 * @param priv is a pointer to #WPTextBufferPrivate
 * @param start is a position in a buffer
 * @param htags is an array of number containing the opened tags
 * @param save contains the user save callback which will be called, when a new
 *             chunk is available and has to be written
 * @param user_data user supplied user data, which will be forwarded
 *             to the save callback
 * @return the result from the <i>save</i> callback
 */
static gint
end_paragraph(WPTextBufferPrivate * priv,
           //   GtkTextIter * start, guchar * htags,
              GtkTextIter * start, guchar * htags, gboolean *p_opened, gboolean *close_p,
              WPDocumentSaveCallback save, gpointer user_data)
{
    gint i;
    gint result = 0;

    for (i = 0; i < TP_LAST && !result; i++)
        while (htags[i]-- > 0 && !result)
            result = save(html_close_tags[i], user_data);
  //  if (!result)
    if (*close_p) {
        result = save("</p>\n", user_data);
	*close_p = FALSE;
	*p_opened = FALSE;
    }
    return result;
}

gint
wp_text_buffer_save_document(WPTextBuffer * buffer,
                             WPDocumentSaveCallback save, gpointer user_data)
{
    g_return_val_if_fail(WP_IS_TEXT_BUFFER(buffer), 1);

    WPTextBufferPrivate *priv = buffer->priv;
    GtkTextBuffer *text_buffer = GTK_TEXT_BUFFER(buffer);
    GtkTextIter start, bend, end, tagtoggle;
    guchar htags[TP_LAST];
    gboolean bullet;
    gboolean list = FALSE;
    gboolean result = 0;
    gboolean p_opened = FALSE;
    gboolean close_p = FALSE;

    gtk_text_buffer_get_start_iter(text_buffer, &start);
    gtk_text_buffer_get_end_iter(text_buffer, &bend);
    if (priv->is_rich_text)
    {
        tagtoggle = start;

        result = save(html_header, user_data);
        if (buffer->priv->background_color)
        {
            gchar *tmp;
            gchar *bgcolor_str;

            bgcolor_str =
                g_strdup_printf("#%02x%02x%02x",
                                buffer->priv->background_color->red >> 8,
                                buffer->priv->background_color->green >> 8,
                                buffer->priv->background_color->blue >> 8);

            tmp = g_strdup_printf(body_bgcolor_start, bgcolor_str);
            save(tmp, user_data);
        }
        else
        {
            save(body_start, user_data);
        }
        if (!result)
            do
            {
                memset(htags, 0, sizeof(htags));

                bullet =
                    _wp_text_iter_skip_bullet(&start,
                                              buffer->priv->tags[WPT_BULLET],
                                              TRUE);
                if (bullet ^ list)
                {
                    if (list)
                        result = save("</ul>\n", user_data);
                    else
                        result = save("<ul>\n", user_data);

                    list = bullet;
                }
                if (!result && bullet)
                {
                    result = save("\t<li>", user_data);
                }
                if (!result){
                    /*result = begin_paragraph(priv, &start, htags, */
					result = begin_paragraph(priv, &start, htags, &p_opened, &close_p,
                                             save, user_data);
                }

                if (!result && !gtk_text_iter_ends_line(&start))
                {
                    end = start;
                    gtk_text_iter_forward_to_line_end(&end);

                    do
                    {
                        while (gtk_text_iter_compare(&tagtoggle, &start) <= 0)
                        {
                            if (!gtk_text_iter_forward_to_tag_toggle
                                (&tagtoggle, NULL))
                            {
                                tagtoggle = bend;
                                break;
                            }
                        }
                        if (gtk_text_iter_compare(&tagtoggle, &end) < 0)
                        {
                            result = encode_text(&start, &tagtoggle,
                                                 save, user_data);
                            if (!result)
                                result =
                                    write_tags(priv,
                                               gtk_text_iter_get_toggled_tags
                                               (&tagtoggle, FALSE), htags,
                                               save, user_data, FALSE);
                            if (!result)
                                result =
                                    write_tags(priv,
                                               gtk_text_iter_get_toggled_tags
                                               (&tagtoggle, TRUE), htags,
                                               save, user_data, TRUE);
                            start = tagtoggle;
                        }
                        else
                            break;
                    } while (!result);
                    if (!result)
                        result = encode_text(&start, &end, save, user_data);
                }
                if (!result)
 //                    result = end_paragraph(priv, &start, htags,
 			result = end_paragraph(priv, &start, htags, &p_opened, &close_p,
                                           save, user_data);
            } while (!result && gtk_text_iter_forward_line(&start));

        if (!result && list)
            result = save("</ul>\n", user_data);

        /* check if last line ends with newline */
        if (!result && !gtk_text_iter_is_start(&start))
        {
             gtk_text_iter_backward_char(&start);
             if (gtk_text_iter_ends_line(&start)) {
                // result = save("<p></p>\n", user_data);
		 if (p_opened){ 
		     result = save ("<br></p>\n", user_data);
		     p_opened = FALSE;
		     close_p = FALSE;
		 } else {
		     result = save("<p></p>\n", user_data);
		 }
             }
        }
	if (p_opened)
	    result = save("</p>\n", user_data);

        if (!result)
            result = save(html_footer, user_data);
    }
    else
    {
        gint offset;
        gchar *text;

        // Search for an existed BOM sign at the start of the text content
        GtkTextBuffer *buf;
        GtkTextIter BOMIter;
        gunichar BOM;

        buf = GTK_TEXT_BUFFER(text_buffer);
        gtk_text_buffer_get_start_iter(buf, &BOMIter);
        BOM = gtk_text_iter_get_char(&BOMIter);

        if (BOM != 65279)
            save("\xEF\xBB\xBF", user_data);

        offset = 0;
        do
        {
            offset += 20480;
            gtk_text_buffer_get_iter_at_offset(text_buffer, &end, offset);
            text = gtk_text_iter_get_text(&start, &end);
            if (text && *text)
                result = save(text, user_data);
            g_free(text);
            start = end;
        } while (!result && gtk_text_iter_compare(&end, &bend) < 0);
    }

    if (!result)
        gtk_text_buffer_set_modified(text_buffer, FALSE);

    return result;
}

void
wp_text_buffer_remember_tag(WPTextBuffer * buffer, gboolean enable)
{
    g_assert(WP_IS_TEXT_BUFFER(buffer));
    WPTextBufferPrivate *priv = buffer->priv;

    priv->remember_tag = enable;
}

void
debug_print_tags(GtkTextIter * giter, gint what)
{
    GSList *head, *iter;

    printf("==============\nTag list at %d, %d:\n",
           gtk_text_iter_get_offset(giter), what);
    if (what == 0)
        head = iter = gtk_text_iter_get_tags(giter);
    else
        head = iter = gtk_text_iter_get_toggled_tags(giter, what == 1);
    while (iter)
    {
        printf("  %s\n",
               GTK_TEXT_TAG(iter->data)->name ? GTK_TEXT_TAG(iter->data)->
               name : "(null)");
        iter = iter->next;
    }
    printf("-------------------\n");
    g_slist_free(head);
}

/**********************************************
 * Font and font size handling
 **********************************************/

const gchar **font_name_list;
const gchar **font_name_list_casefold;
static gint wp_font_num = 0;

/**
 * Compare two font families.
 * @param a is a pointer to a character pointer
 * @param b is a pointer to a character pointer
 * @return the result -1 if a < b, 0 if a = b or 1 if a > b
 *
static int
cmp_families(const void *a, const void *b)
{
    return g_utf8_collate(*(gchar **) a, *(gchar **) b);
} */

/**
 * Check if the supplied <i>name</i> is an internal font. Used for filtering.
 * @param name is a null terminated string
 * @return <b>TRUE</b> if is an internal font
 */
static gboolean
is_internal_font(const gchar * name)
{
    /* Maybe update this if more internals come in */
    return strcmp(name, "DeviceSymbols") == 0
        || strcmp(name, "Nokia Smiley") == 0
        || strcmp(name, "NewCourier") == 0
        || strcmp(name, "NewTimes") == 0
        || strcmp(name, "SwissA") == 0
        || strcmp(name, "Nokia Sans") == 0
        || strcmp(name, "Nokia Sans Cn") == 0;
}

void
wp_text_buffer_library_init()
{
    GdkScreen *screen;
    PangoContext *context;
    PangoFontFamily **families;
    int i, font_num, n;
    const gchar *name;

    screen = gdk_screen_get_default();
    context = gdk_pango_context_get_for_screen(screen);
    pango_context_list_families(context, &families, &font_num);

    font_name_list = g_new(const gchar *, font_num);
    n = 0;
    for (i = 0; i < font_num; i++)
    {
        name = pango_font_family_get_name(families[i]);

        if (!is_internal_font(name))
        {
            font_name_list[n++] = g_strdup(name);
        }
    }

    // reallocate memory for the "wasted space" to became usable again
    font_name_list = g_realloc(font_name_list, n * sizeof(gchar *));
    wp_font_num = n;

	/* For some changing order of fonts breaks font handling. This can be
	reenabled when the actual error is found, NB#70305
    qsort(font_name_list, n, sizeof(gchar *), cmp_families);
    */

    font_name_list_casefold = g_new(const gchar *, n);
    for (i = 0; i < wp_font_num; i++)
        font_name_list_casefold[i] = g_utf8_casefold(font_name_list[i], -1);

    g_free(families);
    g_object_unref(context);
}

void
wp_text_buffer_library_done()
{
    int i;

    for (i = 0; i < wp_font_num; i++)
    {
        g_free((gchar *) font_name_list[i]);
        g_free((gchar *) font_name_list_casefold[i]);
    }

    g_free(font_name_list);
    g_free(font_name_list_casefold);

    finalize_html_parser_library();
}

const gchar *
wp_get_font_name(gint index)
{
    if (!font_name_list)
        wp_text_buffer_library_init();

    if (index >= 0 && index < wp_font_num)
        return font_name_list[index];
    else
        return DEF_FONT;
}

gint
wp_get_font_index(const gchar * font_name, gint def)
{
    gint font;
    gchar *case_fold = g_utf8_casefold(font_name, -1);
    
    if (!font_name_list)
        wp_text_buffer_library_init();

    for (font = 0; font < wp_font_num; font++)
        if (g_utf8_collate(font_name_list_casefold[font], case_fold) == 0)
        {
            g_free(case_fold);
            return font;
        }
    g_free(case_fold);

    // g_warning("Non existing font: %s", font_name);
    return def;
}

gint
wp_get_font_count()
{
    if (!font_name_list)
        wp_text_buffer_library_init();

    return wp_font_num;
}

gint
wp_get_font_size_index(gint font_size, gint def)
{
    gint size, result = def;

    for (size = 0;
         size < WP_FONT_SIZE_COUNT && font_size >= wp_font_size[size]; size++)
        if (font_size >= wp_font_size[size])
            result = size;

    return result;
}

static void 
wp_text_buffer_insert_pixbuf (GtkTextBuffer *buffer,
			      GtkTextIter *location,
			      GdkPixbuf *pixbuf)
{
    GTK_TEXT_BUFFER_CLASS(wp_text_buffer_parent_class)->
	insert_pixbuf(buffer, location, pixbuf);
    
    ((WPTextBuffer *) buffer)->priv->queue_undo_reset = TRUE;
    
    /* Bug:#92190: When a character is typed after pixbuf*/
    ((WPTextBuffer *) buffer)->priv->last_is_insert = FALSE;
}
