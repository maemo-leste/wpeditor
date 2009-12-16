/**
 * @file wphtmlparser.c
 *
 * Implementation file for basic parsing a HTML file
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

#include <string.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <stdlib.h>
#include <ctype.h>

#include "wphtmlparser.h"

#define MAX_TAG_LENGTH 100
#define MAX_TAG_ATTR_LENGTH 100
#define MAX_TAG_VALUE_LENGTH 200
#define MAX_TEXT_LENGTH 4096
#define MAX_SPECIAL_CHAR 10

const gchar *tag_script = "</script>";
#define SCRIPT_LEN 9

/** HTML state from the parsing state machine */
typedef enum {
    ST_TEXT,
    ST_TAG,
    ST_TAGNAME,
    ST_TAGNAMETXT,
    ST_TAGATTRNAME,
    ST_TAGATTRNAMETXT,
    ST_TAGATTRSEP,
    ST_TAGVALUE,
    ST_TAGVALUETXT,
    ST_TAGCLOSE,
    ST_COMMENT,
    ST_SCRIPT
} HTMLState;

/** List type */
typedef enum {
    LT_NONE,
    LT_BULLET,
    LT_NUM,
    LT_LC_ALPHA,
    LT_UC_ALPHA
} HTMLListType;

/** Font type */
typedef struct {
    gint font;
    gint font_size;
    GdkColor color;
} HTMLFontType;

/** Private structure */
struct _WPHTMLParser {
    /** The current state, the parser is in */
    HTMLState state;

    /** Will hold last decoded text */
    gchar last_text[MAX_TEXT_LENGTH + 2];
    /** Pointer to the last character in the decoded text buffer */
    gchar *last_text_pos;
    /** Pointer to the last special character '&' in the decoded buffer */
    gchar *last_special_char;
    /** Last character was a space */
    gint space:1;
    /** Is begin of line */
    gint bol:1;
    /** Should text be skipped */
    gint skip_text;

    /** Hold the last quote mark */
    gchar last_quote_mark;

    /** Buffer to hold the last detected tag */
    gchar last_tag[MAX_TAG_LENGTH + 1];
    /** Pointer to the last character in the last_tag buffer */
    gchar *last_tag_pos;
    /** Set if the tag is a close tag */
    gint is_close_tag:1;

    /** Buffer to hold the last detected tag attribute */
    gchar last_tag_attr[MAX_TAG_ATTR_LENGTH + 1];
    /** Pointer to the last character in the last_tag_attr buffer */
    gchar *last_tag_attr_pos;
    /** Set if is the first attribute */
    gint is_first_attr:1;

    /** Buffer to hold the last detected tag attribute value */
    gchar last_tag_value[MAX_TAG_VALUE_LENGTH + 1];
    /** Pointer to the last character in the last_tag_value buffer */
    gchar *last_tag_value_pos;

    /** Buffer to hold the partial utf8 character */
    gchar last_char[MAX_UTF8_LENGTH];
    /** Number of bytes the last broken uft8 character has */
    gint last_char_bytes;

    /** Position in the script detection */
    guchar script_pos;
    /** Set if currently we are in a script */
    gint is_script:1;

    // Bullets and numbering
    /** Number of the last used list entry */
    gint list_number;
    /** Type of the list */
    HTMLListType list_type;

    /** Pointer to a #WPTextBuffer */
    WPTextBuffer *buffer;

    /** List of font tags, need to know what will close a </font> */
    GSList *font_tags;

    /** Current format and default format of the text */
    WPTextBufferFormat fmt, default_fmt;
    /** Last line justification */
    gint last_line_justification;
};

/**
 * Release the memory occupied by font_tags
 * @param parser is a #WPHTMLParser
 */
static void html_free_font_tags(WPHTMLParser * parser);

typedef void (*ProcessTag) (WPHTMLParser * parser);

/**
 * Process the HTML tag bold
 * @param parser is a #WPHTMLParser
 */
static void process_tag_bold(WPHTMLParser * parser);

/**
 * Process the HTML tag italic
 * @param parser is a #WPHTMLParser
 */
static void process_tag_italic(WPHTMLParser * parser);

/**
 * Process the HTML tag underline
 * @param parser is a #WPHTMLParser
 */
static void process_tag_underline(WPHTMLParser * parser);

/**
 * Process the HTML tag strikethrough
 * @param parser is a #WPHTMLParser
 */
static void process_tag_strike(WPHTMLParser * parser);

