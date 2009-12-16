/**
 * @file wptextview.c
 *
 * Implementation file for WordPad Text View
 */

/* 
 * Osso Notes
 * Copyright (c) 2005-06 Nokia Corporation. All rights reserved.
 * Some parts based on: GTK - The GIMP Toolkit gtktextview.c 
 * Copyright (C) 2000 Red Hat, Inc.
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

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <gtk/gtkimcontext.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtklayout.h>
/* claim we really know what we are doing to be able to use the
 * gtk_text_layout_get_iter_at_pixel and gtk_text_layout_set_preedit_string
 * functions */
#define  GTK_TEXT_USE_INTERNAL_UNSUPPORTED_API
#include <gtk/gtktextlayout.h>

#include <config.h>

#include <string.h>

#include "wptextview.h"
#include "wptextbuffer.h"
#include "wptextbuffer-private.h"

/* Disable surrounding retrieval for speedup, currently non of the im methods 
 * use this. Uncomment it, if is needed in the future */
// #define DISABLE_SURROUNDING 1

static GObject *wp_text_view_constructor(GType type,
                                         guint n_construct_properties,
                                         GObjectConstructParam *
                                         construct_param);
static void wp_text_view_finalize(GObject * object);

/* static gboolean wp_text_view_expose(GtkWidget * widget, GdkEventExpose *
 * event); */

/**
 * Callback happening when the enter was pressed. Needed for bullets.
 * @param view is a #GtkTextView
 * @param event is a #GdkEventKey
 */
static gboolean handle_enter(WPTextView * view,
                             G_GNUC_UNUSED GdkEventKey * event);

/**
 * Callback happening at a key press
 * @param widget is a #GtkWidget
 * @param event is a #GdkEventKey
 */
static int wp_text_view_key_press_event(GtkWidget * widget,
                                        GdkEventKey * event);
/**
 * Callback happening when the cursor was moved. Needed to skip bullets.
 * @param text_view is a #GtkTextView
 * @param step is a #GtkMovementStep
 * @param count is the number of moves
 * @param extend_selection is set if the selection is extended
 */
static void wp_text_view_move_cursor(GtkTextView * text_view,
                                     GtkMovementStep step,
                                     gint count, gboolean extend_selection);
/**
 * Callback happening at the drag and drop motion.
 * @param widget is a #GtkWidget
 * @param context is a #GdkDragContext
 * @param x is the x coordinate of the motion
 * @param y is the y coordinate of the motion
 * @param time is the time of the event
 */
static gboolean wp_text_view_drag_motion(GtkWidget * widget,
                                         GdkDragContext * context,
                                         gint x, gint y, guint time);
/**
 * Callback happening at the drop phase of drag and drop
 * @param widget is a #GtkWidget
 * @param context is a #GdkDragContext
 * @param x is the x coordinate of the motion
 * @param y is the y coordinate of the motion
 * @param selection_data is a #GtkSelectionData
 * @param time is the time of the event
 */
static void wp_text_view_drag_data_received(GtkWidget * widget,
                                            GdkDragContext * context,
                                            gint x,
                                            gint y,
                                            GtkSelectionData * selection_data,
                                            guint info, guint time);
/**
 * Callback happening at the mouse button press.
 * @param widget is a #GtkWidget
 * @param event is a #GdkEventButton
 */
static gint wp_text_view_button_press_event(GtkWidget * widget,
                                            GdkEventButton * event);

/**
 * Callback happening when the backspace was pressed. Needed to delete bullets.
 * @param text_view is a #GtkTextView
 */
static void wp_text_view_backspace(GtkTextView * text_view);

/**
 * Callback happening when the delete was pressed. Needed to delete bullets.
 * @param text_view is a #GtkTextView
 * @param type is a #GtkDeleteType
 * @param count is the number of deletes
 */
static void wp_text_view_delete_from_cursor(GtkTextView * text_view,
                                            GtkDeleteType type, gint count);

/**
 * Callback happening when at the paste operation. Needed for bullets and
 * justification update.
 * @param text_view is a #GtkTextView
 */
static void wp_text_view_paste_clipboard(GtkTextView * text_view);

/**
 * Callback happening when the default font has been changed in the <i>buffer</i>
 * @param buffer is a #GtkTextBuffer
 * @param desc is a #PangoFontDescription containing the new font
 * @param view is a #GtkTextView
 */
static void wp_text_view_def_font_changed(WPTextBuffer * buffer,
                                          PangoFontDescription * desc,
                                          GtkWidget * view);

/**
 * Callback happening when the default justification has been changed in the <i>buffer</i>
 * @param buffer is a #GtkTextBuffer
 * @param justification is on of the #GTK_JUSTIFY_LEFT, #GTK_JUSTIFY_CENTER,
 *                      #GTK_JUSTIFY_RIGHT
 * @param text_view is a #GtkTextView
 */
static void wp_text_view_def_justification_changed(WPTextBuffer * buffer,
                                                   gint justification,
                                                   GtkTextView * text_view);

/**
 * Callback happening when the background color has been changed in the <i>buffer</i>
 * @param buffer is a #GtkTextBuffer
 * @param color is a #GdkColor
 * @param text_view is a #GtkTextView
 */
static void wp_text_view_background_color_changed(WPTextBuffer * buffer,
                                                  const GdkColor * color,
                                                  GtkTextView * text_view);


static void wp_text_view_commit_handler(GtkIMContext * context,
                                        const gchar * str,
                                        GtkTextView * text_view);
static void wp_text_view_commit_text(GtkTextView * text_view,
                                     const gchar * text);
static void wp_text_view_preedit_changed_handler(GtkIMContext * context,
                                                 GtkTextView * text_view);
static gboolean wp_text_view_retrieve_surrounding_handler(GtkIMContext *
                                                          context,
                                                          GtkTextView *
                                                          text_view);
static gboolean wp_text_view_delete_surrounding_handler(GtkIMContext *
                                                        context, gint offset,
                                                        gint n_chars,
                                                        GtkTextView *
                                                        text_view);
static gboolean wp_text_view_has_selection_handler(GtkIMContext * context,
                                                   GtkTextView * text_view);
