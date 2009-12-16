/**
 * @file wptextbuffer-private.h
 *
 * Private header file for WordPad Text Buffer used only between
 * WPTextView, WPUndo and WPHTMLParser
 */

/* 
 * Osso Notes
 * Copyright (c) 2005-06 Nokia. All rights reserved.
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

#ifndef _WP_TEXT_BUFFER_PRIVATE_H
#define _WP_TEXT_BUFFER_PRIVATE_H

#include <gtk/gtktextiter.h>
#include <gtk/gtktexttag.h>

G_BEGIN_DECLS
/**
 * Queries if the position at <i>iter</i> contains opened <i>tag</i>
 * @param iter a position in the buffer
 * @param tag a #GtkTextTag, usually the bullet tag
 * @return <b>TRUE</b> if the <i>tag</i> is opened at <i>iter</i>
 */
    gboolean _wp_text_iter_is_bullet(GtkTextIter * iter, GtkTextTag * tag);

/**
 * Skip the <i>tag</i> at the <i>iter</i> position in <i>forward</i> direction
 * @param iter a position in the buffer
 * @param tag a #GtkTextTag, usually the bullet tag
 * @param forward set to <b>TRUE</b> if the skip is happening forward
 * @return <b>TRUE</b> if there was a <i>tag</i> at the <i>iter</i> position
 */
gboolean _wp_text_iter_skip_bullet(GtkTextIter * iter, GtkTextTag * tag,
                                   gboolean forward);
/**
 * Queries if a the line specified by <i>iter</i> contains the <i>tag</i> at the
 * begining. Modify the position to <i>iter</i> to the begin of the line
 * @param iter a position in the buffer
 * @param tag a #GtkTextTag, usually the bullet tag
 * @return <b>TRUE</b> if the <i>tag</i> has been found
 */
gboolean _wp_text_iter_has_bullet(GtkTextIter * iter, GtkTextTag * tag);

/**
 * Puts a <i>tag</i> at the begining of the line specified by <i>iter</i>
 * @param iter a position in the buffer
 * @param tag a #GtkTextTag, usually the bullet tag
 * @return <b>TRUE</b> if there was no <i>tag</i> at the begining of the line
 */
gboolean _wp_text_iter_put_bullet_line(GtkTextIter * iter, GtkTextTag * tag);

/**
 * Removes a <i>tag</i> from the begining of the line specified by <i>iter</i>
 * @param iter a position in the buffer
 * @param tag a #GtkTextTag, usually the bullet tag
 */
void _wp_text_iter_remove_bullet_line(GtkTextIter * iter, GtkTextTag * tag);
/**
 * Queries the #GtkTextTag used for delimiting a bullet
 * @param buffer pointer to a #WPTextBuffer
 * @return the bullet tag
 */
GtkTextTag *_wp_text_buffer_get_bullet_tag(WPTextBuffer * buffer);

/**
 * Modify the justification of the text delimited by <i>start</i> and
 * <i>end</i> to be the same at the begining and at the end. Usually
 * called after two different line has been concatenated.
 * @param buffer pointer to a #WPTextBuffer
 * @param start a position in the buffer or <b>NULL</b>
 * @param end a position in the buffer or <b>NULL</b>
 * @param def_tag holds the default justification #GtkTextTag or <b>NULL</b>
 * @param align_to_right not used
 */
void _wp_text_buffer_adjust_justification(WPTextBuffer * buffer,
                                          GtkTextIter * start,
                                          GtkTextIter * end,
                                          GtkTextTag * def_tag,
                                          gboolean align_to_right);

/**
 * Set the remember_tag flag to true. It is used, to remember the deleted tags
 * @param buffer pointer to a #WPTextBuffer
 */
void wp_text_buffer_remember_tag(WPTextBuffer * buffer, gboolean enable);

/**
 * Print the tags to stdout toggled/untoggled at <i>giter</i>
 * Used only for debugging.
 * @param start a position in the buffer
 * @param what 0 is print all the tags, 1 for toggled on tags only, 
 *             2 for toggled off tags only
 */
void debug_print_tags(GtkTextIter * giter, gint what);

G_END_DECLS
#endif /* _WP_TEXT_BUFFER_PRIVATE_H */