/**
 * Process the HTML tag subscript
 * @param parser is a #WPHTMLParser
 */
static void process_tag_sub(WPHTMLParser * parser);

/**
 * Process the HTML tag superscript
 * @param parser is a #WPHTMLParser
 */
static void process_tag_sup(WPHTMLParser * parser);

/**
 * Process the HTML tag div
 * @param parser is a #WPHTMLParser
 */
static void process_tag_div(WPHTMLParser * parser);

/**
 * Process the HTML tag unordered list
 * @param parser is a #WPHTMLParser
 */
static void process_tag_ul(WPHTMLParser * parser);

/**
 * Process the HTML tag ordered list
 * @param parser is a #WPHTMLParser
 */
static void process_tag_ol(WPHTMLParser * parser);

/**
 * Process the HTML tag list item
 * @param parser is a #WPHTMLParser
 */
static void process_tag_li(WPHTMLParser * parser);

/**
 * Process the HTML tag font
 * @param parser is a #WPHTMLParser
 */
static void process_tag_font(WPHTMLParser * parser);

/**
 * Process the HTML tag img
 * @param parser is a #WPHTMLParser
 */
static void process_tag_img(WPHTMLParser *parser);

/**
 * This tag will cause the text to be skippen
 * @param parser is a #WPHTMLParser
 */
static void process_skip_tag(WPHTMLParser * parser);

/**
 * Process the HTML tag br
 * @param parser is a #WPHTMLParser
 */
static void process_tag_br(WPHTMLParser * parser);

/**
 * Process the HTML tag paragraph
 * @param parser is a #WPHTMLParser
 */
static void process_tag_p(WPHTMLParser * parser);

/** Hash table containing the HTML tags */
static GHashTable *tag_hash = NULL;

/**
 * Add a new tag to the hash table <i>tag_hash</i>
 * @param tag is the tag name
 * @param func is the pointer to the processing function
 */
static inline void
add_tag(gchar * tag, ProcessTag func)
{
    g_hash_table_insert(tag_hash, tag, func);
}

void
init_html_parser_library()
{
    tag_hash = g_hash_table_new(g_str_hash, g_str_equal);

    add_tag("b", process_tag_bold);
    add_tag("strong", process_tag_bold);
    add_tag("i", process_tag_italic);
    add_tag("em", process_tag_italic);
    add_tag("cite", process_tag_italic);
    add_tag("u", process_tag_underline);
    add_tag("ins", process_tag_underline);
    add_tag("strike", process_tag_strike);
    add_tag("del", process_tag_strike);
    add_tag("s", process_tag_strike);
    add_tag("sub", process_tag_sub);
    add_tag("sup", process_tag_sup);
    add_tag("div", process_tag_div);
    add_tag("ul", process_tag_ul);
    add_tag("ol", process_tag_ol);
    add_tag("li", process_tag_li);
    add_tag("font", process_tag_font);
    add_tag("head", process_skip_tag);
    add_tag("br", process_tag_br);
    add_tag("p", process_tag_p);
    add_tag("img", process_tag_img);
}

void
finalize_html_parser_library()
{
    if (tag_hash)
    {
        g_hash_table_destroy(tag_hash);
        tag_hash = NULL;
    }
}

WPHTMLParser *
wp_html_parser_new(WPTextBuffer * buffer)
{
    WPHTMLParser *parser;

    if (!tag_hash)
        init_html_parser_library();

    parser = g_new(WPHTMLParser, 1);

    if (parser)
    {
        parser->buffer = buffer;
        wp_html_parser_begin(parser);
    }

    return parser;
}

void
wp_html_parser_free(WPHTMLParser * parser)
{
    if (parser)
    {
        html_free_font_tags(parser);
        g_free(parser);
    }
}

void
wp_html_parser_update_default_attributes(WPHTMLParser * parser,
                                         const WPTextBufferFormat * fmt)
{
    parser->default_fmt = *fmt;
    parser->default_fmt.cs.color = TRUE;
    parser->fmt = parser->default_fmt;
}

/**
 * Writes the text to the WPTextBuffer
 * @param parser is a #WPHTMLParser
 */