#ifdef HAVE_HILDON
static void wp_text_view_clipboard_operation_handler(GtkIMContext * context,
                                                     GtkIMContextClipboardOperation
                                                     op,
                                                     GtkTextView * text_view);
#endif

/* WP_TYPE_TEXT_VIEW */
G_DEFINE_TYPE(WPTextView, wp_text_view, GTK_TYPE_TEXT_VIEW);

static void
wp_text_view_class_init(WPTextViewClass * klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    GtkTextViewClass *text_view_class = GTK_TEXT_VIEW_CLASS(klass);

    gobject_class->constructor = wp_text_view_constructor;
    gobject_class->finalize = wp_text_view_finalize;

    widget_class->key_press_event = wp_text_view_key_press_event;
    widget_class->drag_motion = wp_text_view_drag_motion;
    widget_class->drag_data_received = wp_text_view_drag_data_received;
    widget_class->button_press_event = wp_text_view_button_press_event;
    // widget_class->expose_event = wp_text_view_expose;

    text_view_class->move_cursor = wp_text_view_move_cursor;
    text_view_class->backspace = wp_text_view_backspace;
    text_view_class->delete_from_cursor = wp_text_view_delete_from_cursor;
    text_view_class->paste_clipboard = wp_text_view_paste_clipboard;
}


static void
wp_text_view_init(WPTextView * view)
{
    char *name;
    GtkTextView *text_view = GTK_TEXT_VIEW(view);

    name = g_strdup_printf("wp-text-view-%p", view);
    gtk_widget_set_name(GTK_WIDGET(view), name);
    g_free(name);

    g_object_unref(text_view->im_context);
    text_view->im_context = gtk_im_multicontext_new();

    g_signal_connect(text_view->im_context, "commit",
                     G_CALLBACK(wp_text_view_commit_handler), text_view);
    g_signal_connect(text_view->im_context, "preedit_changed",
                     G_CALLBACK(wp_text_view_preedit_changed_handler),
                     text_view);
    g_signal_connect(text_view->im_context, "retrieve_surrounding",
                     G_CALLBACK(wp_text_view_retrieve_surrounding_handler),
                     text_view);
    g_signal_connect(text_view->im_context, "delete_surrounding",
                     G_CALLBACK(wp_text_view_delete_surrounding_handler),
                     text_view);
    g_signal_connect(text_view->im_context, "has_selection",
                     G_CALLBACK(wp_text_view_has_selection_handler),
                     text_view);
#ifdef HAVE_HILDON
    g_signal_connect(text_view->im_context, "clipboard_operation",
                     G_CALLBACK(wp_text_view_clipboard_operation_handler),
                     text_view);
#endif
}


static GObject *
wp_text_view_constructor(GType type,
                         guint n_construct_properties,
                         GObjectConstructParam * construct_param)
{
    GObject *object;
    WPTextView *view;
    WPTextBuffer *buffer;

    object =
        G_OBJECT_CLASS(wp_text_view_parent_class)->constructor(type,
                                                               n_construct_properties,
                                                               construct_param);
    view = WP_TEXT_VIEW(object);
    buffer = wp_text_buffer_new(NULL);
    gtk_text_view_set_buffer(GTK_TEXT_VIEW(view), GTK_TEXT_BUFFER(buffer));
    g_object_unref(buffer);

    g_signal_connect(G_OBJECT(buffer), "def_font_changed",
                     G_CALLBACK(wp_text_view_def_font_changed), view);
    g_signal_connect(G_OBJECT(buffer), "def_justification_changed",
                     G_CALLBACK(wp_text_view_def_justification_changed),
                     view);
    g_signal_connect(G_OBJECT(buffer), "background_color_changed",
                     G_CALLBACK(wp_text_view_background_color_changed), view);

    return object;
}


static void
wp_text_view_finalize(GObject * object)
{
    G_OBJECT_CLASS(wp_text_view_parent_class)->finalize(object);
}


GtkWidget *
wp_text_view_new(void)
{
    GtkWidget *view = g_object_new(WP_TYPE_TEXT_VIEW, NULL);
    return view;
}


/********************************************************************/
/* Drawing and stuff */

/* static void draw_bullet_at_iter(GtkTextView * text_view, GdkEventExpose *
 * event, GtkTextIter * iter) { GdkRectangle rect; gint radius;
 * 
 * gtk_text_view_get_iter_location(text_view, iter, &rect);
 * gtk_text_view_buffer_to_window_coords(text_view, GTK_TEXT_WINDOW_TEXT,
 * rect.x, rect.y, &rect.x, &rect.y); radius = MIN(rect.width / 2,
 * rect.height / 2) / 3;
 * 
 * gdk_draw_arc(event->window,
 * GTK_WIDGET(text_view)->style->text_gc[GTK_STATE_NORMAL], TRUE, rect.x +
 * rect.width / 2 - radius, rect.y + rect.height / 2 - radius, radius * 2,
 * radius * 2, 0, 360 * 64); }
 * 
 * 
 * static void wp_text_view_draw_bullets(GtkTextView * text_view,
 * GdkEventExpose * event, const GtkTextIter * start, const GtkTextIter *
 * end) { GtkTextIter iter = *start; GtkTextTag *bullet =
 * _wp_text_buffer_get_bullet_tag(WP_TEXT_BUFFER (gtk_text_view_get_buffer
 * (text_view)));
 * 
 * if (!_wp_text_iter_is_bullet(&iter, bullet))
 * gtk_text_iter_forward_to_tag_toggle(&iter, bullet);
 * 
 * while (gtk_text_iter_compare(&iter, end) < 0) { // if
 * (gtk_text_iter_get_char (&iter) == '\t') draw_bullet_at_iter(text_view,
 * event, &iter); gtk_text_iter_forward_to_tag_toggle(&iter, bullet);
 * gtk_text_iter_forward_to_tag_toggle(&iter, bullet); } }
 * 
 * 
 * static gboolean wp_text_view_expose(GtkWidget * widget, GdkEventExpose *
 * event) { gboolean handled; GtkTextView *text_view = GTK_TEXT_VIEW(widget);
 * GtkTextIter start, end; int first_line = 0, last_line = 100000;
 * 
 * GdkRectangle rect = event->area;
 * 
 * gtk_text_view_window_to_buffer_coords (text_view, GTK_TEXT_WINDOW_TEXT,
 * rect.x, rect.y, &rect.x, &rect.y);
 * 
 * gtk_text_view_get_line_at_y (text_view, &start, rect.y, NULL);
 * gtk_text_view_get_line_at_y (text_view, &end, rect.y + rect.height, NULL);
 * gtk_text_iter_forward_line (&end);
 * 
 * first_line = gtk_text_iter_get_line (&start); last_line =
 * gtk_text_iter_get_line (&end);
 * 
 * handled = GTK_WIDGET_CLASS(wp_text_view_parent_class)->expose_event(widget,
 * event);
 * 
 * if (last_line - first_line < 2000) wp_text_view_draw_bullets (text_view,
 * event, &start, &end);
 * 
 * return handled; } */


