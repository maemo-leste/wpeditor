/**
 * @file wphtmlparser.h
 *
 * Header file for basic parsing a HTML file
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

#ifndef _WP_HTML_PARSER_H
#define _WP_HTML_PARSER_H

#include "wptextbuffer.h"

#define MAX_UTF8_LENGTH 13      // 2 * max utc4 size + 1

typedef struct _WPHTMLParser WPHTMLParser;

/**
 * Initialize the html parser library. Fill the hash table with known html tags. 
 */
void init_html_parser_library();

/**
 * Finalize the html parser library. Release the memory occupied by the hash table.
 */
void finalize_html_parser_library();

/**
 * Creates a new #WPHTMLParser for the <i>buffer</i>
 * @param buffer is a #WPTextBuffer
 * @return pointer to the newly created parser
 */
WPHTMLParser *wp_html_parser_new(WPTextBuffer * buffer);

/**
 * Destroys a html parser
 * @param parser is a #WPHTMLParser
 */
void wp_html_parser_free(WPHTMLParser * parser);

/**
 * Sets the default attributes for the parser
 * @param parser is a #WPHTMLParser
 * @param fmt is the default attributes
 */
void wp_html_parser_update_default_attributes(WPHTMLParser * parser,
                                              const WPTextBufferFormat * fmt);

/**
 * Prepare the parser for a new document parsing
 * @param parser is a #WPHTMLParser
 */
void wp_html_parser_begin(WPHTMLParser * parser);

/**
 * Writes a new chunk of data to the parser
 * @param parser is a #WPHTMLParser
 * @param buffer is a pointer to the data
 * @param size is the size of the <i>buffer</i>
 */
void wp_html_parser_write(WPHTMLParser * parser, gchar * buffer, gint size);

/**
 * Finalize parsing of the document
 * @param parser is a #WPHTMLParser
 * @return the last line justification
 */
gint wp_html_parser_end(WPHTMLParser * parser);

/**
 * Validates an invalid/partial utf8 character
 * @param buffer is a buffer holding the partial utf8 character
 * @param chars_in_buffer is the number of characters in <i>buffer</i>
 * @param source is the remaining bytes from uft8 character
 * @param max_source_len is the maximum length of <i>source</i>
 * @return number of bytes 'stolen' from source
 */
gint wp_html_parser_validate_invalid_utf8(gchar * buffer,
                                          gint chars_in_buffer,
                                          gchar * source,
                                          gint max_source_len);

#endif /* _WP_HTML_PARSER_H */