static void
html_write_text(WPHTMLParser * parser)
{
    if (parser->last_special_char)
    {
        if (parser->last_text_pos - parser->last_special_char >
            MAX_SPECIAL_CHAR)
        {
            *parser->last_text_pos = 0;
            parser->last_special_char = NULL;
        }
        else
            *parser->last_special_char = 0;
    }
    else
        *parser->last_text_pos = 0;

    if (*parser->last_text)
    {
        GtkTextIter iter;

        gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(parser->buffer), &iter);
        wp_text_buffer_insert_with_attribute(parser->buffer, &iter,
                                             parser->last_text, -1,
                                             &parser->fmt, TRUE);

        parser->last_line_justification = parser->fmt.justification;
        // printf("Text: '%s'\n", parser->last_text);
    }

    if (parser->last_special_char)
    {
        gint len = parser->last_text_pos - parser->last_special_char;

        *parser->last_special_char = '&';
        memcpy(parser->last_text, parser->last_special_char, len);

        parser->last_special_char = parser->last_text;
        parser->last_text_pos = parser->last_text + len;
    }
    else
    {
        parser->last_text_pos = parser->last_text;
        *parser->last_text = 0;
        // parser->space = !parser->bol;
    }
}

/**
 * Recognise and replace special HTML tags (ex. &amp;)
 * @param parser is a #WPHTMLParser
 */
static void
html_replace_special_char(WPHTMLParser * parser)
{
    /* This function only recognise a small subset of special chars For more
     * see: http://www.pemberley.com/janeinfo/latin1.html */
    gchar *pos = parser->last_special_char;

    *parser->last_text_pos = 0;
    parser->last_special_char = NULL;

    if (strcmp(pos + 1, "nbsp") == 0)
        pos += g_unichar_to_utf8(0xa0, pos) - 1;
    else if (strcmp(pos + 1, "gt") == 0)
        *pos = '>';
    else if (strcmp(pos + 1, "lt") == 0)
        *pos = '<';
    else if (strcmp(pos + 1, "amp") == 0)
        *pos = '&';
    else if (strcmp(pos + 1, "quot") == 0)
        *pos = '"';
    else if (strcmp(pos + 1, "space") == 0)
        *pos = ' ';
    else if (strcmp(pos + 1, "euro") == 0)
    {
        pos += g_unichar_to_utf8(0x20ac, pos) - 1;
    }
    else if (*(pos + 1) == '#')
    {
        gunichar ch;
        gchar *p = pos + 2;
        gint base = 10;

        if (*p == 'x')
        {
            p++;
            base = 16;
        }
        ch = (gunichar) strtol(p, NULL, base);
        if (g_unichar_isdefined(ch))
            pos += g_unichar_to_utf8(ch, pos) - 1;
        else
        {
            *parser->last_text_pos = ';';
            return;
        }
    }
    else
    {
        *parser->last_text_pos = ';';
        return;
    }

    parser->last_text_pos = pos;
}

/**
 * Parse the retrieved HTML tag. It will look up in the hash table and call
 * the specific callback if found.
 * @param parser is a #WPHTMLParser
 */
static void
html_parse_tag(WPHTMLParser * parser)
{
    if (parser->last_tag_pos)
    {
        *parser->last_tag_pos = 0;
        // printf("New tag: %s,%d\n", parser->last_tag,
        // parser->is_close_tag);

        if (!parser->last_tag_attr_pos)
            parser->last_tag_attr_pos = parser->last_tag_attr;
        *parser->last_tag_attr_pos = 0;

        if (!parser->last_tag_value_pos)
            parser->last_tag_value_pos = parser->last_tag_value;
        *parser->last_tag_value_pos = 0;

        // printf("Value: %s='%s'\n", parser->last_tag_attr,
        // parser->last_tag_value);

        ProcessTag fnc = g_hash_table_lookup(tag_hash, parser->last_tag);
        if (fnc)
            fnc(parser);
    }
}

/**
 * Queries if at position <i>pos</i> is a close tag.
 * @param parser is a #WPHTMLParser
 * @param pos a position in a text
 * @return <b>TRUE</b> if it is a close tag.
 */
static gboolean
html_is_tag_close(WPHTMLParser * parser, gchar * pos)
{
    if (*pos == '/' && parser->state != ST_TAGCLOSE)
    {
        parser->state = ST_TAGCLOSE;
        return TRUE;
    }
    else if (*pos == '>')
    {
        parser->state = ST_TEXT;

        if (parser->last_tag_pos)
            html_parse_tag(parser);

        return TRUE;
    }

    return FALSE;
}

/**
 * Write a character to the <i>last_text</i> buffer from <i>*pos</i>.
 * Also check if the character is a partial utf8 character.
 * @param parser is a #WPHTMLParser
 * @param pos a position in the buffer
 * @param end a the end position of the buffer
 */