/******************
 * Keyboard & mouse
 */

static int
wp_text_view_key_press_event(GtkWidget * widget, GdkEventKey * event)
{
    WPTextView *view;
    GtkTextView *text_view;
    GtkTextBuffer *buffer;
    gboolean handled = FALSE;
    int keyval = event->keyval;

    view = WP_TEXT_VIEW(widget);
    text_view = GTK_TEXT_VIEW(widget);
    buffer = gtk_text_view_get_buffer(text_view);

#ifdef HAVE_HILDON
    if (text_view->editable &&
        gtk_im_context_filter_keypress(text_view->im_context, event))
    {
        text_view->need_im_reset = TRUE;
        return TRUE;
    }
#endif
    // printf("<> Keypress: %d <>\n", keyval);

    if (!(event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK)))
    {
        switch (keyval)
        {
            case GDK_KP_Enter:
            case GDK_Return:
                handled = handle_enter(view, event);
                break;
        }
    }

    if (handled)
        return TRUE;

    // gtk_text_buffer_begin_user_action (buffer);
    handled =
        GTK_WIDGET_CLASS(wp_text_view_parent_class)->
        key_press_event(widget, event);
    // gtk_text_buffer_end_user_action (buffer);

    return handled;
}

static gboolean
handle_enter(WPTextView * view, G_GNUC_UNUSED GdkEventKey * event)
{
    GtkTextBuffer *buffer;
    GtkTextIter start, end, iter;
    gboolean has_selection, has_bullet, just_remove_bullet;
    GtkTextTag *bullet;

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    has_selection =
        gtk_text_buffer_get_selection_bounds(buffer, &start, &end);

    if (!gtk_text_iter_can_insert(&start, view->parent.editable))
        return FALSE;

    iter = start;
    bullet = _wp_text_buffer_get_bullet_tag(WP_TEXT_BUFFER(buffer));

    has_bullet = _wp_text_iter_has_bullet(&iter, bullet) &&
        !gtk_text_iter_begins_tag(&start, bullet);
    just_remove_bullet = gtk_text_iter_ends_tag(&start, bullet) &&
        gtk_text_iter_ends_line(&start);

    gtk_text_buffer_begin_user_action(buffer);
    if (has_selection)
        gtk_text_buffer_delete(buffer, &start, &end);

    if (just_remove_bullet)
        _wp_text_iter_remove_bullet_line(&start, bullet);
    else
    {
        gtk_text_buffer_insert(buffer, &start, "\n", 1);
        if (has_bullet)
            _wp_text_iter_put_bullet_line(&start, bullet);
    }

    gtk_text_buffer_end_user_action(buffer);

    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(view),
                                       gtk_text_buffer_get_insert(buffer));
    return TRUE;
}


static void
wp_text_view_move_cursor(GtkTextView * text_view,
                         GtkMovementStep step,
                         gint count, gboolean extend_selection)
{
    GtkTextBuffer *buffer;
    GtkTextMark *insert;
    GtkTextIter iter;
    GtkTextTag *bullet;

    GTK_TEXT_VIEW_CLASS(wp_text_view_parent_class)->move_cursor(text_view,
                                                                step,
                                                                count,
                                                                extend_selection);

    if (!text_view->cursor_visible)
        return;

    buffer = gtk_text_view_get_buffer(text_view);
    bullet = _wp_text_buffer_get_bullet_tag(WP_TEXT_BUFFER(buffer));
    insert = gtk_text_buffer_get_insert(buffer);
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, insert);

    if (                        /* gtk_text_iter_begins_tag(&iter, bullet) || 
                                 */
           _wp_text_iter_is_bullet(&iter, bullet)   // &&
           /* !(gtk_text_iter_ends_tag(&iter, bullet)) */ )
    {
        if (count < 0 && (step == GTK_MOVEMENT_LOGICAL_POSITIONS ||
                          step == GTK_MOVEMENT_VISUAL_POSITIONS ||
                          step == GTK_MOVEMENT_WORDS))
        {
            _wp_text_iter_skip_bullet(&iter, bullet, FALSE);
            if (gtk_text_iter_is_start(&iter))
                _wp_text_iter_skip_bullet(&iter, bullet, TRUE);
            else
                gtk_text_iter_backward_char(&iter);
        }
        else
        {
            _wp_text_iter_skip_bullet(&iter, bullet, TRUE);
        }

        if (extend_selection)
            gtk_text_buffer_move_mark(buffer, insert, &iter);
        else
            gtk_text_buffer_place_cursor(buffer, &iter);
    }
}


/* taken from gtk */

#define DND_SCROLL_MARGIN 0.20

