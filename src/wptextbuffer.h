/**
 * @file wptextbuffer.h
 *
 * Header file for WordPad Text Buffer
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

#ifndef _WP_TEXT_BUFFER_H
#define _WP_TEXT_BUFFER_H

#include <gtk/gtktextbuffer.h>
#include <gtk/gtktexttag.h>

G_BEGIN_DECLS
#define WP_TYPE_TEXT_BUFFER              (wp_text_buffer_get_type ())
#define WP_TEXT_BUFFER(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), WP_TYPE_TEXT_BUFFER, WPTextBuffer))
#define WP_TEXT_BUFFER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), WP_TYPE_TEXT_BUFFER, WPTextBufferClass))
#define WP_IS_TEXT_BUFFER(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), WP_TYPE_TEXT_BUFFER))
#define WP_IS_TEXT_BUFFER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), WP_TYPE_TEXT_BUFFER))
#define WP_TEXT_BUFFER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), WP_TYPE_TEXT_BUFFER, WPTextBufferClass))
typedef struct _WPTextBuffer WPTextBuffer;
typedef struct _WPTextBufferPrivate WPTextBufferPrivate;
typedef struct _WPTextBufferClass WPTextBufferClass;

/** Constansts used for tag types */
enum {
    /** Tag contains bold style */
    WPT_BOLD = 0,
    /** Tag contains italic style */
    WPT_ITALIC,
    /** Tag contains underline style */
    WPT_UNDERLINE,
    /** Tag contains strikethrough style */
    WPT_STRIKE,
    /** Tag is an align to left style */
    WPT_LEFT,
    /** Tag is an align to center style */
    WPT_CENTER,
    /** Tag is an align to right style */
    WPT_RIGHT,
    /** Tag contains a bullet or number */
    WPT_BULLET,
    /** Tag contains a foreground color */
    WPT_FORECOLOR,
    WPT_LASTTAG,
    /** Tag is a font size modifier */
    WPT_FONT_SIZE = 1000,
    /** Tag is a superscript with font size modifier */
    WPT_SUP_SRPT = 2000,
    /** Tag is a subscript with font size modifier */
    WPT_SUB_SRPT = 3000,
    /** Tag is a font family modifier */
    WPT_FONT = 4000,
    /** Tag is a group of #WPT_FONT_SIZE, #WPT_SUP_SRPT and #WPT_SUB_SRPT */
    WPT_ALL_FONT_SIZE = 9000
};

/** Contains all the font sizes in points */
extern const gint wp_font_size[];

/** Number of font size */
#define WP_FONT_SIZE_COUNT 7

/** Scaling factor for superscript and subscript font size */
#define SUP_SUB_SIZE_MULT 3
#define SUP_SUB_SIZE_DIV 5

/** Scaling factor for superscript and subscript rise */
#define SUP_RISE_MULT 4
#define SUP_RISE_DIV 5
#define SUB_RISE_MULT 2
#define SUB_RISE_DIV 5

/** Text position type */
typedef enum {
    TEXT_POSITION_NORMAL = 0,
    TEXT_POSITION_SUPERSCRIPT,
    TEXT_POSITION_SUBSCRIPT
} TextPosition;

/** Format change set, used to notify when a specific style is set */
typedef struct {
    gint bold:1;
    gint italic:1;
    gint underline:1;
    gint strikethrough:1;
    gint justification:1;
    gint text_position:1;
    gint color:1;
    gint font_size:1;
    gint font:1;
    gint bullet:1;
} WPTextBufferFormatChangeSet;

/** Contains a format state */
typedef struct {
    gint bold:1;
    gint italic:1;
    gint underline:1;
    gint strikethrough:1;
    gint bullet:1;
    TextPosition text_position;
    gint justification;
    GdkColor color;
    gint font;
    gint font_size;
    gint rich_text:1;

    WPTextBufferFormatChangeSet cs;
} WPTextBufferFormat;

/** WPTextBuffer object */
struct _WPTextBuffer {
    GtkTextBuffer parent;

    WPTextBufferPrivate *priv;
};

/** WPTextBuffer class */
struct _WPTextBufferClass {
    GtkTextBufferClass parent_class;

