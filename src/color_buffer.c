/**
 * @file color_buffer.c
 * Implementation for GTK color tag handling. */

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

#include "color_buffer.h"
#include <search.h>


ColorBuffer *
color_buffer_create(GtkTextBuffer * text_buffer,
                    const gchar * tag_attribute, gint size)
{
    ColorBuffer *color_buffer = NULL;

    color_buffer = g_new0(ColorBuffer, 1);

    color_buffer->text_buffer = text_buffer;
    color_buffer->tag_attribute = tag_attribute;
    color_buffer->size = size;
    color_buffer->current_size = 0;
    color_buffer->last = 0;
    color_buffer->elements = g_new0(ColorBufferElement, size);

    return color_buffer;
}

void
color_buffer_destroy(ColorBuffer * color_buffer)
{
    g_free(color_buffer->elements);
    color_buffer->text_buffer = NULL;
    /* Tag attribute string is not freed. */
    color_buffer->tag_attribute = NULL;
    color_buffer->elements = NULL;
    color_buffer->size = 0;
    color_buffer->current_size = 0;
    color_buffer->last = 0;
    g_free(color_buffer);
}


void
color_buffer_add(ColorBuffer * color_buffer, const GdkColor * color,
                 GtkTextTag * tag)
{
    gint current_size = 0;
    gint index = 0;

    /* Do not allow items with NULL tag. */

    current_size = color_buffer->current_size;
    if (current_size < color_buffer->size)
    {
        color_buffer->elements[current_size].color = *color;
        color_buffer->elements[current_size].tag = tag;
        color_buffer->last = current_size;
        color_buffer->current_size++;
    }
    else
    {
        index = color_buffer->last + 1;
        if (index >= color_buffer->size)
        {
            index = 0;
        }
        color_buffer->elements[index].color = *color;
        color_buffer->elements[index].tag = tag;
        color_buffer->last = index;
        color_buffer->current_size = color_buffer->size;
    }
}


int
color_buffer_compare_elements(const void *elem1, const void *elem2)
{
    const ColorBufferElement *el1 = NULL;
    const ColorBufferElement *el2 = NULL;

    el1 = (const ColorBufferElement *) elem1;
    el2 = (const ColorBufferElement *) elem2;
    if (el1->color.red < el2->color.red)
    {
        return -1;
    }
    else if (el1->color.red > el2->color.red)
    {
        return 1;
    }
    else if (el1->color.green < el2->color.green)
    {
        return -1;
    }
    else if (el1->color.green > el2->color.green)
    {
        return 1;
    }
    else if (el1->color.blue < el2->color.blue)
    {
        return -1;
    }
    else if (el1->color.blue > el2->color.blue)
    {
        return 1;
    }
    else if (el1->color.pixel < el2->color.pixel)
    {
        return -1;
    }
    else if (el1->color.pixel > el2->color.pixel)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}


ColorBufferElement *
color_buffer_search(ColorBuffer * color_buffer, const GdkColor * color)
{
    ColorBufferElement *element = NULL;
    ColorBufferElement target;
    size_t current_size = 0;

    target.color = *color;
    target.tag = NULL;
    current_size = color_buffer->current_size;
    element = lfind(&target,
                    color_buffer->elements,
                    &current_size,
                    sizeof(ColorBufferElement),
                    color_buffer_compare_elements);

    return element;
}


GtkTextTag *
color_buffer_query_tag(ColorBuffer * color_buffer, const GdkColor * color)
{
    ColorBufferElement *element = NULL;

    element = color_buffer_search(color_buffer, color);
    if (element != NULL)
    {
        return element->tag;
    }
    else
    {
        return NULL;
    }
}


GtkTextTag *
color_buffer_create_tag(ColorBuffer * color_buffer,
                        const GdkColor * color, gint priority)
{
    GtkTextTag *tag = NULL;
    GtkTextTagTable *tbl =
        gtk_text_buffer_get_tag_table(color_buffer->text_buffer);

    /* Does the tag have a copy of the color? */
    gchar *tmp =
        g_strdup_printf("wp-text-color-%02x%02x%02x", color->red / 256,
                        color->green / 256, color->blue / 256);
    tag = gtk_text_tag_table_lookup(tbl, tmp);
    if (!tag)
        tag = gtk_text_buffer_create_tag(color_buffer->text_buffer, tmp,
                                         color_buffer->tag_attribute, color,
                                         NULL);
    g_free(tmp);
    gtk_text_tag_set_priority(tag, priority);
    return tag;
}


GtkTextTag *
color_buffer_get_tag(ColorBuffer * color_buffer,
                     const GdkColor * color, gint priority)
{
    ColorBufferElement *element = NULL;
    GtkTextTag *tag = NULL;


    element = color_buffer_search(color_buffer, color);
    if (element != NULL)
    {
        return element->tag;
    }
    else
    {
        tag = color_buffer_create_tag(color_buffer, color, priority);
        color_buffer_add(color_buffer, color, tag);
        return tag;
    }
}
