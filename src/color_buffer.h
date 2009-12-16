/**
 * @file color_buffer.h
 * Header file for GTK color tag handling. */

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
 */

#ifndef COLOR_BUFFER_H
#define COLOR_BUFFER_H


 /*GTK*/
#include <gtk/gtk.h>
#include <gdk/gdk.h>
    typedef struct {
    GdkColor color;
    GtkTextTag *tag;
} ColorBufferElement;

typedef struct {
    GtkTextBuffer *text_buffer;
    const gchar *tag_attribute;
    gint size;
    gint current_size;
    gint last;
    ColorBufferElement *elements;
} ColorBuffer;


/**
   Create a ColorBuffer object.

   @param text_buffer GtkTextBuffer object
   @param tag_attribute GTK attribute name for tags
   @param size size of the color buffer
   @return created ColorBuffer object
*/
ColorBuffer *color_buffer_create(GtkTextBuffer * text_buffer,
                                 const gchar * tag_attribute, gint size);


/**
   Destroy a ColorBuffer object

   @param color_buffer ColorBuffer object
*/
void color_buffer_destroy(ColorBuffer * color_buffer);


/**
   Add a pair consisting a color and GTK tag to a color buffer

   @param color_buffer ColorBuffer object
   @param color GDK color
   @param tag GTK tag for the color
 */
void color_buffer_add(ColorBuffer * color_buffer, const GdkColor * color,
                      GtkTextTag * tag);


/**
   Compare two ColorBufferElement objects.
   Used internally for searching.

   @param elem1 element 1
   @param elem2 element 2
   @return
   -1 if element 1 <  element 2
   0  if element 1 == element 2
   1  if element 1 >  element 2
*/
int color_buffer_compare_elements(const void *elem1, const void *elem2);


/**
   Search an element from a color buffer by color

   @param color_buffer ColorBuffer object
   @param color GDK color
   @return pointer to the element in the color buffer
   NULL if no element for the given color is not found in the color buffer
*/
ColorBufferElement *color_buffer_search(ColorBuffer * color_buffer,
                                        const GdkColor * color);


/**
   Query a tag from a color buffer

   @param color_buffer ColorBuffer object
   @param color GDK color
   @return GTK tag for the color
   NULL if the color is not found in the color buffer
*/
GtkTextTag *color_buffer_query_tag(ColorBuffer * color_buffer,
                                   const GdkColor * color);


/**
   Create a color tag.
   The created GTK tag has attribute given by color_buffer->tag_attribute
   set to the color.

   @param color_buffer ColorBuffer object
   @param color GDK color
   @param priority is the priority of the created tag in the tag table
   @return GTK tag for the color
*/
GtkTextTag *color_buffer_create_tag(ColorBuffer * color_buffer,
                                    const GdkColor * color, gint priority);


/**
   Get a color tag from a color buffer.
   If a tag for the given color is found in the buffer it is returned.
   Otherwise a new color tag is created, stored in the color buffer,
   and returned.

   @param color_buffer ColorBuffer object
   @param color GDK color
   @param priority is the priority of the created tag in the tag table
   @return GTK tag for the color
 */
GtkTextTag *color_buffer_get_tag(ColorBuffer * color_buffer,
                                 const GdkColor * color, gint priority);


#endif
