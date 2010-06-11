/**
 * @file wpundo.c
 *
 * Implementation file for undo/redo functionality
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

#include <glib.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

#include "wpundo.h"
#include "wptextbuffer.h"
#include "wptextbuffer-private.h"

/** Minimum undo level */
#define MIN_UNDO_LEVEL 5
/** Maximum undo level */
#define MAX_UNDO_LEVEL 200
/** Default undo level */
#define DEF_UNDO_LEVEL 5

/** Type of the undo operation */
typedef enum {
    WP_UNDO_INSERT,
    WP_UNDO_DELETE,
    WP_UNDO_TAG,
    WP_UNDO_SIMPLE_JUSTIFY,
    WP_UNDO_SELECT,
    WP_UNDO_FMT,
    WP_UNDO_LAST_LINE_JUSTIFY,
} WPUndoType;

/** An undo operation type */
typedef struct {
    WPUndoType type;

    gint start;
    gint end;
    gchar *text;
    GtkTextTag *orig_tag;
    GtkTextTag *tag;
    GSList *orig_tags;
    GSList *tags;
    gint sel_start;
    gint sel_end;
    gint old_line_justify;
    gint new_line_justify;
    gchar mergeable:1;
    gchar backspace:1;
    gchar rich_text:1;
} WPUndoOperation;

/** Apply tag type undo operation */
typedef struct {
    gboolean apply;
    gint start;
    gint end;
    GtkTextTag *tag;
} WPUndoTag;

/** Private structure to pass as userdata to gtk+ functions */
typedef struct {
    GSList *tags;
    const GtkTextIter *end;
} WPHashData;

/** The object's private variables */
struct _WPUndoPrivate {
    /** Contain a GSList of WPUndoOperations, used for undo */
    GSList *undo_queue;
    /** Contain a GSList of WPUndoOperations, used for redo */
    GSList *redo_queue;

    /** Hash table used for finding the toggled tags in the buffer */
    GHashTable *hash;

    /** Contain the recent added operation list */
    GSList *current_op_list;
    /** Contain the recent added operation */
    WPUndoOperation *current_op;
    /** True if the last inserted character was a whitespace */
    gint last_char_is_space:1;
    /** True if the operation is the first operation after group start */
    gint first_in_group:1;

    /** Reference to the group number. If >0 then a group is started */
    gint group;
    /** Reference to the undo disabled variable. If >0 then the undo is disabled */
    gint undo_disabled;
    /** Maximum undo level */
    gint max_undo_level;
    /** Current undo queue length */
    gint undo_queue_len;

    /** Last undo status signaled */
    gint undo_sent:1;
    /** Last redo status signaled */
    gint redo_sent:1;

    /** True if there is a low memory situation. In this the undo is disabled */
    gint low_mem:1;
    /** True if there was not enough memory during inserting a new operation to the
     * undo queue. If is enabled, no more operations will be registered until the group
     * is ended */
    gint disable_this_group:1;

    /** Pointer a #GtkTextBuffer associated with the undo */
    GtkTextBuffer *text_buffer;
};

/** Signals */
enum {
    /** Sent when undo state has changed */
    CAN_UNDO,
    /** Sent when redo state has changed */
    CAN_REDO,
    /** Sent when formatting has changed (rich text <-> plain text) */
    FMT_CHANGED,
    /** Sent when last line justification has changed */
    LAST_LINE_JUSTIFY,
    /** Sent when there is not enough memory for the current operation */
    NO_MEMORY,
    LAST_SIGNAL
};