static gint
html_write_char(WPHTMLParser * parser, gchar * pos, gchar * end)
{
    if (parser->skip_text)
        return g_utf8_skip[*(guchar *) pos];
    if (end && isspace(*pos))
    {
        parser->space = !parser->bol;
        return 1;
    }
    else
    {
        guchar len;

        if (parser->space)
        {
            *parser->last_text_pos = ' ';
            parser->last_text_pos++;
            parser->space = FALSE;
        }

        parser->bol = FALSE;

        len = g_utf8_skip[*(guchar *) pos];
        if (parser->last_text_pos - parser->last_text + len > MAX_TEXT_LENGTH)
            html_write_text(parser);

        // TODO: Find a faster validating method
        if (pos + len <= end || !end)
        {
            if (g_utf8_validate(pos, len, NULL))
            {
                switch (len)
                {
                    case 1:
                        if (*pos == ';' && parser->last_special_char)
                            html_replace_special_char(parser);
                        else
                        {
                            if (*pos == '&')
                                parser->last_special_char =
                                    parser->last_text_pos;
                            *parser->last_text_pos = *pos;
                        }
                        break;
                    case 2:
                        *(gushort *) parser->last_text_pos = *(gushort *) pos;
                        break;
                    default:
                        memcpy(parser->last_text_pos, pos, len);
                }
                parser->last_text_pos += len;
            }
            else
                len = 1;
            pos += len;
        }
        else
        {
            // broken UTF8 sequence at the end of the buffer
            parser->last_char_bytes = end - pos;
            memcpy(parser->last_char, pos, parser->last_char_bytes);
        }

        return len;
    }
}

/**
 * Inserts a new line to the buffer if needed or forced
 * @param parser is a #WPHTMLParser
 * @param force set to force a newline
 */
static void
html_insert_newline(WPHTMLParser * parser, gboolean force)
{
    if (force || (!parser->bol && !parser->is_close_tag))
    {
        parser->space = FALSE;
        html_write_char(parser, "\n", NULL);
        html_write_text(parser);
        parser->bol = TRUE;
    }
}

/**
 * Insert a image replacement
 */
static void
html_insert_image(WPHTMLParser *parser, const gchar *image_id)
{
    GtkTextIter iter;
    html_write_text (parser);
    gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (parser->buffer), &iter);
    wp_text_buffer_insert_image_replacement (parser->buffer, &iter, image_id);
}

gint
wp_html_parser_validate_invalid_utf8(gchar * buffer, gint chars_in_buffer,
                                     gchar * source, gint max_source_len)
{
    gint len, clen;
    gchar *p = buffer;
    gchar *invalid_offset = NULL;

    if (source)
    {
        clen = MIN(6, max_source_len);
        memcpy(buffer + chars_in_buffer, source, clen);
        len = chars_in_buffer + clen;
    }
    else
    {
        len = chars_in_buffer;
        clen = 0;
    }
    buffer[len] = 0;

    while (!g_utf8_validate(p, buffer + len - p, (const gchar **) &invalid_offset))
    {
        g_assert(buffer + len >= invalid_offset);
        memmove(invalid_offset, invalid_offset + 1,
                len - (invalid_offset + 1 - buffer));
        len--;
        if (invalid_offset < buffer + chars_in_buffer)
            chars_in_buffer--;
        else
        {
            *invalid_offset = 0;
            return (invalid_offset - buffer) - chars_in_buffer;
        }
        p = invalid_offset;
    }

    return clen;
}