    /**
     * Called when refresh attributes should happen
     * @param buffer pointer to a #WPTextBuffer
     */
    void (*refresh_attributes) (WPTextBuffer * buffer);
    /**
     * Called when the undo state has changed
     * @param buffer pointer to a #WPTextBuffer
     * @param can_undo is <b>TRUE</b> when there is something to undo
     */
    void (*can_undo) (WPTextBuffer * buffer, gboolean can_undo);
    /**
     * Called when the undo state has changed
     * @param buffer pointer to a #WPTextBuffer
     * @param can_redo is <b>TRUE</b> when there is something to redo
     */
    void (*can_redo) (WPTextBuffer * buffer, gboolean can_redo);
    /**
     * Called when the formatting has changed (rich text <-> plain text)
     * @param buffer pointer to a #WPTextBuffer
     * @param rich_text is <b>TRUE</b> when the buffer contains rich text
     */
    void (*fmt_changed) (WPTextBuffer * buffer, gboolean rich_text);
    /**
     * Called when the default font has been changed
     * @param buffer pointer to a #WPTextBuffer
     * @param desc pointer to a #PangoFontDescription containing the new font
     */
    void (*def_font_changed) (WPTextBuffer * buffer,
                              PangoFontDescription * desc);
    /**
     * Called when the default justification has been changed
     * @param buffer pointer to a #WPTextBuffer
     * @param alignment contains the new alignment
     */
    void (*def_justification_changed) (WPTextBuffer * buffer, gint alignment);
    /**
     * Called when the background color has been changed
     * @param buffer pointer to a #WPTextBuffer
     * @param color pointer to a #GdkColor containing the new color
     */
    void (*background_color_changed) (WPTextBuffer * buffer,
                                      const GdkColor * color);
    /**
     * Called when there is not enough memory to perform the operation
     * @param buffer pointer to a #WPTextBuffer
     */
    void (*no_memory) (WPTextBuffer * buffer);
};

/**
 * A save callback type used to save the buffer to an external format
 * @param buffer a <b>NULL</b> terminated character list containing the 
 *               text to be written
 * @param user_data contains a user supplied pointer
 */
typedef gint(*WPDocumentSaveCallback) (const gchar * buffer,
                                       gpointer user_data);

GType
wp_text_buffer_get_type(void)
    G_GNUC_CONST;

/**
 * Creates a new wordpad text buffer
 * @param table pointer to a #GtkTextTagTable or <b>NULL</b>
 * @return the newly created #WPTextBuffer
 */
  WPTextBuffer *wp_text_buffer_new(GtkTextTagTable * table);

/**
 * Queries the state of a selection in the buffer
 * @param buffer pointer to a #WPTextBuffer
 * @return <b>TRUE</b> if there is something selected
 */
  gboolean wp_text_buffer_has_selection(WPTextBuffer * buffer);

/**
 * Retrieve a specific tag from the buffer. The tag can be identified
 * with WPT_*
 * @param buffer pointer to a #WPTextBuffer
 * @param tagno contains the tag identifier. It should be between #WPT_BOLD and
 *              WPT_LASTTAG
 */
  GtkTextTag *wp_text_buffer_get_tag(WPTextBuffer * buffer, gint tagno);

/**
 * Get the formatting attributes from the text.
 * @param buffer pointer to a #WPTextBuffer
 * @param pointer to a #WPTextBufferFormat which will be filled with the current
 *                attributes from the cursor position
 * @param parse_selection means that the selection should be parsed, to detect
 *                if the tag is toggled multiple times. If it is toggled multiple
 *                times, then the fmt->cs.* will be set accordingly.
 */
  void wp_text_buffer_get_attributes(WPTextBuffer * buffer,
                                     WPTextBufferFormat * fmt,
                                     gboolean parse_selection);
/**
 * Get the formatting attributes from the text without parsing the selection
 * @param buffer pointer to a #WPTextBuffer
 * @param pointer to a #WPTextBufferFormat which will be filled with the current
 *                attributes from the cursor position
 */
  void wp_text_buffer_get_current_state(WPTextBuffer * buffer,
                                        WPTextBufferFormat * fmt);