static gint
drag_scan_timeout(gpointer data)
{
    GtkTextView *text_view = NULL;
    GtkTextBuffer *buffer = NULL;
    GtkTextTag *bullet = NULL;
    GdkWindow *wnd = NULL;
    GtkTextIter newplace;
    gint x, y;

    GDK_THREADS_ENTER();

    /* if (data != (gpointer)text_view) { */
    text_view = GTK_TEXT_VIEW(data);
    buffer = gtk_text_view_get_buffer(text_view);
    bullet = _wp_text_buffer_get_bullet_tag(WP_TEXT_BUFFER(buffer));
    // TODO: It is safe to use as static ?
    wnd = gtk_text_view_get_window(text_view, GTK_TEXT_WINDOW_TEXT);
    // }

    gdk_window_get_pointer(wnd, &x, &y, NULL);
    gtk_text_view_window_to_buffer_coords(text_view, GTK_TEXT_WINDOW_TEXT,
                                          x, y, &x, &y);
    gtk_text_layout_get_iter_at_pixel(text_view->layout, &newplace, x, y);

    if (_wp_text_iter_is_bullet(&newplace, bullet))
        _wp_text_iter_skip_bullet(&newplace, bullet, TRUE);

    gtk_text_buffer_move_mark(buffer, text_view->dnd_mark, &newplace);

    gtk_text_view_scroll_to_mark(text_view,
                                 text_view->dnd_mark,
                                 DND_SCROLL_MARGIN, FALSE, 0.0, 0.0);

    GDK_THREADS_LEAVE();

    return TRUE;
}


static gboolean
wp_text_view_drag_motion(GtkWidget * widget,
                         GdkDragContext * context, gint x, gint y, guint time)
{
    gboolean handled = GTK_WIDGET_CLASS(wp_text_view_parent_class)->
        drag_motion(widget, context, x, y, time);
    GtkTextView *text_view = GTK_TEXT_VIEW(widget);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter iter;
    GtkTextTag *bullet;

    gtk_text_buffer_begin_user_action(buffer);
    if (handled)
    {
        bullet = _wp_text_buffer_get_bullet_tag(WP_TEXT_BUFFER(buffer));
        gtk_text_buffer_get_iter_at_mark(buffer, &iter, text_view->dnd_mark);
        if (_wp_text_iter_is_bullet(&iter, bullet))
        {
            _wp_text_iter_skip_bullet(&iter, bullet, TRUE);
            gtk_text_buffer_move_mark(buffer, text_view->dnd_mark, &iter);

            /* Remove the timeout installed by gtk, and install ours */
            if (text_view->scroll_timeout != 0)
                g_source_remove(text_view->scroll_timeout);

            text_view->scroll_timeout =
                g_timeout_add(50, drag_scan_timeout, text_view);
        }
    }
    gtk_text_buffer_end_user_action(buffer);

    return handled;
}


static void
wp_text_view_drag_data_received(GtkWidget * widget,
                                GdkDragContext * context,
                                gint x,
                                gint y,
                                GtkSelectionData * selection_data,
                                guint info, guint time)
{
    GtkTextView *text_view = GTK_TEXT_VIEW(widget);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextTag *bullet = NULL;
    GtkTextIter start, iter;
    gboolean has_bullet = FALSE, adjust_justification;
    gint len = 0;

    if (!text_view->dnd_mark)
        return;

    adjust_justification =
        (selection_data->target ==
         gdk_atom_intern("GTK_TEXT_BUFFER_CONTENTS", FALSE));
    gtk_text_buffer_get_iter_at_mark(buffer, &start, text_view->dnd_mark);
    if (gtk_text_iter_can_insert(&start, text_view->editable))
    {
        /* need to add bullet, if the droped line had bullet */
        bullet = _wp_text_buffer_get_bullet_tag(WP_TEXT_BUFFER(buffer));
        has_bullet = _wp_text_iter_has_bullet(&start, bullet);
    }
    else
        return;

    gtk_text_buffer_begin_user_action(buffer);

    if (adjust_justification)
    {
        gtk_text_buffer_get_selection_bounds(buffer, &start, &iter);
        len = gtk_text_iter_get_offset(&iter) -
            gtk_text_iter_get_offset(&start);
        wp_text_buffer_freeze(WP_TEXT_BUFFER(buffer));
    }

    GTK_WIDGET_CLASS(wp_text_view_parent_class)->
        drag_data_received(widget, context, x, y, selection_data, info, time);
    if (adjust_justification)
        wp_text_buffer_thaw(WP_TEXT_BUFFER(buffer));

    if (adjust_justification)
    {
        gtk_text_buffer_get_iter_at_mark(buffer, &iter,
                                         gtk_text_buffer_get_insert(buffer));
        start = iter;
        gtk_text_iter_backward_chars(&start, len);
        /* printf("Dnd: %d - %d\n", gtk_text_iter_get_offset(&start),
         * gtk_text_iter_get_offset(&iter)); */
        if (gtk_text_iter_get_line(&start) != gtk_text_iter_get_line(&iter))
        {
            if (!gtk_text_iter_starts_line(&start))
                _wp_text_buffer_adjust_justification(WP_TEXT_BUFFER(buffer),
                                                     &start, NULL, NULL,
                                                     FALSE);
            if (!gtk_text_iter_ends_line(&iter))
                _wp_text_buffer_adjust_justification(WP_TEXT_BUFFER(buffer),
                                                     NULL, &iter, NULL,
                                                     FALSE);
        }
        else
            _wp_text_buffer_adjust_justification(WP_TEXT_BUFFER(buffer),
                                                 &start, &iter, NULL, FALSE);
    }

    if (bullet)
    {
        gtk_text_buffer_get_iter_at_mark(buffer, &iter,
                                         gtk_text_buffer_get_insert(buffer));
        if (has_bullet)
            _wp_text_iter_put_bullet_line(&iter, bullet);
        else
        {
            if (!gtk_text_iter_ends_line(&iter))
                _wp_text_iter_remove_bullet_line(&iter, bullet);
        }
    }
    gtk_text_buffer_end_user_action(buffer);
}

static inline void
get_mouse_coords(GtkTextView * text_view, gint * x, gint * y)
{
    GdkModifierType state;

    gdk_window_get_pointer(gtk_text_view_get_window
                           (text_view, GTK_TEXT_WINDOW_TEXT), x, y, &state);

    gtk_text_view_window_to_buffer_coords(text_view,
                                          GTK_TEXT_WINDOW_TEXT, *x, *y, x, y);
}

/* Took from gtk and modified a little because we don't want to select
 * bullets */