void
wp_html_parser_write(WPHTMLParser * parser, gchar * buffer, gint size)
{
    gchar *pos, *end;
    gchar c;

    /* This parser works correctly only for UTF8. To implement for other
     * charset, should be not so hard. Just the buffer need to be converted
     * to utf8 from the detected character set. */

    pos = buffer;
    end = pos + size;

    if (parser->last_char_bytes)
    {
        pos += wp_html_parser_validate_invalid_utf8(parser->last_char,
                                                    parser->last_char_bytes,
                                                    pos, size);
        parser->last_char_bytes = 0;
        if (*parser->last_char)
            wp_html_parser_write(parser, parser->last_char,
                                 strlen(parser->last_char));
    }

    while (pos < end)
    {
        switch (parser->state)
        {
            case ST_TEXT:
                if (*pos == '<')
                {
                    pos++;
                    html_write_text(parser);
                    parser->state = ST_TAG;
                    parser->is_close_tag = FALSE;
                }
                else
                    pos += html_write_char(parser, pos, end);
                break;
            case ST_TAG:
                if (*pos == '!')
                {
                    pos++;
                    parser->state = ST_COMMENT;
                    parser->last_quote_mark = 0;
                }
                else
                {
                    if (*pos == '/')
                    {
                        pos++;
                        parser->is_close_tag = TRUE;
                    }
                    parser->state = ST_TAGNAME;
                }
                break;
            case ST_TAGNAME:
                if (isspace(*pos))
                    pos++;
                else
                {
                    parser->state = ST_TAGNAMETXT;
                    parser->last_tag_pos = NULL;
                    parser->last_tag_attr_pos = NULL;
                    parser->is_first_attr = TRUE;
                }
                break;
            case ST_TAGNAMETXT:
                if (html_is_tag_close(parser, pos))
                    pos++;
                else if (isspace(*pos))
                {
                    pos++;
                    if (parser->last_tag_pos)
                        parser->state = ST_TAGATTRNAME;
                }
                else if (!parser->is_close_tag && tolower(*pos) == 's')
                {
                    // Handle the case when special tag script is found
                    // Skip the entire text between these, because script
                    // can_redo
                    // contain very ugly tags, and fool the parser!
                    parser->state = ST_SCRIPT;
                    parser->last_quote_mark = 0;
                    parser->script_pos = 3;
                    parser->is_script = FALSE;
                    pos++;
                }
                else
                {
                    if (!parser->last_tag_pos)
                        parser->last_tag_pos = parser->last_tag;

                    if (parser->last_tag_pos - parser->last_tag <
                        MAX_TAG_LENGTH)
                    {
                        *parser->last_tag_pos = tolower(*pos);

                      //  *parser->last_tag_pos++;
                        parser->last_tag_pos++;
                    }
                    pos++;
                }
                break;
            case ST_TAGATTRNAME:
                if (isspace(*pos))
                    pos++;
                else
                {
                    parser->state = ST_TAGATTRNAMETXT;
                    parser->last_tag_attr_pos = NULL;
                    parser->last_tag_value_pos = NULL;
                    parser->last_quote_mark = 0;
                }
                break;
            case ST_TAGATTRNAMETXT:
                if (html_is_tag_close(parser, pos))
                    pos++;
                else if (isspace(*pos))
                {
                    pos++;
                    parser->state = ST_TAGATTRSEP;
                }
                else if (*pos == '=')
                {
                    pos++;
                    parser->state = ST_TAGVALUE;
                }
                else
                {
                    if (!parser->last_tag_attr_pos)
                        parser->last_tag_attr_pos = parser->last_tag_attr;
                    if (parser->last_tag_attr_pos - parser->last_tag_attr <
                        MAX_TAG_ATTR_LENGTH)
                    {
                        *parser->last_tag_attr_pos = tolower(*pos);
                        parser->last_tag_attr_pos++;
                    }
                    pos++;
                }
                break;
            case ST_TAGATTRSEP:
                if (html_is_tag_close(parser, pos))
                    pos++;
                else if (isspace(*pos))
                    pos++;
                else if (*pos == '=')
                {
                    pos++;
                    parser->state = ST_TAGVALUE;
                }
                else
                {
                    g_warning("Invalid html syntax (tagattrsep)");
                    parser->state = ST_TEXT;
                }
                break;
            case ST_TAGVALUE:
                if (html_is_tag_close(parser, pos))
                    pos++;
                else if (isspace(*pos))
                    pos++;
                else if (*pos == '"' || *pos == '\'')
                {
                    parser->state = ST_TAGVALUETXT;
                    parser->last_quote_mark = *pos;
                    pos++;
                }
                else
                {
                    parser->state = ST_TAGVALUETXT;
                    parser->last_quote_mark = 0;
                }
                break;
            case ST_TAGVALUETXT:
                if (*pos == parser->last_quote_mark ||
                    (!parser->last_quote_mark && isspace(*pos)))
                {
                    pos++;
                    parser->last_quote_mark = 0;
                    html_parse_tag(parser);
                    parser->is_first_attr = FALSE;
                    parser->state = ST_TAGATTRNAME;
                }
                else if (!parser->last_quote_mark
                         && html_is_tag_close(parser, pos))
                    pos++;
                else
                {
                    if (!parser->last_tag_value_pos)
                        parser->last_tag_value_pos = parser->last_tag_value;
                    if (parser->last_tag_value_pos - parser->last_tag_value <
                        MAX_TAG_VALUE_LENGTH)
                    {
                        *parser->last_tag_value_pos = tolower(*pos);
                        parser->last_tag_value_pos++;
                    }
                    pos++;
                }
                break;
            case ST_TAGCLOSE:
                if (isspace(*pos) || html_is_tag_close(parser, pos))
                    pos++;
                else
                {
                    g_warning("Invalid html syntax (tagclose)");
                    pos++;
                    parser->state = ST_TEXT;
                    parser->is_close_tag = TRUE;
                }
                break;
            case ST_COMMENT:
                if (*pos == '"' || *pos == '\'')
                {
                    if (!parser->last_quote_mark)
                        parser->last_quote_mark = *pos;
                    else if (parser->last_quote_mark == *pos)
                        parser->last_quote_mark = 0;
                    pos++;
                }
                else
                {
                    if (!parser->last_quote_mark)
                        html_is_tag_close(parser, pos);
                    pos++;
                }
                break;
            case ST_SCRIPT:
                c = tolower(*pos);
                if (isspace(c) && parser->script_pos == 8)
                {
                    if (!parser->is_script)
                    {
                        parser->is_script = TRUE;
                        parser->script_pos = 0;
                    }
                    pos++;
                }
                else if (c == tag_script[parser->script_pos] &&
                         !parser->last_quote_mark)
                {
                    parser->script_pos++;
                    pos++;
                    if (parser->script_pos >= SCRIPT_LEN)
                    {
                        if (parser->is_script)
                        {
                            parser->is_script = FALSE;
                            parser->state = ST_TEXT;
                        }
                        else
                        {
                            parser->is_script = TRUE;
                            parser->script_pos = 0;
                        }
                    }
                }
                else if (!parser->is_script)
                {
                    memcpy(parser->last_tag, tag_script + 2,
                           parser->script_pos - 2);
                    parser->last_tag_pos =
                        parser->last_tag + parser->script_pos - 2;
                    parser->state = ST_TAGNAMETXT;
                }
                else if (c == '"' || c == '\'')
                {
                    if (!parser->last_quote_mark)
                        parser->last_quote_mark = c;
                    else if (parser->last_quote_mark == c)
                        parser->last_quote_mark = 0;
                    pos++;
                }
                else
                    pos++;
            default:
                g_assert("Should not happen!!");
        }
    }
}