/**
 * Modify the current attribute to the given one. If there is something selected,
 * the attribute is applied for the whole selection, otherwise if the cursor is
 * on a word (not at the end), then the attribute will be applied to the word
 * otherwise the attribute is just toggled, waiting to be applied to the next
 * entered character.
 * @param buffer pointer to a #WPTextBuffer
 * @param tagno contains the tag identifier.
 * @param data can contain different formats. <ul>
 *             <li>If tagid <= WPT_BULLET, then it is treated as (gboolean)data
 *             <li>If tagid == WPT_COLOR, then it is treated as (GdkColor *)data
 *             <li>If tagid == WPT_FONT or WPT_FONT_SIZE, then it is treated as (gint)data
 *          </ul>
 * @return <b>TRUE</b> if the attribute has been applied to a selection or a word
 */
  gboolean wp_text_buffer_set_attribute(WPTextBuffer * buffer, gint tagid,
                                        gpointer data);
/**
 * Same as #wp_text_buffer_set_attribute, only it can modify several attributes
 * at the same time. If fmt->cs.* is set then it will be applied using the same
 * rules.
 * @param buffer pointer to a #WPTextBuffer
 * @param fmt pointer to a #WPTextBufferFormat
 * @return <b>TRUE</b> if the attribute has been applied to a selection or a word
 */
  gboolean wp_text_buffer_set_format(WPTextBuffer * buffer,
                                     WPTextBufferFormat * fmt);

/**
 * Undo the last operation in the buffer.
 * @param buffer pointer to a #WPTextBuffer
 */
  void wp_text_buffer_undo(WPTextBuffer * buffer);
/**
 * Redo the last operation in the buffer.
 * @param buffer pointer to a #WPTextBuffer
 */
  void wp_text_buffer_redo(WPTextBuffer * buffer);

/**
 * Cursor movement detection and tag copying is freezed in the buffer.
 * It is a reference count, so it can be called several time.
 * @param buffer pointer to a #WPTextBuffer
 */
  void wp_text_buffer_freeze(WPTextBuffer * buffer);
/**
 * Cursor movement detection and tag copying is defreezed in the buffer
 * @param buffer pointer to a #WPTextBuffer
 */
  void wp_text_buffer_thaw(WPTextBuffer * buffer);

/**
 * Enable rich text in the buffer
 * @param buffer pointer to a #WPTextBuffer
 * @param rich_text is <b>TRUE</b> if rich text should be supported in the buffer
 */
  void wp_text_buffer_enable_rich_text(WPTextBuffer * buffer,
                                       gboolean enable);

/**
 * Queries the rich text state of the buffer
 * @param buffer pointer to a #WPTextBuffer
 * @return <b>TRUE</b> if the rich text is enabled
 */
  gboolean wp_text_buffer_is_rich_text(WPTextBuffer * buffer);

/**
 * Queries the modification state of the buffer
 * @param buffer pointer to a #WPTextBuffer
 * @return <b>TRUE</b> if the buffer contains modified text / attributes
 */
  gboolean wp_text_buffer_is_modified(WPTextBuffer * buffer);

/**
 * Set the font scaling factor to the buffer
 * @param buffer pointer to a #WPTextBuffer
 * @param scale the new font scaling factor
 */
  void wp_text_buffer_set_font_scaling_factor(WPTextBuffer * buffer,
                                              double scale);

/**
 * Set the current background color
 * @param buffer: pointer to a #WPTextBuffer
 * @param color: a #GdkColor
 *
 * establishes a new background color.
 */
  void wp_text_buffer_set_background_color(WPTextBuffer * buffer,
                                           const GdkColor * color);

/**
 * Get the current background color
 * @param buffer: pointer to a #WPTextBuffer
 *
 * obtains the current background color
 *
 * Returns: a #GdkColor.
 */
  const GdkColor *wp_text_buffer_get_background_color(WPTextBuffer * buffer);

/**
 * Insert a given text to a given position into the buffer, and apply the given
 * attributes to them.
 * @param buffer pointer to a #WPTextBuffer
 * @param pos contains the position iterator where the insert should happen
 * @param text a valid UTF-8 character array
 * @param len contains the length of the inserted text
 * @param fmt pointer to a #WPTextBufferFormat, holding the attributes of the new
 *            text
 * @param disable_undo if the undo should be disable during this complex operation
 */
  void wp_text_buffer_insert_with_attribute(WPTextBuffer * buffer,
                                            GtkTextIter * pos, gchar * text,
                                            gint len,
                                            WPTextBufferFormat * fmt,
                                            gboolean disable_undo);