static void
move_mark_to_pointer_and_scroll(GtkTextView * text_view,
                                const gchar * mark_name, GtkTextTag * bullet)
{
    gint x, y;
    GtkTextIter newplace;

    get_mouse_coords(text_view, &x, &y);

    gtk_text_layout_get_iter_at_pixel(text_view->layout, &newplace, x, y);
    if (_wp_text_iter_is_bullet(&newplace, bullet))
        _wp_text_iter_skip_bullet(&newplace, bullet, TRUE);

    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);

        GtkTextMark *mark = gtk_text_buffer_get_mark(buffer, mark_name);

        /* This may invalidate the layout */
        gtk_text_buffer_move_mark(buffer, mark, &newplace);

        gtk_text_view_scroll_mark_onscreen(text_view, mark);
    }
}


static gint
selection_scan_timeout(gpointer data)
{
    GtkTextView *text_view;

    GDK_THREADS_ENTER();

    text_view = GTK_TEXT_VIEW(data);

    gtk_text_view_scroll_mark_onscreen(text_view,
                                       gtk_text_buffer_get_mark
                                       (gtk_text_view_get_buffer
                                        (text_view), "insert"));

    GDK_THREADS_LEAVE();

    return TRUE;                /* remain installed. */
}


typedef enum {
    SELECT_CHARACTERS,
    SELECT_WORDS,
    SELECT_LINES
} SelectionGranularity;


/* 
 * Move @start and @end to the boundaries of the selection unit (indicated by 
 * @granularity) which contained @start initially. Return wether @start was
 * contained in a selection unit at all (which may not be the case for words).
 */
static gboolean
extend_selection(GtkTextView * text_view,
                 SelectionGranularity granularity,
                 GtkTextIter * start, GtkTextIter * end, GtkTextTag * bullet)
{
    gboolean extend = TRUE;

    *end = *start;

    if (granularity == SELECT_WORDS)
    {
        if (gtk_text_iter_inside_word(start))
        {
            if (!gtk_text_iter_starts_word(start))
                gtk_text_iter_backward_visible_word_start(start);

            if (!gtk_text_iter_ends_word(end))
            {
                if (!gtk_text_iter_forward_visible_word_end(end))
                    gtk_text_iter_forward_to_end(end);
            }
        }
        else
            extend = FALSE;
    }
    else if (granularity == SELECT_LINES)
    {
        if (gtk_text_view_starts_display_line(text_view, start))
        {
            /* If on a display line boundary, we assume the user clicked off
             * the end of a line and we therefore select the line before the
             * boundary. */
            gtk_text_view_backward_display_line_start(text_view, start);
        }
        else
        {
            /* start isn't on the start of a line, so we move it to the
             * start, and move end to the end unless it's already there. */
            gtk_text_view_backward_display_line_start(text_view, start);

            if (!gtk_text_view_starts_display_line(text_view, end))
                gtk_text_view_forward_display_line_end(text_view, end);
        }

        if (_wp_text_iter_is_bullet(start, bullet))
            _wp_text_iter_skip_bullet(start, bullet, TRUE);
    }

    return extend;
}


static gint
selection_motion_event_handler(GtkTextView * text_view,
                               GdkEventMotion * event, gpointer data)
{
    SelectionGranularity granularity = GPOINTER_TO_INT(data);
    GtkTextBuffer *buffer;
    GtkTextTag *bullet;
    gint x, y;
    WPTextView *view = WP_TEXT_VIEW(text_view);

    if (event->is_hint)
        gdk_device_get_state(event->device, event->window, NULL, NULL);


    buffer = gtk_text_view_get_buffer(text_view);
    bullet = _wp_text_buffer_get_bullet_tag(WP_TEXT_BUFFER(buffer));

    get_mouse_coords(text_view, &x, &y);
#define MIN_MOVE 6
    if (abs(x - view->mx) < MIN_MOVE && abs(y - view->my) < MIN_MOVE)
        return TRUE;

    view->mx = x;
    view->my = y;
    if (granularity == SELECT_CHARACTERS)
    {
        move_mark_to_pointer_and_scroll(text_view, "insert", bullet);
    }
    else
    {
        GtkTextIter start, end;
        GtkTextIter old_start, old_end;
        GtkTextIter ins, bound;

        gtk_text_layout_get_iter_at_pixel(text_view->layout, &start, x, y);

        if (_wp_text_iter_is_bullet(&start, bullet))
            _wp_text_iter_skip_bullet(&start, bullet, TRUE);

        if (extend_selection(text_view, granularity, &start, &end, bullet))
        {
            /* Extend selection */
            gtk_text_buffer_get_iter_at_mark(buffer,
                                             &ins,
                                             gtk_text_buffer_get_insert
                                             (buffer));
            gtk_text_buffer_get_iter_at_mark(buffer, &bound,
                                             gtk_text_buffer_get_selection_bound
                                             (buffer));

            if (gtk_text_iter_compare(&ins, &bound) < 0)
            {
                old_start = ins;
                old_end = bound;
            }
            else
            {
                old_start = bound;
                old_end = ins;
            }

            if (gtk_text_iter_compare(&start, &old_start) < 0)
            {
                /* newly selected unit before the current selection */
                ins = start;
                bound = old_end;
            }
            else if (gtk_text_iter_compare(&old_end, &end) < 0)
            {
                /* newly selected unit after the current selection */
                ins = end;
                bound = old_start;
            }
            else if (gtk_text_iter_equal(&ins, &old_start))
            {
                /* newly selected unit inside the current selection at the
                 * start */
                if (!gtk_text_iter_equal(&ins, &start))
                    ins = end;
            }
            else
            {
                /* newly selected unit inside the current selection at the
                 * end */
                if (!gtk_text_iter_equal(&ins, &end))
                    ins = start;
            }

            gtk_text_buffer_select_range(buffer, &ins, &bound);
        }

        gtk_text_view_scroll_mark_onscreen(text_view,
                                           gtk_text_buffer_get_mark(buffer,
                                                                    "insert"));
    }

    /* If we had to scroll offscreen, insert a timeout to do so again. Note
     * that in the timeout, even if the mouse doesn't move, due to this
     * scroll xoffset/yoffset will have changed and we'll need to scroll
     * again. */
    if (text_view->scroll_timeout != 0) /* reset on every motion event */
        g_source_remove(text_view->scroll_timeout);

    text_view->scroll_timeout =
        g_timeout_add(50, selection_scan_timeout, text_view);

    return TRUE;
}