void
wp_html_parser_begin(WPHTMLParser * parser)
{
    WPTextBuffer *buffer = parser->buffer;
    WPTextBufferFormat fmt = parser->default_fmt;
    // WPTextBufferFormat *fmt;

    memset(parser, 0, sizeof(WPHTMLParser));

    parser->buffer = buffer;
    parser->state = ST_TEXT;
    parser->last_text_pos = parser->last_text;
    parser->bol = TRUE;
    /* 
     * g_object_get(buffer, "def_attr", &fmt, NULL); if (fmt) { parser->fmt = 
     * *fmt; parser->fmt.cs.color = TRUE; parser->default_fmt = parser->fmt;
     * } */
    parser->fmt = parser->default_fmt = fmt;

    parser->last_line_justification = GTK_JUSTIFY_LEFT;
}

gint
wp_html_parser_end(WPHTMLParser * parser)
{
    if (parser->last_char_bytes)
        wp_html_parser_write(parser, NULL, 0);

    html_write_text(parser);

    return parser->last_line_justification;
}

static void
html_free_font_tags(WPHTMLParser * parser)
{
    GSList *tmp = parser->font_tags;
    while (tmp)
    {
        g_free(tmp->data);
        tmp = tmp->next;
    }
    g_slist_free(parser->font_tags);
    parser->font_tags = NULL;
}

static void
process_tag_bold(WPHTMLParser * parser)
{
    parser->fmt.bold = !parser->is_close_tag;
    parser->fmt.cs.bold = !parser->is_close_tag;
}

static void
process_tag_italic(WPHTMLParser * parser)
{
    parser->fmt.italic = !parser->is_close_tag;
    parser->fmt.cs.italic = !parser->is_close_tag;
}

static void
process_tag_underline(WPHTMLParser * parser)
{
    parser->fmt.underline = !parser->is_close_tag;
    parser->fmt.cs.underline = !parser->is_close_tag;
}

