/**
 * @file wptextview.h
 *
 * Header file for WordPad Text View
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

#ifndef _WP_TEXT_VIEW_H
#define _WP_TEXT_VIEW_H

#include <gtk/gtktextview.h>

G_BEGIN_DECLS
#define WP_TYPE_TEXT_VIEW              (wp_text_view_get_type ())
#define WP_TEXT_VIEW(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), WP_TYPE_TEXT_VIEW, WPTextView))
#define WP_TEXT_VIEW_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), WP_TYPE_TEXT_VIEW, WPTextViewClass))
#define WP_IS_TEXT_VIEW(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), WP_TYPE_TEXT_VIEW))
#define WP_IS_TEXT_VIEW_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), WP_TYPE_TEXT_VIEW))
#define WP_TEXT_VIEW_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), WP_TYPE_TEXT_VIEW, WPTextViewClass))
typedef struct _WPTextView WPTextView;
typedef struct _WPTextViewPrivate WPTextViewPrivate;
typedef struct _WPTextViewClass WPTextViewClass;

/** WPTextView object */
struct _WPTextView {
    GtkTextView parent;

    gint mx, my;
    gboolean in_action;
};

/** WPTextView class */
struct _WPTextViewClass {
    GtkTextViewClass parent_class;
};


GType
wp_text_view_get_type(void)
    G_GNUC_CONST;

/**
 * Creates a new #WPTextView object
 * @return the newly created object
 */
  GtkWidget *wp_text_view_new(void);

  void wp_text_view_reset_and_show_im(WPTextView * view);

G_END_DECLS
#endif /* _WP_TEXT_VIEW_H */