/** Properties */
enum {
    PROP_0,
    /** R/W. Pointer. Specify a GtkTextBuffer */
    PROP_DOCUMENT,
    /** R/W. Integer. Specify the number of undo levels */
    PROP_UNDO_LEVEL,
    /** R/W. Boolean. Specify if there is a low memory situation */
    PROP_LOW_MEM
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(WPUndo, wp_undo, G_TYPE_OBJECT);

static void wp_undo_finalize(GObject * object);

static void wp_undo_get_property(GObject * object,
                                 guint prop_id,
                                 GValue * value, GParamSpec * pspec);
static void wp_undo_set_property(GObject * object,
                                 guint prop_id,
                                 const GValue * value, GParamSpec * pspec);

/**
 * Free a #GSList of WPUndoTags
 * @param tags contains the list of tags
 * @return <b>NULL</b>
 */
static GSList *wp_undo_free_tags(GSList * tags);

/**
 * Free an operation list. An operation list contains a #GSList of
 * #GSList of WPUndoTags.
 * @param queue pointer to the #GSList
 * @return <b>NULL</b>
 */
static GSList *wp_undo_free_op_list(GSList * queue);

/**
 * Add a new operation to the undo queue
 * @param undo pointer to the undo object
 * @param op contains the operation to be added
 */
static void wp_undo_add_queue(WPUndo * undo, WPUndoOperation * op);

/**
 * Create a new WPUndoTag from the given parameters.
 * @param start is the start offset the tag
 * @param end contains the end iterator of the applied tag
 * @param tag contains the tag itself
 * @param enable is <b>TRUE</b> if the tag is applied on the range
 * @return a newly allocated WPUndoTag
 */
static WPUndoTag *wp_undo_create_tag(gint start, const GtkTextIter * end,
                                     GtkTextTag * tag, gboolean enable);

/**
 * Send the undo and redo signals if the state of the queue has changed from
 * the last call.
 * @param undo pointer to the undo object
 */
static void wp_undo_send_signals(const WPUndo * undo);

/**
 * Send the no memory signal.
 * @param undo pointer to the undo object
 */
static void emit_no_memory(WPUndo * undo);

static void
wp_undo_class_init(WPUndoClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->set_property = wp_undo_set_property;
    object_class->get_property = wp_undo_get_property;
    object_class->finalize = wp_undo_finalize;

    klass->can_undo = NULL;
    klass->can_redo = NULL;
    klass->fmt_changed = NULL;
    klass->last_line_justify = NULL;
    klass->no_memory = NULL;

    g_object_class_install_property(object_class,
                                    PROP_DOCUMENT,
                                    g_param_spec_pointer("document",
                                                         "document",
                                                         "GtkTextView document on which the undo/redo is working",
                                                         G_PARAM_CONSTRUCT
                                                         |
                                                         G_PARAM_READWRITE));
    g_object_class_install_property(object_class, PROP_UNDO_LEVEL,
                                    g_param_spec_int("undo_levels",
                                                     "undo_levels",
                                                     "Maximum undo levels allowed",
                                                     MIN_UNDO_LEVEL,
                                                     MAX_UNDO_LEVEL,
                                                     DEF_UNDO_LEVEL,
                                                     G_PARAM_CONSTRUCT |
                                                     G_PARAM_READWRITE));
    g_object_class_install_property(object_class, PROP_LOW_MEM,
                                    g_param_spec_boolean("low_memory",
                                                         "low_memory",
                                                         "Low memory situation (undo disabled)",
                                                         FALSE,
                                                         G_PARAM_READWRITE));

    signals[CAN_UNDO] =
        g_signal_new("can_undo",
                     G_OBJECT_CLASS_TYPE(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(WPUndoClass, can_undo),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__BOOLEAN,
                     G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

    signals[CAN_REDO] =
        g_signal_new("can_redo",
                     G_OBJECT_CLASS_TYPE(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(WPUndoClass, can_redo),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__BOOLEAN,
                     G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

    signals[FMT_CHANGED] =
        g_signal_new("fmt_changed",
                     G_OBJECT_CLASS_TYPE(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(WPUndoClass, fmt_changed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__BOOLEAN,
                     G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

    signals[LAST_LINE_JUSTIFY] =
        g_signal_new("last_line_justify",
                     G_OBJECT_CLASS_TYPE(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(WPUndoClass, last_line_justify),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE, 1, G_TYPE_INT);

    signals[NO_MEMORY] =
        g_signal_new("no_memory",
                     G_OBJECT_CLASS_TYPE(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(WPUndoClass, no_memory),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static void
wp_undo_init(WPUndo * undo)
{
    undo->priv = g_new0(WPUndoPrivate, 1);
    undo->priv->max_undo_level = DEF_UNDO_LEVEL;
    undo->priv->hash = g_hash_table_new(g_direct_hash, g_direct_equal);
}

static void
wp_undo_finalize(GObject * object)
{
    WPUndo *undo;

    undo = WP_UNDO(object);

    undo->priv->undo_queue = wp_undo_free_op_list(undo->priv->undo_queue);
    undo->priv->redo_queue = wp_undo_free_op_list(undo->priv->redo_queue);
    g_hash_table_destroy(undo->priv->hash);

    g_free(undo->priv);

    G_OBJECT_CLASS(wp_undo_parent_class)->finalize(object);
}

static void
wp_undo_set_property(GObject * object,
                     guint prop_id, const GValue * value, GParamSpec * pspec)
{
    WPUndo *undo = WP_UNDO(object);
    gint new_size, size, qsize;
    GSList *tmp;

    switch (prop_id)
    {
        case PROP_DOCUMENT:
            undo->priv->text_buffer = g_value_get_pointer(value);
            break;
        case PROP_UNDO_LEVEL:
            new_size = g_value_get_int(value);
            if ((size = new_size - undo->priv->max_undo_level) < 0)
            {
                size = -size;
                qsize = g_slist_length(undo->priv->redo_queue);
                if (qsize > size)
                {
                    tmp =
                        g_slist_nth(undo->priv->redo_queue, qsize - size - 1);
                    tmp->next = wp_undo_free_op_list(tmp->next);
                    size = 0;
                }
                else
                {
                    undo->priv->redo_queue =
                        wp_undo_free_op_list(undo->priv->redo_queue);
                    size -= qsize;
                }

                if (size > 0)
                {
                    if (undo->priv->undo_queue_len > size)
                    {
                        tmp = g_slist_nth(undo->priv->redo_queue,
                                          undo->priv->undo_queue_len - size -
                                          1);
                        tmp->next = wp_undo_free_op_list(tmp->next);
                        undo->priv->undo_queue_len -= size;
                    }
                    else
                    {
                        undo->priv->undo_queue =
                            wp_undo_free_op_list(undo->priv->undo_queue);
                        undo->priv->undo_queue_len = 0;
                    }
                }
            }
            undo->priv->max_undo_level = new_size;

            wp_undo_send_signals(undo);
            break;
        case PROP_LOW_MEM:
            undo->priv->low_mem = g_value_get_boolean(value);
            if (undo->priv->low_mem)
                wp_undo_reset(undo);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}


static void
wp_undo_get_property(GObject * object,
                     guint prop_id, GValue * value, GParamSpec * pspec)
{
    WPUndo *undo = WP_UNDO(object);

    switch (prop_id)
    {
        case PROP_DOCUMENT:
            g_value_set_pointer(value, undo->priv->text_buffer);
            break;
        case PROP_UNDO_LEVEL:
            g_value_set_int(value, undo->priv->max_undo_level);
            break;
        case PROP_LOW_MEM:
            g_value_set_boolean(value, undo->priv->low_mem != FALSE);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

WPUndo *
wp_undo_new(GtkTextBuffer * buffer)
{
    WPUndo *undo = WP_UNDO(g_object_new(WP_TYPE_UNDO, NULL));

    undo->priv->text_buffer = buffer;

    return undo;
}

void
wp_undo_freeze(WPUndo * undo)
{
    g_return_if_fail(WP_IS_UNDO(undo));

    ++undo->priv->undo_disabled;
}

void
wp_undo_thaw(WPUndo * undo)
{
    g_return_if_fail(WP_IS_UNDO(undo));

    --undo->priv->undo_disabled;
}

gboolean
wp_undo_is_enabled(WPUndo * undo)
{
    g_return_val_if_fail(WP_IS_UNDO(undo), FALSE);

    return undo->priv->undo_disabled == 0 && undo->priv->low_mem == FALSE;
}

void
wp_undo_start_group(WPUndo * undo)
{
    g_return_if_fail(WP_IS_UNDO(undo));

    if (++undo->priv->group == 1)
    {
        undo->priv->first_in_group = TRUE;
        undo->priv->disable_this_group = FALSE;
    }
}

void
wp_undo_end_group(WPUndo * undo)
{
    g_return_if_fail(WP_IS_UNDO(undo));

    if (--undo->priv->group == 0)
        undo->priv->disable_this_group = FALSE;
}

static void
wp_undo_send_signals(const WPUndo * undo)
{
    gboolean enable = undo->priv->low_mem == FALSE;

    if (undo->priv->redo_sent != (undo->priv->redo_queue != NULL) && enable)
    {
        undo->priv->redo_sent = undo->priv->redo_queue != NULL && enable;
        g_signal_emit(G_OBJECT(undo), signals[CAN_REDO], 0,
                      undo->priv->redo_sent);
    }

    if (undo->priv->undo_sent != (undo->priv->undo_queue != NULL) && enable)
    {
        undo->priv->undo_sent = undo->priv->undo_queue != NULL && enable;
        g_signal_emit(G_OBJECT(undo), signals[CAN_UNDO], 0,
                      undo->priv->undo_sent);
    }
}

gboolean
wp_undo_can_undo(const WPUndo * undo)
{
    g_return_val_if_fail(WP_IS_UNDO(undo), FALSE);

    return undo->priv->undo_queue != NULL && undo->priv->low_mem == FALSE;
}

gboolean
wp_undo_can_redo(const WPUndo * undo)
{
    g_return_val_if_fail(WP_IS_UNDO(undo), FALSE);

    return undo->priv->redo_queue != NULL && undo->priv->low_mem == FALSE;
}

void
wp_undo_reset_mergeable(const WPUndo * undo)
{
    g_return_if_fail(WP_IS_UNDO(undo));

    if (undo->priv->current_op)
    {
        undo->priv->current_op->mergeable = FALSE;
        undo->priv->current_op = NULL;
        undo->priv->current_op_list = NULL;
    }
}

/**
 * Apply a #GSList of WPUndoTag to a #GtkTextBuffer.
 * @param buffer is a #GtkTextBuffer associated with the undo
 * @param tags pointer to the list of tags
 */
static void
wp_undo_apply_saved_tags(GtkTextBuffer * buffer, GSList * tags)
{
    WPUndoTag *tag;
    GtkTextIter s, e;
    GSList *current = tags;

    /* First iteration will apply the remove tag commands, second iteration
     * will apply the add tag commands. Sometimes there's an apply and
     * a removed for the same tag in the taglist (it can come from incorrect
     * use of changesets in wp_text_buffer_set_format). If the apply is the
     * first in the list and remove is the second (which can be the case with
     * red), the property belonging to that tag would be restored to it's
     * default. To avoid this, we can first execute the remove, then the apply
     * operations, or we can maintain the taglist. Since the second one is
     * more error-prone, and requires more resources, the first approach is
     * used. */
    while (current)
    {
        tag = (WPUndoTag *) current->data;
        if (!tag->apply) {
            gtk_text_buffer_get_iter_at_offset(buffer, &s, tag->start);
            gtk_text_buffer_get_iter_at_offset(buffer, &e, tag->end);
            gtk_text_buffer_remove_tag(buffer, tag->tag, &s, &e);
        }
        current = current->next;
    }

    current = tags;
    while (current)
    {
        tag = (WPUndoTag *) current->data;
        if (tag->apply) {
            gchar *tag_name = NULL;
            g_object_get (G_OBJECT (tag->tag), "name", &tag_name, NULL);
            gtk_text_buffer_get_iter_at_offset(buffer, &s, tag->start);
            gtk_text_buffer_get_iter_at_offset(buffer, &e, tag->end);
            if (tag_name != NULL && 
                    g_str_has_prefix (tag_name, "image-tag-") &&
                    !g_str_has_prefix (tag_name, "image-tag-replace-")) {
                gchar *new_name;
                const gchar *image_id;
                image_id = g_object_get_data (G_OBJECT (tag->tag), "image-index");
                if (image_id != NULL) {
                    GtkTextTagTable *tag_table;
                    gtk_text_buffer_remove_tag (buffer, tag->tag, &s, &e);
                    new_name = g_strdup_printf ("image-tag-replace-%s", image_id);
                    tag_table = gtk_text_buffer_get_tag_table (buffer);
                    tag->tag = gtk_text_tag_table_lookup (tag_table, new_name);
                    if (tag->tag == NULL)
                        tag->tag = gtk_text_buffer_create_tag (buffer, new_name, NULL);
                    g_free (new_name);
                    gtk_text_buffer_apply_tag (buffer, tag->tag, &s, &e);
                }
            } else {
                gtk_text_buffer_apply_tag(buffer, tag->tag, &s, &e);
            }
	 /* freeing the tag_name */
	 g_free(tag_name);
         }
        current = current->next;
    }
}

/**
 * Redo/Undo a selection. If there is something selected in the textbuffer
 * and the undo/redo operation is selecting the same text, then skip that 
 * undo/redo. If the next operation is also a selection, apply that, otherwise
 * unselect the text.
 * @param text_buffer is a #GtkTextBuffer associated with the undo
 * @param queue contains either the undo or redo queue
 * @param op contains the current undo/redo operation
 * @param first if the function was started for the first time
 */
static void
wp_undo_redo_selection(GtkTextBuffer * text_buffer, GSList * queue,
                       WPUndoOperation * op, gboolean first)
{
    GtkTextIter start, end, sstart, send;
    WPUndoOperation *pop;
    gboolean repeat = FALSE;

    gtk_text_buffer_get_selection_bounds(text_buffer, &sstart, &send);

    if (op->type == WP_UNDO_SELECT)
    {
        gtk_text_buffer_get_iter_at_offset(text_buffer, &start,
                                           op->sel_start);
        gtk_text_buffer_get_iter_at_offset(text_buffer, &end, op->sel_end);
    }

    if (gtk_text_iter_equal(&sstart, &start) &&
        gtk_text_iter_equal(&send, &end))
    {
        if (queue && queue->data)
        {
            pop = (WPUndoOperation *) ((GSList *) queue->data)->data;
            repeat = first && pop && pop->type == WP_UNDO_SELECT;
        }

        if (!repeat)
            gtk_text_buffer_place_cursor(text_buffer, &end);
        else
            wp_undo_redo_selection(text_buffer, queue, pop, FALSE);
    }
    else
        gtk_text_buffer_select_range(text_buffer, &start, &end);
}

/**
 * Skip a bullet tag at the cursor position if there is any.
 * @param text_buffer is a #GtkTextBuffer associated with the undo
 */
static void
wp_undo_skip_bullet_at_cursor(GtkTextBuffer * text_buffer)
{

    WPTextBuffer *buffer = WP_TEXT_BUFFER(text_buffer);
    GtkTextTag *tag = _wp_text_buffer_get_bullet_tag(buffer);
    GtkTextIter pos;

    gtk_text_buffer_get_iter_at_mark(text_buffer, &pos,
                                     gtk_text_buffer_get_insert(text_buffer));
    if (_wp_text_iter_skip_bullet(&pos, tag, TRUE))
        gtk_text_buffer_place_cursor(text_buffer, &pos);
}

void
wp_undo_undo(WPUndo * undo)
{
    WPUndoOperation *op = NULL;
    GSList *lact;
    GtkTextBuffer *text_buffer;
    GtkTextIter start, end;
    gint proposed_cursor_pos = -1;

    g_return_if_fail(WP_IS_UNDO(undo));

    if (!undo->priv->undo_queue || undo->priv->low_mem)
        return;

    wp_undo_freeze(undo);
    gtk_text_buffer_begin_user_action(undo->priv->text_buffer);

    text_buffer = undo->priv->text_buffer;

    lact = undo->priv->undo_queue;
    undo->priv->undo_queue = undo->priv->undo_queue->next;
    lact->next = undo->priv->redo_queue;
    undo->priv->redo_queue = lact;
    undo->priv->undo_queue_len--;

    lact = (GSList *) lact->data;
        
    while (lact)
    {
        op = (WPUndoOperation *) lact->data;
        lact = lact->next;

        g_return_if_fail(op != NULL);  
        
        switch (op->type)
        {
            case WP_UNDO_DELETE:
                gtk_text_buffer_get_iter_at_offset(text_buffer,
                                                   &start, op->start);

                gtk_text_buffer_insert(text_buffer,
                                       &start,
                                       op->text, (int) strlen(op->text));

                end = start;
                gtk_text_buffer_get_iter_at_offset(text_buffer,
                                                   &start, op->start);

                proposed_cursor_pos = op->backspace ? op->end : op->start;

                gtk_text_buffer_remove_all_tags(text_buffer, &start, &end);

                wp_undo_apply_saved_tags(text_buffer, op->tags);

                break;

            case WP_UNDO_INSERT:
                gtk_text_buffer_get_iter_at_offset(text_buffer,
                                                   &start, op->start);
                gtk_text_buffer_get_iter_at_offset(text_buffer, &end,
                                                   op->end);

            	/* Bullet are also handled in insert */
            	if( gtk_text_iter_toggles_tag( &start,
	            	_wp_text_buffer_get_bullet_tag(
	            		WP_TEXT_BUFFER( text_buffer ) ) ) ) {
	            		
            		GtkTextIter at = start;
            		while( gtk_text_iter_ends_line( &at ) ) {
            			++proposed_cursor_pos;
						gtk_text_buffer_get_iter_at_offset(text_buffer, &at,
							1 );		
            		}
            		
				} else {
					proposed_cursor_pos = op->start;
				}

                gtk_text_buffer_delete(text_buffer, &start, &end);

                break;
            case WP_UNDO_TAG:
                gtk_text_buffer_get_iter_at_offset(text_buffer,
                                                   &start, op->start);
                gtk_text_buffer_get_iter_at_offset(text_buffer, &end,
                                                   op->end);

                gtk_text_buffer_select_range(text_buffer, &start, &end);

                gtk_text_buffer_remove_all_tags(text_buffer, &start, &end);

                wp_undo_apply_saved_tags(text_buffer, op->orig_tags);
                break;
            case WP_UNDO_SELECT:
                wp_undo_redo_selection(text_buffer, undo->priv->undo_queue,
                                       op, TRUE);
                break;
            case WP_UNDO_FMT:
                if (!op->rich_text)
                {                	
                    GtkTextIter at;
                    /* Mark current position */
					GtkTextMark * mark = gtk_text_buffer_get_insert(
						text_buffer );
						
                    wp_undo_apply_saved_tags(text_buffer, op->tags);

					/* Set focus to current position */
					gtk_text_buffer_get_iter_at_mark( text_buffer, &at, mark );
					gtk_text_buffer_place_cursor( text_buffer, &at );
					
					/* Undo changes to cursor pos */
					proposed_cursor_pos = -1;
                    
                }
                else
                {
                    gtk_text_buffer_get_bounds(text_buffer, &start, &end);
                    gtk_text_buffer_remove_all_tags(text_buffer, &start,
                                                    &end);
                }

                g_signal_emit(G_OBJECT(undo), signals[FMT_CHANGED], 0,
                              !op->rich_text);
                break;
            case WP_UNDO_SIMPLE_JUSTIFY:
                gtk_text_buffer_get_iter_at_offset(text_buffer,
                                                   &start, op->start);
                gtk_text_buffer_get_iter_at_offset(text_buffer, &end,
                                                   op->end);
                if (op->tag)
                    gtk_text_buffer_remove_tag(text_buffer, op->tag, &start,
                                               &end);
                gtk_text_buffer_apply_tag(text_buffer, op->orig_tag, &start,
                                          &end);
                break;
            case WP_UNDO_LAST_LINE_JUSTIFY:
                g_signal_emit(G_OBJECT(undo), signals[LAST_LINE_JUSTIFY], 0,
                              op->old_line_justify);
                break;

            default:
                g_warning
                    ("wp_undo_undo. Unknown undo tag. This should not happen.");
                return;
        }
    }

    if (proposed_cursor_pos != -1)
    {
        gtk_text_buffer_get_iter_at_offset(text_buffer, &start,
                                           proposed_cursor_pos);
        gtk_text_buffer_place_cursor(text_buffer, &start);
    }

    gtk_text_buffer_end_user_action(undo->priv->text_buffer);
    wp_undo_thaw(undo);

    wp_undo_reset_mergeable(undo);
    wp_undo_send_signals(undo);
}

void
wp_undo_redo(WPUndo * undo)
{
    WPUndoOperation *op = NULL;
    GSList *lact, *lact_head;
    GtkTextBuffer *text_buffer;
    GtkTextIter start, end;
    gint proposed_cursor_pos = -1;
    WPUndoTag *utag;

    g_return_if_fail(WP_IS_UNDO(undo));

    if (!undo->priv->redo_queue || undo->priv->low_mem)
        return;

    wp_undo_freeze(undo);
    gtk_text_buffer_begin_user_action(undo->priv->text_buffer);

    text_buffer = undo->priv->text_buffer;

    lact = undo->priv->redo_queue;
    undo->priv->redo_queue = undo->priv->redo_queue->next;
    lact->next = undo->priv->undo_queue;
    undo->priv->undo_queue = lact;
    undo->priv->undo_queue_len++;

    lact = lact_head = g_slist_reverse((GSList *) lact->data);
    while (lact)
    {
        op = (WPUndoOperation *) lact->data;
        lact = lact->next;

        g_return_if_fail(op != NULL);      

        switch (op->type)
        {
            case WP_UNDO_DELETE:
                gtk_text_buffer_get_iter_at_offset(text_buffer,
                                                   &start, op->start);
                gtk_text_buffer_get_iter_at_offset(text_buffer, &end,
                                                   op->end);

                gtk_text_buffer_delete(text_buffer, &start, &end);

                proposed_cursor_pos = op->backspace ? op->end : op->start;

                break;

            case WP_UNDO_INSERT:
                gtk_text_buffer_get_iter_at_offset(text_buffer,
                                                   &start, op->start);

                gtk_text_buffer_insert(text_buffer,
                                       &start,
                                       op->text, (int) strlen(op->text));
                proposed_cursor_pos = op->end;                

                wp_undo_apply_saved_tags(text_buffer, op->tags);

                break;
            case WP_UNDO_TAG:
                utag = (WPUndoTag *) op->tags->data;

                gtk_text_buffer_get_iter_at_offset(text_buffer,
                                                   &start, op->start);
                gtk_text_buffer_get_iter_at_offset(text_buffer, &end,
                                                   op->end);

                gtk_text_buffer_select_range(text_buffer, &start, &end);

                wp_undo_apply_saved_tags(text_buffer, op->tags);

                break;
            case WP_UNDO_SELECT:
                wp_undo_redo_selection(text_buffer, undo->priv->redo_queue,
                                       op, TRUE);
                break;
            case WP_UNDO_FMT:
                if (op->rich_text)
                {
                    wp_undo_apply_saved_tags(text_buffer, op->tags);
                    wp_undo_skip_bullet_at_cursor(text_buffer);
                }
                else
                {
                    gtk_text_buffer_get_bounds(text_buffer, &start, &end);
                    gtk_text_buffer_remove_all_tags(text_buffer, &start,
                                                    &end);
                }

                g_signal_emit(G_OBJECT(undo), signals[FMT_CHANGED], 0,
                              op->rich_text);
                break;
            case WP_UNDO_SIMPLE_JUSTIFY:
                gtk_text_buffer_get_iter_at_offset(text_buffer,
                                                   &start, op->start);
                gtk_text_buffer_get_iter_at_offset(text_buffer, &end,
                                                   op->end);
                gtk_text_buffer_remove_tag(text_buffer, op->orig_tag, &start,
                                           &end);
                if (op->tag)
                    gtk_text_buffer_apply_tag(text_buffer, op->tag, &start,
                                              &end);
                break;
            case WP_UNDO_LAST_LINE_JUSTIFY:
                g_signal_emit(G_OBJECT(undo), signals[LAST_LINE_JUSTIFY], 0,
                              op->new_line_justify);
                break;

            default:
                g_warning
                    ("wp_undo_redo. Unknown undo tag. This should not happen.");
                return;
        }

        undo->priv->current_op = NULL;
        undo->priv->current_op_list = NULL;
    }
    // no point in reversing as it is no longer used.
    //lact_head = g_slist_reverse(lact_head);

    if (proposed_cursor_pos != -1)
    {
        gtk_text_buffer_get_iter_at_offset(text_buffer, &start,
                                           proposed_cursor_pos);
        gtk_text_buffer_place_cursor(text_buffer, &start);
    }

    gtk_text_buffer_end_user_action(undo->priv->text_buffer);
    wp_undo_thaw(undo);

    wp_undo_reset_mergeable(undo);
    wp_undo_send_signals(undo);
}

static GSList *
wp_undo_free_tags(GSList * tags)
{
    GSList *tmp = tags;

    while (tmp)
    {
        g_free(tmp->data);
        tmp = tmp->next;
    }
    g_slist_free(tmp);

    return NULL;
}

static GSList *
wp_undo_free_op_list(GSList * queue)
{
    GSList *tmp_queue;
    GSList *action_list, *tmp;
    WPUndoOperation *act;

    tmp_queue = queue;
    while (tmp_queue)
    {
        action_list = tmp = (GSList *) tmp_queue->data;
        tmp_queue = tmp_queue->next;

        while (tmp)
        {
            act = (WPUndoOperation *) tmp->data;
            tmp = tmp->next;

            if (act)
            {
                g_free(act->text);
                wp_undo_free_tags(act->orig_tags);
                wp_undo_free_tags(act->tags);
            }

            g_free(act);
        }

        g_slist_free(action_list);
    }

    g_slist_free(queue);

    return NULL;
}

static WPUndoTag *
wp_undo_create_tag(gint start, const GtkTextIter * end,
                   GtkTextTag * tag, gboolean enable)
{
    WPUndoTag *result = g_new(WPUndoTag, 1);
    result->start = start;
    result->end = gtk_text_iter_get_offset(end);
    result->tag = tag;
    result->apply = enable;
    // printf("Create-tag: %s, %d-%d, %d\n", result->tag->name,
    // result->start, result->end, enable);
    return result;
}

/**
 * Callback to close the opened tags in the hash table.
 * @key pointer to a #GtkTextTag
 * @value contains the start offset of the tag
 * @user_data pointer to a #WPHashData structure
 * @return <b>TRUE</b> to remove the element from the table
 */
static gboolean
wp_undo_close_opened_tags(gpointer key, gpointer value, gpointer user_data)
{
    WPHashData *data = (WPHashData *) user_data;
    data->tags = g_slist_prepend(data->tags,
                                 wp_undo_create_tag(GPOINTER_TO_INT(value)
                                                    - 1, data->end,
                                                    GTK_TEXT_TAG(key), TRUE));
    return TRUE;
}

/**
 * Creates a tag list for the specified range
 * @param undo pointer to the undo object
 * @param start contains the start iterator of the range
 * @param end contains the end iterator of the range
 * @return <b>NULL</b>
 */
static GSList *
wp_undo_get_toggled_tags(WPUndo * undo,
                         const GtkTextIter * start, const GtkTextIter * end)
{
    GSList *tags = NULL;
    GHashTable *hash = undo->priv->hash;
    GtkTextIter tmp = *start;
    gint tmp_start;
    GSList *list, *list_head;
    WPHashData data;

    /* TODO maybe is enough to save only the real toggled tags */
    /* list = list_head = gtk_text_iter_get_toggled_tags(&tmp, FALSE); while
     * (list) { tags = g_slist_prepend(tags,
     * wp_undo_create_tag(gtk_text_iter_get_offset(start), end,
     * GTK_TEXT_TAG(list->data), FALSE)); list = list->next; }
     * g_slist_free(list_head); */

    // list = list_head = gtk_text_iter_get_toggled_tags(&tmp, TRUE);
    list = list_head = gtk_text_iter_get_tags(&tmp);
    while (list)
    {
        g_hash_table_insert(hash, list->data,
                            GINT_TO_POINTER(gtk_text_iter_get_offset(&tmp)
                                            + 1));
        list = list->next;
    }
    g_slist_free(list_head);

    while (gtk_text_iter_forward_to_tag_toggle(&tmp, NULL))
    {
        if (gtk_text_iter_compare(&tmp, end) >= 0)
            break;

        list = list_head = gtk_text_iter_get_toggled_tags(&tmp, FALSE);
        while (list)
        {
            if ((tmp_start =
                 GPOINTER_TO_INT(((GtkTextIter
                                   *) (g_hash_table_lookup(hash,
                                                           list->data))))))
            {
                tags = g_slist_prepend(tags,
                                       wp_undo_create_tag(tmp_start - 1,
                                                          &tmp,
                                                          GTK_TEXT_TAG
                                                          (list->data),
                                                          TRUE));
                g_hash_table_remove(hash, list->data);
            }
            list = list->next;
        }
        g_slist_free(list_head);

        list = list_head = gtk_text_iter_get_toggled_tags(&tmp, TRUE);
        while (list)
        {
            g_hash_table_insert(hash, list->data,
                                GINT_TO_POINTER(gtk_text_iter_get_offset
                                                (&tmp) + 1));
            list = list->next;
        }
        g_slist_free(list_head);
    }

    data.tags = tags;
    data.end = end;
    g_hash_table_foreach_remove(hash, wp_undo_close_opened_tags, &data);

    return g_slist_reverse(data.tags);
}

/**
 * Modified the tag range in a #GSList of WPUndoTag
 * @param tags pointer to a #GSList of tags
 * @param start is the start offset of the tag
 * @param end is the end offset of the tag
 */
static void
update_tags_range(GSList * tags, gint start, gint end)
{
    GSList *iter = tags;
    WPUndoTag *tag;

    while (iter)
    {
        tag = (WPUndoTag *) iter->data;
        tag->start = start;
        tag->end = end;
        iter = iter->next;
    }
}

void
wp_undo_insert_text(WPUndo * undo,
                    GtkTextIter * pos, const gchar * text, gint length)
{
    WPUndoOperation co = { 0 };
    WPUndoOperation *op;
    WPUndoOperation *la;
    gboolean is_space = FALSE;

    g_return_if_fail(WP_IS_UNDO(undo));

    if (undo->priv->undo_disabled > 0 || undo->priv->low_mem)
        return;

    g_return_if_fail(strlen(text) == (guint) length);

    la = undo->priv->current_op;
    co.start = gtk_text_iter_get_offset(pos);
    co.end = co.start + g_utf8_strlen(text, length);

    co.mergeable = !((co.end - co.start > 1)
                     || (g_utf8_get_char(text) == '\n'));
    if (co.mergeable)
        is_space = g_unichar_isspace(g_utf8_get_char(text));

    if (co.mergeable && la && la->mergeable && la->type == WP_UNDO_INSERT)
    {
        if (la->end == co.start &&
            (is_space || !undo->priv->last_char_is_space))
        {
           gchar*temp_str = la->text;
            la->text = g_strconcat(la->text, text, NULL);
	   g_free(temp_str);  /*bug 140583*/
            update_tags_range(la->tags, la->start, la->end);
            undo->priv->last_char_is_space = is_space;
            la->end = co.end;
            return;
        }
        else
            la->mergeable = FALSE;
    }

    op = g_new0(WPUndoOperation, 1);
    if (!op)
    {
        emit_no_memory(undo);
        return;
    }
    *op = co;

    op->type = WP_UNDO_INSERT;
    op->text = g_strdup(text);
    undo->priv->last_char_is_space = is_space;
    if (!op->text)
    {
        emit_no_memory(undo);
        g_free(op);
        return;
    }

    wp_undo_add_queue(undo, op);
}

void
wp_undo_delete_range(WPUndo * undo, GtkTextIter * start, GtkTextIter * end)
{
    WPUndoOperation co = { 0 };
    WPUndoOperation *op;
    WPUndoOperation *la;
    GtkTextIter pos;
    gboolean mergeable = FALSE;
    gboolean is_space;
    gchar *str = NULL;

    g_return_if_fail(WP_IS_UNDO(undo));

    if (undo->priv->undo_disabled > 0 || undo->priv->low_mem)
        return;

    la = undo->priv->current_op;
    gtk_text_buffer_get_iter_at_mark(undo->priv->text_buffer,
                                     &pos,
                                     gtk_text_buffer_get_insert(undo->
                                                                priv->
                                                                text_buffer));
    co.backspace = co.start < gtk_text_iter_get_offset(&pos);

    co.start = gtk_text_iter_get_offset(start);
    co.end = gtk_text_iter_get_offset(end);
    co.text =
        gtk_text_buffer_get_slice(undo->priv->text_buffer, start, end, TRUE);
    if (!co.text)
    {
        emit_no_memory(undo);
        return;
    }
    is_space = g_unichar_isspace(g_utf8_get_char(co.text));
    co.mergeable = !(((co.end - co.start) > 1)
                     || (g_utf8_get_char(co.text) == '\n'));
    if (co.mergeable)
        mergeable = co.backspace ?
            !gtk_text_iter_toggles_tag(start, NULL) :
            !gtk_text_iter_toggles_tag(end, NULL);

    if (co.mergeable && la && la->mergeable && la->type == WP_UNDO_DELETE
        && la->backspace == co.backspace)
    {
        if (co.backspace && la->start == co.end &&
            (is_space || !undo->priv->last_char_is_space))
        {
            str = g_strconcat(co.text, la->text, NULL);
            la->start = co.start;
        }
        else if (!co.backspace && la->end == co.start &&
                 (is_space || !undo->priv->last_char_is_space))
        {
            str = g_strconcat(la->text, co.text, NULL);
            la->end = co.end;
        }

        if (str)
        {
            g_free(la->text);
            la->text = str;
            update_tags_range(la->tags, la->start, la->end);
            undo->priv->last_char_is_space = is_space;
            la->mergeable = mergeable;
            return;
        }
        else
            la->mergeable = FALSE;
    }

    op = g_new(WPUndoOperation, 1);
    *op = co;

    op->type = WP_UNDO_DELETE;
    op->mergeable = mergeable;
    op->tags = wp_undo_get_toggled_tags(undo, start, end);
    undo->priv->last_char_is_space = is_space;

    wp_undo_add_queue(undo, op);
}

void
wp_undo_apply_tag(WPUndo * undo,
                  const GtkTextIter * start,
                  const GtkTextIter * end, GtkTextTag * tag, gboolean enable)
{
    WPUndoOperation *op;
    gint is, ie;

    g_return_if_fail(WP_IS_UNDO(undo));

    if (undo->priv->undo_disabled > 0 || undo->priv->low_mem)
        return;

    if (undo->priv->current_op && tag)
    {
        op = undo->priv->current_op;

        switch (op->type)
        {
            case WP_UNDO_INSERT:
                op->tags = g_slist_append(op->tags,
                                          wp_undo_create_tag
                                          (gtk_text_iter_get_offset(start),
                                           (GtkTextIter *) end, tag, enable));
                break;
            case WP_UNDO_TAG:
                is = gtk_text_iter_get_offset(start);
                ie = gtk_text_iter_get_offset(end);
                if (is >= op->start && ie <= op->end)
                {
                    op->tags = g_slist_prepend(op->tags,
                                               wp_undo_create_tag(is, end,
                                                                  tag,
                                                                  enable));
                }
                break;
            case WP_UNDO_FMT:
                is = gtk_text_iter_get_offset(start);
                ie = gtk_text_iter_get_offset(end);
                op->tags = g_slist_prepend(op->tags,
                                           wp_undo_create_tag(is, end, tag,
                                                              enable));
                break;
            default:
                g_return_if_fail(FALSE);
        }
    }
    else if (!tag)
    {
        op = g_new0(WPUndoOperation, 1);
        if (!op)
        {
            emit_no_memory(undo);
            return;
        }
        op->type = WP_UNDO_TAG;

        op->orig_tags = wp_undo_get_toggled_tags(undo, start, end);
        op->start = gtk_text_iter_get_offset(start);
        op->end = gtk_text_iter_get_offset(end);
#if 0
       /* 	commented as it is a dead code  */
        if (tag)
            op->tags = g_slist_prepend(NULL,
                                       wp_undo_create_tag(op->start,
                                                          end, tag, enable));
        else
#endif 
            op->tags = NULL;

        op->mergeable = FALSE;

        wp_undo_add_queue(undo, op);
    }
}

void
wp_undo_simple_justification(WPUndo * undo, GtkTextIter * start,
                             GtkTextIter * end, GtkTextTag * orig_tag,
                             GtkTextTag * tag)
{
    WPUndoOperation *op;

    if (undo->priv->undo_disabled > 0 || undo->priv->low_mem)
        return;

    op = g_new0(WPUndoOperation, 1);

    if (!op)
    {
        emit_no_memory(undo);
        return;
    }
    op->type = WP_UNDO_SIMPLE_JUSTIFY;

    op->orig_tag = orig_tag;
    op->tag = tag;
    op->start = gtk_text_iter_get_offset(start);
    op->end = gtk_text_iter_get_offset(end);

    op->mergeable = FALSE;

    wp_undo_add_queue(undo, op);
}

void
wp_undo_selection_changed(WPUndo * undo, GtkTextIter * start,
                          GtkTextIter * end)
{
    WPUndoOperation *op;
    gint istart, iend;

    g_return_if_fail(WP_IS_UNDO(undo));

    if (undo->priv->undo_disabled > 0 || undo->priv->low_mem)
        return;

    istart = gtk_text_iter_get_offset(start);
    iend = gtk_text_iter_get_offset(end);
    op = undo->priv->current_op;

    if (op && op->type == WP_UNDO_SELECT)
    {
        if ((op->sel_start == istart || op->sel_end == iend) && op->mergeable)
        {
            op->sel_start = istart;
            op->sel_end = iend;
            return;
        }
        else if (op)
            op->mergeable = FALSE;
    }

    if (istart != iend)
    {
        op = g_new0(WPUndoOperation, 1);

        if (!op)
        {
            emit_no_memory(undo);
            return;
        }
        op->type = WP_UNDO_SELECT;

        op->sel_start = istart;
        op->sel_end = iend;

        op->mergeable = TRUE;

        wp_undo_add_queue(undo, op);
    }
}

void
wp_undo_format_changed(WPUndo * undo, gboolean rich_text)
{
    WPUndoOperation *op;
    GtkTextIter start, end;

    g_return_if_fail(WP_IS_UNDO(undo));

    if (undo->priv->undo_disabled > 0 || undo->priv->low_mem)
        return;

    op = g_new0(WPUndoOperation, 1);

    if (!op)
    {
        emit_no_memory(undo);
        return;
    }
    op->type = WP_UNDO_FMT;

    op->rich_text = rich_text;

    gtk_text_buffer_get_bounds(undo->priv->text_buffer, &start, &end);
    op->tags = wp_undo_get_toggled_tags(undo, &start, &end);

    wp_undo_add_queue(undo, op);
}

void
wp_undo_last_line_justify(WPUndo * undo, gint old_line_justify,
                          gint new_line_justify)
{
    WPUndoOperation *op;

    g_return_if_fail(WP_IS_UNDO(undo));

    if (undo->priv->undo_disabled > 0 || undo->priv->low_mem)
        return;

    op = g_new0(WPUndoOperation, 1);

    if (!op)
    {
        emit_no_memory(undo);
        return;
    }
    op->type = WP_UNDO_LAST_LINE_JUSTIFY;

    op->old_line_justify = old_line_justify;
    op->new_line_justify = new_line_justify;

    wp_undo_add_queue(undo, op);
}

void
remove_image_tags (gchar **string)
{
    GString *new_string;
    gchar *current;
    if ((string == NULL)||(*string == NULL))
       return;
    new_string = g_string_new ("");
    current = *string;
    while (*current != '\0') {
       gunichar c = g_utf8_get_char (current);
       gchar output[6];
       if (c == 0xFFFC) {
           new_string = g_string_append_c (new_string, ' ');
       } else {
	   gint output_size;
	   output_size = g_unichar_to_utf8 (c, output);
	   new_string = g_string_append_len (new_string, output, output_size);
       }
       current = g_utf8_next_char (current);
    }
    g_free (*string);
    *string = g_string_free (new_string, FALSE);
}


static void
wp_undo_add_queue(WPUndo * undo, WPUndoOperation * op)
{
    GSList *tmp;
    WPUndoPrivate *priv = undo->priv;
    gboolean first_in_group = priv->first_in_group;

    /* first we remove all image text tags */
    remove_image_tags (&(op->text));

    if (priv->disable_this_group)
    {
    	
	/*bug 140583*/
	g_free(op->text);
        g_free(op);
        return;
    }

    if (!priv->group || priv->first_in_group)
    {
        priv->first_in_group = FALSE;
        wp_undo_reset_mergeable(undo);
    }

    priv->current_op_list = g_slist_prepend(priv->current_op_list, op);
    if (!priv->current_op)
        priv->undo_queue = g_slist_prepend(priv->undo_queue,
                                           priv->current_op_list);
    else
        priv->undo_queue->data = priv->current_op_list;

    priv->current_op = op;

    priv->redo_queue = wp_undo_free_op_list(priv->redo_queue);

    if (first_in_group && ++priv->undo_queue_len > priv->max_undo_level)
    {
        /* printf("Undo queue len: %d, %d\n", undo->priv->undo_queue_len,
         * undo->priv->max_undo_level); */
        undo->priv->undo_queue_len = undo->priv->max_undo_level;
        tmp =
            g_slist_nth(undo->priv->undo_queue,
                        undo->priv->undo_queue_len - 1);
        if (tmp)
            tmp->next = wp_undo_free_op_list(tmp->next);
    }

    wp_undo_send_signals(undo);
}

void
wp_undo_reset(WPUndo * undo)
{
    g_return_if_fail(WP_IS_UNDO(undo));
    
    wp_undo_reset_mergeable (undo);
    
    undo->priv->undo_queue = wp_undo_free_op_list(undo->priv->undo_queue);
    undo->priv->redo_queue = wp_undo_free_op_list(undo->priv->redo_queue);

    wp_undo_send_signals(undo);
}

static void
emit_no_memory(WPUndo * undo)
{
    WPUndoPrivate *priv = undo->priv;

    g_signal_emit(undo, signals[NO_MEMORY], 0);
    if (!priv->first_in_group && !priv->disable_this_group
        && priv->undo_queue)
    {
        GSList *tmp = priv->undo_queue;
        priv->undo_queue = tmp->next;
        tmp->next = NULL;
        wp_undo_free_op_list(tmp);

        wp_undo_send_signals(undo);
    }
    priv->disable_this_group = TRUE;
}