static void
process_tag_strike(WPHTMLParser * parser)
{
    parser->fmt.strikethrough = !parser->is_close_tag;
    parser->fmt.cs.strikethrough = !parser->is_close_tag;
}

static void
process_tag_sub(WPHTMLParser * parser)
{
    parser->fmt.text_position = parser->is_close_tag ?
        TEXT_POSITION_NORMAL : TEXT_POSITION_SUBSCRIPT;
    // parser->fmt.cs.text_position = TRUE;
}

static void
process_tag_sup(WPHTMLParser * parser)
{
    parser->fmt.text_position = parser->is_close_tag ?
        TEXT_POSITION_NORMAL : TEXT_POSITION_SUPERSCRIPT;
    // parser->fmt.cs.text_position = TRUE;
}

/**
 * Process an align attribute
 * @param parser is a #WPHTMLParser
 */
static void
process_align(WPHTMLParser * parser)
{
    if (!parser->is_close_tag && strcmp(parser->last_tag_attr, "align") == 0)
    {
        gchar *value = parser->last_tag_value;
        if (strcmp(value, "center") == 0)
            parser->fmt.justification = GTK_JUSTIFY_CENTER;
        else if (strcmp(value, "right") == 0)
            parser->fmt.justification = GTK_JUSTIFY_RIGHT;
        else
            parser->fmt.justification = GTK_JUSTIFY_LEFT;
    }
    else
        parser->fmt.justification = GTK_JUSTIFY_LEFT;
}

static void
process_tag_div(WPHTMLParser * parser)
{
    html_insert_newline(parser, FALSE);

    process_align(parser);
    // parser->fmt.cs.justification = TRUE;
}

static void
process_tag_ul(WPHTMLParser * parser)
{
    html_insert_newline(parser, FALSE);

    parser->list_type = parser->is_close_tag ? LT_NONE : LT_BULLET;
    // need this, if the html syntax is not correct
    parser->fmt.bullet = !parser->is_close_tag;
}

static void
process_tag_ol(WPHTMLParser * parser)
{
    html_insert_newline(parser, FALSE);

    /* Numbering need to be implemented in the editor ! */
    if (parser->is_first_attr && !parser->is_close_tag)
    {
        parser->list_number = 1;
        parser->list_type = LT_NUM;
    }
    else if (parser->is_close_tag)
        parser->list_type = LT_NONE;

    if (!parser->is_close_tag)
    {
        if (strcmp(parser->last_tag_attr, "start") == 0)
        {
            parser->list_type = atoi(parser->last_tag_value);
            if (parser->list_type)
                --parser->list_type;
        }
        else if (strcmp(parser->last_tag_attr, "type") == 0)
        {
            switch (*parser->last_tag_value)
            {
                case '1':
                    parser->list_type = LT_NUM;
                    break;
                case 'a':
                    parser->list_type = LT_LC_ALPHA;
                    break;
                case 'A':
                    parser->list_type = LT_UC_ALPHA;
                    break;
                default:
                    parser->list_type = LT_NUM;
            }
        }
    }
}

static void
process_tag_li(WPHTMLParser * parser)
{
    if (parser->list_type != LT_NONE)
    {
        html_insert_newline(parser, FALSE);

        ++parser->list_number;
        parser->fmt.bullet = !parser->is_close_tag;
        parser->fmt.cs.bullet = !parser->is_close_tag;
    }
}

/**
 * Process a font face attribute
 * @param parser is a #WPHTMLParser
 * @param name is the name of the font
 */
static void
process_font_face(WPHTMLParser * parser, gchar * name)
{
    // TODO: Should we support multiple font names separated by ',' ?
    parser->fmt.font = wp_get_font_index(name, parser->default_fmt.font);
}

/**
 * Process a font size attribute
 * @param parser is a #WPHTMLParser
 * @param value is the size of the font
 */
static void
process_font_size(WPHTMLParser * parser, gchar * value)
{
    gint sign = 0;
    gint size;

    if (*value == '+')
    {
        sign = 1;
        value++;
    }
    else if (*value == '-')
    {
        sign = -1;
        value++;
    }

    if (*value >= '1' && *value <= '7')
    {
        size = *value - '0';
        if (sign > 0)
        {
            size += 3;
            if (size >= WP_FONT_SIZE_COUNT)
                size = WP_FONT_SIZE_COUNT - 1;
        }
        else if (sign < 0)
        {
            size = 3 - size;
            if (size < 0)
                size = 0;
        }
        else
            size--;

        parser->fmt.font_size = size;
    }
    else
        g_warning("Invalid font size: %s\n", value);
}