/**
 * Inserts an image inside the text buffer.
 * @param buffer pointer to a #WPTextBuffer
 * @param pos containing the position iterator where the insert should happen
 * @param image_id the image id, used to refer to the specific image
 * @param pixbuf is a #GdkPixbuf
 */
  void wp_text_buffer_insert_image(WPTextBuffer * buffer,
                                   GtkTextIter * pos,
                                   const gchar * image_id,
                                   GdkPixbuf * pixbuf);

/**
 * Inserts an image replacement inside the text buffer. THese replacement
 * can be replaced with the image later.
 * @param buffer pointer to a #WPTextBuffer
 * @param pos containing the position iterator where the insert should happen
 * @param image_id the image id, used to refer to the specific image
 */
void wp_text_buffer_insert_image_replacement (WPTextBuffer *buffer,
                                             GtkTextIter *pos,
                                             const gchar *image_id);

/**
 * Replaces all image replacements with image_id with pixbuf.
 * @param buffer: a #WPTextBuffer
 * @param image_id the id of the image
 * @param pixbuf a #GdkPixbuf
 */
void wp_text_buffer_replace_image (WPTextBuffer *buffer,
                                  const gchar *image_id,
                                  GdkPixbuf *pixbuf);

/**
 * Removes an image inside the text buffer.
 * @param buffer pointer to a #WPTextBuffer
 * @param image_id the image id, used to refer to the specific image
 */
  void wp_text_buffer_remove_image(WPTextBuffer * buffer,
                                   const gchar * image_id);

/**
 * Reset the buffer. Clear the text, clear the undo queue, set it to unmodified.
 * @param buffer pointer to a #WPTextBuffer
 * @param rich_text if the reseted buffer will contain rich text
 */
  void wp_text_buffer_reset_buffer(WPTextBuffer * buffer, gboolean rich_text);

/**
 * Prepare the buffer for document loading. The document is loaded in chunck,
 * for better memory usage, and interoperability.
 * @param buffer pointer to a #WPTextBuffer
 * @param html if the data will be read from an html file
 */
  void wp_text_buffer_load_document_begin(WPTextBuffer * buffer,
                                          gboolean html);

/**
 * Load the next chunk into the buffer
 * @param buffer pointer to a #WPTextBuffer
 * @param data pointer to the read data
 * @param size contains the length of the data
 */
  void wp_text_buffer_load_document_write(WPTextBuffer * buffer, gchar * data,
                                          gint size);

/**
 * Finalize the buffer loading.
 * @param buffer pointer to a #WPTextBuffer
 */
  void wp_text_buffer_load_document_end(WPTextBuffer * buffer);

/**
 * Save the content of the buffer. The save is happening in chuncks,
 * so a #WPDocumentSaveCallback must be supplied to the function.
 * The save process is interrupted if the return value of the save
 * callback is not 0.
 * @param buffer pointer to a #WPTextBuffer
 * @param save contains the user save callback which will be called, when a new
 *             chunk is available and has to be written
 * @param user_data user supplied user data, which will be forwarded
 *             to the save callback
 * @return the result of the save callback
 */
  gint wp_text_buffer_save_document(WPTextBuffer * buffer,
                                    WPDocumentSaveCallback save,
                                    gpointer user_data);

/**
 * Queries the name of the font family at the given index
 * @param index is a number between 0 and #wp_get_font_count
 * @return the font family name
 */
  const gchar *wp_get_font_name(gint index);

/**
 * Tries to find the insensitive font_name in the detected font list.
 * @param font_name is the name of the font we are trying to find
 * @param def is the index of the font for the situation when the font is not found
 * @return the font name index if is found otherwise return def
 */
  gint wp_get_font_index(const gchar * font_name, gint def);

/**
 * Tries to find the correct font size index from the defined one
 * @param font_size is the size in points of the font we are trying to find
 * @param def is the index of the default font size for the situation when the 
 *             font is not found
 * @return the font size index if is found otherwise return def
 */
  gint wp_get_font_size_index(gint font_size, gint def);

/**
 * Queries the number of fonts detected on the system.
 * @return the number of fonts
 */
  gint wp_get_font_count();

/**
 * Initialize the WordPad Text Buffer library
 */
  void wp_text_buffer_library_init();
/**
 * Finalize the WordPad Text Buffer library
 */
  void wp_text_buffer_library_done();

G_END_DECLS
#endif /* _WP_TEXT_BUFFER_H */
