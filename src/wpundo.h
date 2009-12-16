/**
 * @file wpundo.h
 *
 * Header file for undo/redo functionality
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

#ifndef _WP_UNDO_H
#define _WP_UNDO_H

#include <gtk/gtktextiter.h>

G_BEGIN_DECLS
#define WP_TYPE_UNDO              (wp_undo_get_type ())
#define WP_UNDO(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), WP_TYPE_UNDO, WPUndo))
#define WP_UNDO_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), WP_TYPE_UNDO, WPUndoClass))
#define WP_IS_UNDO(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), WP_TYPE_UNDO))
#define WP_IS_UNDO_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), WP_TYPE_UNDO))
#define WP_UNDO_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), WP_TYPE_UNDO, WPUndoClass))
typedef struct _WPUndoPrivate WPUndoPrivate;
typedef struct _WPUndo WPUndo;
typedef struct _WPUndoClass WPUndoClass;

/** Undo object */
struct _WPUndo {
    GObject base;

    WPUndoPrivate *priv;
};

/** Undo class */
struct _WPUndoClass {
    GObjectClass parent_class;

    /** 
     * Called when the undo state changed
     * @param undo pointer to the undo object
     * @param can_undo is enabled when there is something to undo 
     */
    void (*can_undo) (WPUndo * undo, gboolean can_undo);
    /** 
     * Called when the redo state changed
     * @param undo pointer to the undo object
     * @param can_redo is enabled when there is something to redo 
     */
    void (*can_redo) (WPUndo * undo, gboolean can_redo);
    /** 
     * Called when the formatting has changed from rich text to normal text or
     * viceversa 
     * @param undo pointer to the undo object
     * @param rich_text enabled when the buffer contains rich text
     */
    void (*fmt_changed) (WPUndo * undo, gboolean rich_text);
    /** 
     * Called when the last line justification changed 
     * @param undo pointer to the undo object
     * @param last_line_justification can be GTK_JUSTIFY_LEFT, 
     *        GTK_JUSTIFY_CENTER, GTK_JUSTIFY_RIGHT
     */
    void (*last_line_justify) (WPUndo * undo, gint last_line_justification);
    /**
     * Called when there is not enough memory to perform an operation 
     * @param undo pointer to the undo object
     */
    void (*no_memory) (WPUndo * undo);
};

GType
wp_undo_get_type(void)
    G_GNUC_CONST;

/**
 * Create a new undo object for the given buffer
 * @param buffer a gtk text buffer
 * @return the newly created undo object
 */
  WPUndo *wp_undo_new(GtkTextBuffer * buffer);
/**
 * Destroy the given undo object
 * @param undo pointer to the undo object
 */
  void wp_undo_free(WPUndo * undo);

/**
 * Querys the undo capability
 * @param undo pointer to the undo object
 * @return <b>TRUE</b> if the undo queue is not empty
 */
  gboolean wp_undo_can_undo(const WPUndo * undo);
/**
 * Querys the redo capability
 * @param undo pointer to the undo object
 * @return <b>TRUE</b> if the redo queue is not empty
 */
  gboolean wp_undo_can_redo(const WPUndo * undo);

/**
 * Undo the last operation
 * @param undo pointer to the undo object
 */
  void wp_undo_undo(WPUndo * undo);
/**
 * Redo the last operation
 * @param undo pointer to the undo object
 */
  void wp_undo_redo(WPUndo * undo);

/**
 * Freeze the undo buffer. Can be called several times.
 * In this time the undo queue is not registering new actions.
 * @param undo pointer to the undo object
 */
  void wp_undo_freeze(WPUndo * undo);
/**
 * Unfreeze the undo buffer. Can be called several times.
 * @param undo pointer to the undo object
 */
  void wp_undo_thaw(WPUndo * undo);
/**
 * Start a group undo operation. The group operations will be
 * handled as a single operation at undo/redo step.
 * @param undo pointer to the undo object
 */
  void wp_undo_start_group(WPUndo * undo);
/**
 * Ends a group undo operation.
 * @param undo pointer to the undo object
 */
  void wp_undo_end_group(WPUndo * undo);

/**
 * Register a new insert operation to the undo queue
 * @param undo pointer to the undo object
 * @param pos contains the position where the insert happended
 * @param text contains a pointer to the inserted text
 * @param length contains the length of the inserted text
 */
  void
   
      wp_undo_insert_text(WPUndo * undo,
                          GtkTextIter * pos, const gchar * text, gint length);
/**
 * Register a new delete operation to the undo queue
 * @param undo pointer to the undo object
 * @param start contains the start position of the delete
 * @param end contains the end position of the delete
 */
  void
   
      wp_undo_delete_range(WPUndo * undo,
                           GtkTextIter * start, GtkTextIter * end);
/**
 * Register a new tag change operation to the undo queue
 * @param undo pointer to the undo object
 * @param start contains the start position of the tag
 * @param end contains the end position of the tag
 * @param tag contains the tag which has been applied/removed
 * @param enable <b>TRUE</b> if the tag has been applied
 */
  void
   
      wp_undo_apply_tag(WPUndo * undo,
                        const GtkTextIter * start,
                        const GtkTextIter * end,
                        GtkTextTag * tag, gboolean enable);
/**
 * Register a new justification change operation to the undo queue.
 * This usually happens when a two lines were merged
 * @param undo pointer to the undo object
 * @param start contains the start position of the tag
 * @param end contains the end position of the tag
 * @param orig_tag contains the old justification tag
 * @param tag contains the new justification tag
 */
  void
   
      wp_undo_simple_justification(WPUndo * undo, GtkTextIter * start,
                                   GtkTextIter * end, GtkTextTag * orig_tag,
                                   GtkTextTag * tag);
/**
 * Register a new selection change operation to the undo queue.
 * @param undo pointer to the undo object
 * @param start contains the start position of the selection
 * @param end contains the end position of the selection
 */
  void
   
      wp_undo_selection_changed(WPUndo * undo, GtkTextIter * start,
                                GtkTextIter * end);

/**
 * Register a new format change operation to the undo queue.
 * @param undo pointer to the undo object
 * @param rich_text is <b>TRUE</b> if the new buffer contains rich text
 */
  void wp_undo_format_changed(WPUndo * undo, gboolean rich_text);

/**
 * Register a last line justification change operation to the undo queue.
 * @param undo pointer to the undo object
 * @param old_line_justify contains the old line justification
 * @param new_line_justify contains the new line justification
 */
  void wp_undo_last_line_justify(WPUndo * undo, gint old_line_justify,
                                 gint new_line_justify);

/**
 * Queries the undo enable state
 * @param undo pointer to the undo object
 * @return <b>TRUE</b> if undo is enabled
 */
  gboolean wp_undo_is_enabled(WPUndo * undo);

/**
 * Set the last undo operation as not mergeable
 * @param undo pointer to the undo object
 */
  void wp_undo_reset_mergeable(const WPUndo * undo);

/**
 * Clear the undo and redo queues
 * @param undo pointer to the undo object
 */
  void wp_undo_reset(WPUndo * undo);

G_END_DECLS
#endif /* _WP_UNDO_H */