static void
gtk_text_view_start_selection_drag(GtkTextView * text_view,
                                   const GtkTextIter * iter,
                                   GdkEventButton * button)
{
    GtkTextIter start, end;
    GtkTextBuffer *buffer;
    GtkTextTag *bullet;
    WPTextView *view = WP_TEXT_VIEW(text_view);
    SelectionGranularity granularity;

    g_return_if_fail(text_view->selection_drag_handler == 0);

    get_mouse_coords(text_view, &view->mx, &view->my);

    if (button->type == GDK_2BUTTON_PRESS)
        granularity = SELECT_WORDS;
    else if (button->type == GDK_3BUTTON_PRESS)
        granularity = SELECT_LINES;
    else
        granularity = SELECT_CHARACTERS;

    gtk_grab_add(GTK_WIDGET(text_view));

    buffer = gtk_text_view_get_buffer(text_view);
    bullet = _wp_text_buffer_get_bullet_tag(WP_TEXT_BUFFER(buffer));

    start = *iter;
    if (_wp_text_iter_is_bullet(&start, bullet))
        _wp_text_iter_skip_bullet(&start, bullet, TRUE);

    extend_selection(text_view, granularity, &start, &end, bullet);

    if (button->state & GDK_SHIFT_MASK)
    {
        /* Extend selection */
        GtkTextIter old_start, old_end;

        gtk_text_buffer_get_selection_bounds(buffer, &old_start, &old_end);

        gtk_text_iter_order(&start, &old_start);
        gtk_text_iter_order(&old_end, &end);

        /* Now start is the first of the starts, and end is the last of the
         * ends */
    }

    gtk_text_buffer_select_range(buffer, &end, &start);

    text_view->selection_drag_handler = g_signal_connect(text_view,
                                                         "motion_notify_event",
                                                         G_CALLBACK
                                                         (selection_motion_event_handler),
                                                         GINT_TO_POINTER
                                                         (granularity));
}


/* returns whether we were really dragging */
static gboolean
gtk_text_view_end_selection_drag(GtkTextView * text_view,
                                 GdkEventButton * event)
{
    if (text_view->selection_drag_handler == 0)
        return FALSE;

    g_signal_handler_disconnect(text_view, text_view->selection_drag_handler);
    text_view->selection_drag_handler = 0;

    if (text_view->scroll_timeout != 0)
    {
        g_source_remove(text_view->scroll_timeout);
        text_view->scroll_timeout = 0;
    }

    gtk_grab_remove(GTK_WIDGET(text_view));

    return TRUE;
}


/* Took from the gtk with little modification, to not be able to select the
 * bullet */
static gint
wp_text_view_button_press_event(GtkWidget * widget, GdkEventButton * event)
{
    GtkTextView *text_view;

    text_view = GTK_TEXT_VIEW(widget);

    gtk_widget_grab_focus(widget);

#ifdef HAVE_HILDON
    if (text_view->editable &&
        hildon_gtk_im_context_filter_event(text_view->im_context,
                                           (GdkEvent *) event))
    {
        text_view->need_im_reset = TRUE;
        return TRUE;
    }
#endif

    // if (event->window != gtk_text_view_get_window(text_view,
    // GTK_TEXT_WINDOW_TEXT))
    if (gtk_text_view_get_window_type(text_view, event->window) !=
        GTK_TEXT_WINDOW_TEXT)
    {
        return GTK_WIDGET_CLASS(wp_text_view_parent_class)->
            button_press_event(widget, event);
    }

    if (event->type == GDK_BUTTON_PRESS)
    {
        gtk_im_context_reset(text_view->im_context);

        if (event->button == 1)
        {
            /* If we're in the selection, start a drag copy/move of the
             * selection; otherwise, start creating a new selection. */
            GtkTextIter iter;
            GtkTextIter start, end;
            gint x, y;

            gtk_text_view_window_to_buffer_coords(text_view,
                                                  GTK_TEXT_WINDOW_TEXT,
                                                  (int) event->x,
                                                  (int) event->y, &x, &y);

            gtk_text_layout_get_iter_at_pixel(text_view->layout, &iter, x, y);

            if (gtk_text_buffer_get_selection_bounds
                (gtk_text_view_get_buffer(text_view), &start, &end)
                && gtk_text_iter_in_range(&iter, &start, &end))
            {
                text_view->drag_start_x = event->x;
                text_view->drag_start_y = event->y;
                text_view->pending_place_cursor_button = event->button;
            }
            else
            {
                gtk_text_view_start_selection_drag(text_view, &iter, event);
            }

            return TRUE;
        }
    }
    else if ((event->type == GDK_2BUTTON_PRESS ||
              event->type == GDK_3BUTTON_PRESS) && event->button == 1)
    {
        GtkTextIter iter;
        gint x, y;

        gtk_text_view_end_selection_drag(text_view, event);

        gtk_text_view_window_to_buffer_coords(text_view,
                                              GTK_TEXT_WINDOW_TEXT,
                                              (int) event->x,
                                              (int) event->y, &x, &y);
        gtk_text_layout_get_iter_at_pixel(text_view->layout, &iter, x, y);

        gtk_text_view_start_selection_drag(text_view, &iter, event);
        return TRUE;
    }

    return FALSE;
}


static void
wp_text_view_backspace(GtkTextView * text_view)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter end;
    gboolean run_parent = TRUE;

    gtk_text_buffer_begin_user_action(buffer);
    if (!gtk_text_buffer_get_selection_bounds(buffer, &end, NULL))
    {
        GtkTextIter start, iter;
        GtkTextTag *bullet =
            _wp_text_buffer_get_bullet_tag(WP_TEXT_BUFFER(buffer));

        iter = end;
        if (gtk_text_iter_ends_tag(&end, bullet))
        {
            if (gtk_text_iter_backward_line(&iter) &&
                !_wp_text_iter_has_bullet(&iter, bullet))
                run_parent = FALSE;

            start = end;
            gtk_text_iter_backward_char(&start);
            _wp_text_iter_skip_bullet(&start, bullet, FALSE);
            gtk_text_buffer_delete(buffer, &start, &end);
        }
    }

    if (run_parent)
        GTK_TEXT_VIEW_CLASS(wp_text_view_parent_class)->backspace(text_view);

    gtk_text_buffer_end_user_action(buffer);
}