/**
 * Process a font size attribute given in point size
 * @param parser is a #WPHTMLParser
 * @param value is the size of the font
 */
static void
process_font_pt_size(WPHTMLParser * parser, gchar * value)
{
    parser->fmt.font_size = wp_get_font_size_index(atoi(value),
                                                   parser->default_fmt.
                                                   font_size);
}

/**
 * Process a font style attribute
 * @param parser is a #WPHTMLParser
 * @param style holding the style to be parsed
 */
static void
process_font_style(WPHTMLParser * parser, gchar * style)
{
    gchar **list;
    list = g_strsplit(style, ":", 2);

    if (*list && list[1])
    {
        *list = g_strstrip(*list);

        if (strcmp(*list, "font-family") == 0)
            process_font_face(parser, g_strstrip(list[1]));
        else if (strcmp(*list, "font-size") == 0)
            // not totally correct, can have sizes too
            process_font_pt_size(parser, g_strstrip(list[1]));
    }
    g_strfreev(list);
}

static void
process_tag_font(WPHTMLParser * parser)
{
    HTMLFontType *font;

    if (!parser->is_close_tag)
    {
        gchar *attr = parser->last_tag_attr;

        if (parser->is_first_attr)
        {
            font = g_new0(HTMLFontType, 1);
            font->font = parser->fmt.font;
            font->font_size = parser->fmt.font_size;
            font->color = parser->fmt.color;
            parser->font_tags = g_slist_prepend(parser->font_tags, font);
        }

        if (strcmp(attr, "face") == 0)
        {
            process_font_face(parser, g_strstrip(parser->last_tag_value));
        }
        else if (strcmp(parser->last_tag_attr, "size") == 0)
        {
            process_font_size(parser, parser->last_tag_value);
        }
        else if (strcmp(parser->last_tag_attr, "color") == 0)
        {
            gdk_color_parse(parser->last_tag_value, &parser->fmt.color);
        }
        else if (strcmp(parser->last_tag_attr, "point-size") == 0)
        {
            process_font_pt_size(parser, parser->last_tag_value);
        }
        else if (strcmp(parser->last_tag_attr, "style") == 0)
        {
            gchar **head, **list;

            head = list = g_strsplit(parser->last_tag_value, ";", 5);
            for (; *list; list++)
                process_font_style(parser, *list);
            g_strfreev(head);
        }
    }
    else if (parser->font_tags)
    {
        GSList *tmp = parser->font_tags;
        parser->font_tags = tmp->next;
        tmp->next = NULL;

        font = tmp->data;
        parser->fmt.font = font->font;
        parser->fmt.font_size = font->font_size;
        parser->fmt.color = font->color;
        g_slist_free_1(tmp);
    }
}

static void
process_tag_img (WPHTMLParser *parser)
{
    if (!parser->is_close_tag)
    {
       gchar *attr = parser->last_tag_attr;

       if (strcmp (attr, "src") == 0)
       {
           gchar *src = parser->last_tag_value;
           if ((src != NULL)&&(g_str_has_prefix (src, "cid:")))
               html_insert_image (parser, src+4);
       }
           
    }
}

static void
process_skip_tag(WPHTMLParser * parser)
{
    if (parser->is_first_attr)
    {
        if (!parser->is_close_tag)
            parser->skip_text++;
        else if (parser->skip_text > 0)
            parser->skip_text--;
    }
}

static void
process_tag_br(WPHTMLParser * parser)
{
    html_insert_newline(parser, TRUE);
}

static void
process_tag_p(WPHTMLParser * parser)
{
    gboolean bullet = parser->fmt.bullet && !parser->is_close_tag;
    html_insert_newline(parser, FALSE);
    // reset attributes to default except bullet
    parser->fmt = parser->default_fmt;
    parser->fmt.bullet = bullet;
    process_align(parser);
    html_free_font_tags(parser);
}

/* 
 * int main() { gchar buffer[4096]; gint size, fd; WPHTMLParser *parser;
 * 
 * parser = wp_html_parser_new(); wp_html_parser_begin(parser); fd =
 * open("a.note.html", O_RDONLY); while ((size = read(fd, buffer,
 * sizeof(buffer)))) wp_html_parser_write(parser, buffer, size); close(fd);
 * wp_html_parser_end(parser); wp_html_parser_free(parser);
 * 
 * finalize_html_parser_library();
 * 
 * return 0; } */