static void
wp_text_view_delete_from_cursor(GtkTextView * text_view,
                                GtkDeleteType type, gint count)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    gboolean had_selection =
        gtk_text_buffer_get_selection_bounds(buffer, NULL, NULL);

    gtk_text_buffer_begin_user_action(buffer);
    GTK_TEXT_VIEW_CLASS(wp_text_view_parent_class)->
        delete_from_cursor(text_view, type, count);
    if (!had_selection)
    {
        GtkTextIter start, end;
        GtkTextTag *bullet =
            _wp_text_buffer_get_bullet_tag(WP_TEXT_BUFFER(buffer));

        gtk_text_buffer_get_iter_at_mark(buffer, &start,
                                         gtk_text_buffer_get_insert(buffer));
        if (gtk_text_iter_begins_tag(&start, bullet))
        {
            end = start;
            _wp_text_iter_skip_bullet(&end, bullet, TRUE);
            gtk_text_buffer_delete(buffer, &start, &end);
        }
    }
    gtk_text_buffer_end_user_action(buffer);
}

/* TODO: maybe it would be better, if the WPTextView will use a separate
 * clipboard content than GtkTextBuffer. It is important for bullets. When
 * the bullets are on, and the clipboard is not containing a WPTextView
 * buffer, it should bulletize each line of the pasted text */
static void
wp_text_view_paste_clipboard(GtkTextView * text_view)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    /* GtkClipboard *clipboard = gtk_widget_get_clipboard (GTK_WIDGET
     * (text_view), GDK_SELECTION_CLIPBOARD); gboolean simple_text; */
    GtkTextTag *bullet = NULL;
    GtkTextIter start, iter;
    gint offset;
    gboolean has_bullet = FALSE;

    gtk_text_buffer_get_selection_bounds(buffer, &iter, NULL);
    offset = gtk_text_iter_get_offset(&iter);

    bullet = _wp_text_buffer_get_bullet_tag(WP_TEXT_BUFFER(buffer));
    has_bullet = _wp_text_iter_has_bullet(&iter, bullet);

    gtk_text_buffer_begin_user_action(buffer);
    /* simple_text = !gtk_clipboard_wait_is_target_available (clipboard,
     * gdk_atom_intern ("WP_TEXT_VIEW", FALSE)); */

    // printf("Paste begin\n");
    wp_text_buffer_freeze(WP_TEXT_BUFFER(buffer));
    GTK_TEXT_VIEW_CLASS(wp_text_view_parent_class)->
        paste_clipboard(text_view);
    wp_text_buffer_thaw(WP_TEXT_BUFFER(buffer));
    // printf("Paste end\n");

    gtk_text_buffer_get_iter_at_mark(buffer, &iter,
                                     gtk_text_buffer_get_insert(buffer));
    gtk_text_buffer_get_iter_at_offset(buffer, &start, offset);
    /* printf("Paste: %d - %d\n", gtk_text_iter_get_offset(&start),
     * gtk_text_iter_get_offset(&iter)); */
    if (gtk_text_iter_get_line(&start) != gtk_text_iter_get_line(&iter))
    {
        if (!gtk_text_iter_starts_line(&start))
            _wp_text_buffer_adjust_justification(WP_TEXT_BUFFER(buffer),
                                                 &start, NULL, NULL, FALSE);
        if (!gtk_text_iter_ends_line(&iter))
            _wp_text_buffer_adjust_justification(WP_TEXT_BUFFER(buffer),
                                                 NULL, &iter, NULL, FALSE);
    }
    else
        _wp_text_buffer_adjust_justification(WP_TEXT_BUFFER(buffer),
                                             &start, &iter, NULL, FALSE);

    if (bullet)
    {
        if (has_bullet)
            _wp_text_iter_put_bullet_line(&iter, bullet);
        else
        {
            if (!gtk_text_iter_ends_line(&iter))
                _wp_text_iter_remove_bullet_line(&iter, bullet);
        }
    }

    gtk_text_buffer_end_user_action(buffer);
}

static void
wp_text_view_def_font_changed(WPTextBuffer * buffer,
                              PangoFontDescription * desc, GtkWidget * view)
{
    gtk_widget_modify_font(view, desc);
}

static void
wp_text_view_background_color_changed(WPTextBuffer * buffer,
                                      const GdkColor * color,
                                      GtkTextView * text_view)
{
    gtk_widget_modify_base(GTK_WIDGET(text_view), GTK_STATE_NORMAL, color);
}

static void
wp_text_view_def_justification_changed(WPTextBuffer * buffer,
                                       gint justification,
                                       GtkTextView * text_view)
{
    gtk_text_view_set_justification(text_view, justification);
}

/* IM Handling - mostly taken from gtk */
static void
wp_text_view_commit_handler(GtkIMContext * context,
                            const gchar * str, GtkTextView * text_view)
{
    // printf("WP Commit text: %s\n", str);
    if (*str)
        wp_text_view_commit_text(text_view, str);

    if (WP_TEXT_VIEW(text_view)->in_action)
    {
        gtk_text_buffer_end_user_action(gtk_text_view_get_buffer(text_view));
        WP_TEXT_VIEW(text_view)->in_action = FALSE;
    }
}

static void
wp_text_view_commit_text(GtkTextView * text_view, const gchar * text)
{
    gboolean had_selection;

    gtk_text_buffer_begin_user_action(gtk_text_view_get_buffer(text_view));

    had_selection =
        gtk_text_buffer_get_selection_bounds(gtk_text_view_get_buffer
                                             (text_view), NULL, NULL);

    gtk_text_buffer_delete_selection(gtk_text_view_get_buffer(text_view),
                                     TRUE, text_view->editable);

    if (!strcmp(text, "\n"))
    {
        gtk_text_buffer_insert_interactive_at_cursor(gtk_text_view_get_buffer
                                                     (text_view), "\n", 1,
                                                     text_view->editable);
    }
    else
    {
        if (!had_selection && text_view->overwrite_mode)
        {
            GtkTextIter insert;

            gtk_text_buffer_get_iter_at_mark(gtk_text_view_get_buffer
                                             (text_view), &insert,
                                             gtk_text_buffer_get_mark
                                             (gtk_text_view_get_buffer
                                              (text_view), "insert"));
            if (!gtk_text_iter_ends_line(&insert))
                wp_text_view_delete_from_cursor(text_view, GTK_DELETE_CHARS,
                                                1);
        }
        gtk_text_buffer_insert_interactive_at_cursor(gtk_text_view_get_buffer
                                                     (text_view), text, -1,
                                                     text_view->editable);
    }

    gtk_text_buffer_end_user_action(gtk_text_view_get_buffer(text_view));

    gtk_text_view_scroll_mark_onscreen(text_view,
                                       gtk_text_buffer_get_mark
                                       (gtk_text_view_get_buffer(text_view),
                                        "insert"));
}

static void
wp_text_view_preedit_changed_handler(GtkIMContext * context,
                                     GtkTextView * text_view)
{
    gchar *str;
    PangoAttrList *attrs;
    gint cursor_pos;

    gtk_im_context_get_preedit_string(context, &str, &attrs, &cursor_pos);
    gtk_text_layout_set_preedit_string(text_view->layout, str, attrs,
                                       cursor_pos);
    pango_attr_list_unref(attrs);
    // printf("WP Preedit changed: %s\n", str);
    g_free(str);

    gtk_text_view_scroll_mark_onscreen(text_view,
                                       gtk_text_buffer_get_mark
                                       (gtk_text_view_get_buffer(text_view),
                                        "insert"));
}

static gboolean
whitespace(gunichar ch, gpointer user_data)
{
    return (ch == ' ' || ch == '\t');
}

static gboolean
not_whitespace_crlf(gunichar ch, gpointer user_data)
{
    return !whitespace(ch, user_data) && ch != '\r' && ch != '\n';
}

static gboolean
wp_text_view_retrieve_surrounding_handler(GtkIMContext * context,
                                          GtkTextView * text_view)
{
    GtkTextIter start;
    GtkTextIter end;
    GtkTextIter cursor;
    gint pos;
    gchar *text;
    gchar *text_between = NULL;

#ifdef DISABLE_SURROUNDING
    return FALSE;
#endif

    gtk_text_buffer_get_iter_at_mark(text_view->buffer, &cursor,
                                     gtk_text_buffer_get_insert(text_view->
                                                                buffer));
    end = start = cursor;

    gtk_text_iter_set_line_offset(&start, 0);
    gtk_text_iter_forward_to_line_end(&end);

    /* we want to include the previous non-whitespace character in the
     * surroundings. */
    if (gtk_text_iter_backward_char(&start))
        gtk_text_iter_backward_find_char(&start, not_whitespace_crlf, NULL,
                                         NULL);

    text_between = gtk_text_iter_get_slice(&start, &cursor);

    if (text_between != NULL)
        pos = strlen(text_between);
    else
        pos = 0;

    text = gtk_text_iter_get_slice(&start, &end);
    // printf("WP Surronding: %d, %s\n", pos, text);
    gtk_im_context_set_surrounding(context, text, -1, pos);
    g_free(text);
    g_free(text_between);

    return TRUE;
}

static gboolean
wp_text_view_delete_surrounding_handler(GtkIMContext * context,
                                        gint offset, gint n_chars,
                                        GtkTextView * text_view)
{
    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_begin_user_action(gtk_text_view_get_buffer(text_view));
    WP_TEXT_VIEW(text_view)->in_action = TRUE;
    gtk_text_buffer_get_iter_at_mark(text_view->buffer, &start,
                                     gtk_text_buffer_get_insert(text_view->
                                                                buffer));
    end = start;

    gtk_text_iter_forward_chars(&start, offset);
    gtk_text_iter_forward_chars(&end, offset + n_chars);

    /* printf("WP Delete surrounding: %d-%d\n",
     * gtk_text_iter_get_offset(&start), gtk_text_iter_get_offset(&end)); */
    wp_text_buffer_remember_tag(WP_TEXT_BUFFER(text_view->buffer), TRUE);
    gtk_text_buffer_delete_interactive(text_view->buffer, &start, &end,
                                       text_view->editable);
    wp_text_buffer_remember_tag(WP_TEXT_BUFFER(text_view->buffer), FALSE);

    return TRUE;
}

static gboolean
wp_text_view_has_selection_handler(GtkIMContext * context,
                                   GtkTextView * text_view)
{
    GtkTextBuffer *buffer;

    buffer = gtk_text_view_get_buffer(text_view);
    return gtk_text_buffer_get_selection_bounds(buffer, NULL, NULL);
}

#ifdef HAVE_HILDON
static void
wp_text_view_clipboard_operation_handler(GtkIMContext * context,
                                         GtkIMContextClipboardOperation op,
                                         GtkTextView * text_view)
{
    /* Similar to gtk_editable_*_clipboard(), handle these by sending signals
     * instead of directly calling our internal functions. That way the
     * application can hook into them if needed. */
    switch (op)
    {
        case GTK_IM_CONTEXT_CLIPBOARD_OP_COPY:
            g_signal_emit_by_name(text_view, "copy_clipboard");
            break;
        case GTK_IM_CONTEXT_CLIPBOARD_OP_CUT:
            g_signal_emit_by_name(text_view, "cut_clipboard");
            break;
        case GTK_IM_CONTEXT_CLIPBOARD_OP_PASTE:
            g_signal_emit_by_name(text_view, "paste_clipboard");
            break;
    }
}
#endif

void
wp_text_view_reset_and_show_im(WPTextView * view)
{
    GtkTextView *text_view = GTK_TEXT_VIEW(view);

    gtk_im_context_reset(text_view->im_context);
#ifdef HAVE_HILDON
    hildon_gtk_im_context_show(text_view->im_context);
#endif
}
