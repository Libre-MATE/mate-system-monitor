/*
 * Mate system monitor color pickers
 * Copyright (C) 2007 Karl Lattimer <karl@qdh.org.uk>
 * Copyright (C) 2012-2021 MATE Developers
 * All rights reserved.
 *
 * This Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with the software; see the file COPYING. If not,
 * write to the Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gsm_color_button.h"

#include <cairo.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <librsvg/rsvg.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
  GtkWidget *cc_dialog; /* Color chooser dialog */

  gchar *title; /* Title for the color selection window */

  GdkRGBA color;

  gdouble fraction; /* Only used by GSMCP_TYPE_PIE */
  guint type;
  cairo_surface_t *image_buffer;
  gdouble highlight;
  gboolean button_down;
  gboolean in_button;
} GSMColorButtonPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(GSMColorButton, gsm_color_button,
                           GTK_TYPE_DRAWING_AREA)

/* Properties */
enum { PROP_0, PROP_PERCENTAGE, PROP_TITLE, PROP_COLOR, PROP_TYPE };

/* Signals */
enum { COLOR_SET, LAST_SIGNAL };

#define GSMCP_MIN_WIDTH 15
#define GSMCP_MIN_HEIGHT 15

static void gsm_color_button_finalize(GObject *object);
static void gsm_color_button_set_property(GObject *object, guint param_id,
                                          const GValue *value,
                                          GParamSpec *pspec);
static void gsm_color_button_get_property(GObject *object, guint param_id,
                                          GValue *value, GParamSpec *pspec);
static void gsm_color_button_realize(GtkWidget *widget);
static void gsm_color_button_get_preferred_width(GtkWidget *widget,
                                                 gint *minimum_width,
                                                 gint *natural_width);
static void gsm_color_button_get_preferred_height(GtkWidget *widget,
                                                  gint *minimum_height,
                                                  gint *natural_height);
static void gsm_color_button_size_allocate(GtkWidget *widget,
                                           GtkAllocation *allocation);
static void gsm_color_button_unrealize(GtkWidget *widget);
static void gsm_color_button_state_changed(GtkWidget *widget,
                                           GtkStateType previous_state);
static void gsm_color_button_style_set(GtkWidget *widget,
                                       GtkStyle *previous_style);
static gint gsm_color_button_pressed(GtkWidget *widget, GdkEventButton *event);
static gint gsm_color_button_released(GtkWidget *widget, GdkEventButton *event);
static gboolean gsm_color_button_enter_notify(GtkWidget *widget,
                                              GdkEventCrossing *event);
static gboolean gsm_color_button_leave_notify(GtkWidget *widget,
                                              GdkEventCrossing *event);
/* source side drag signals */
static void gsm_color_button_drag_begin(GtkWidget *widget,
                                        GdkDragContext *context, gpointer data);
static void gsm_color_button_drag_data_get(GtkWidget *widget,
                                           GdkDragContext *context,
                                           GtkSelectionData *selection_data,
                                           guint info, guint time,
                                           GSMColorButton *color_button);

/* target side drag signals */
static void gsm_color_button_drag_data_received(
    GtkWidget *widget, GdkDragContext *context, gint x, gint y,
    GtkSelectionData *selection_data, guint info, guint32 time,
    GSMColorButton *color_button);

static guint color_button_signals[LAST_SIGNAL] = {0};

static const GtkTargetEntry drop_types[] = {{"application/x-color", 0, 0}};

static void gsm_color_button_class_init(GSMColorButtonClass *klass) {
  GObjectClass *gobject_class;
  GtkWidgetClass *widget_class;

  gobject_class = G_OBJECT_CLASS(klass);
  widget_class = GTK_WIDGET_CLASS(klass);

  gobject_class->get_property = gsm_color_button_get_property;
  gobject_class->set_property = gsm_color_button_set_property;
  gobject_class->finalize = gsm_color_button_finalize;
  widget_class->state_changed = gsm_color_button_state_changed;
  widget_class->get_preferred_width = gsm_color_button_get_preferred_width;
  widget_class->get_preferred_height = gsm_color_button_get_preferred_height;
  widget_class->size_allocate = gsm_color_button_size_allocate;
  widget_class->realize = gsm_color_button_realize;
  widget_class->unrealize = gsm_color_button_unrealize;
  widget_class->style_set = gsm_color_button_style_set;
  widget_class->button_release_event = gsm_color_button_released;
  widget_class->button_press_event = gsm_color_button_pressed;
  widget_class->enter_notify_event = gsm_color_button_enter_notify;
  widget_class->leave_notify_event = gsm_color_button_leave_notify;

  klass->color_set = NULL;

  g_object_class_install_property(
      gobject_class, PROP_PERCENTAGE,
      g_param_spec_double("fraction", _("Fraction"),
                          // TRANSLATORS: description of the pie color picker's
                          // (mem, swap) filled percentage property
                          _("Percentage full for pie color pickers"), 0, 1, 0.5,
                          G_PARAM_READWRITE));

  g_object_class_install_property(
      gobject_class, PROP_TITLE,
      g_param_spec_string("title", _("Title"),
                          _("The title of the color selection dialog"),
                          _("Pick a Color"), G_PARAM_READWRITE));

  g_object_class_install_property(
      gobject_class, PROP_COLOR,
      g_param_spec_boxed("color", _("Current Color"), _("The selected color"),
                         GDK_TYPE_RGBA, G_PARAM_READWRITE));

  g_object_class_install_property(
      gobject_class, PROP_TYPE,
      g_param_spec_uint("type", _("Type"), _("Type of color picker"), 0, 4, 0,
                        G_PARAM_READWRITE));

  color_button_signals[COLOR_SET] = g_signal_new(
      "color_set", G_TYPE_FROM_CLASS(gobject_class), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET(GSMColorButtonClass, color_set), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static cairo_surface_t *fill_image_buffer_from_file(cairo_t *cr,
                                                    const char *filePath) {
  GError *error = NULL;
  RsvgHandle *handle;
  cairo_surface_t *tmp_surface;
  cairo_t *tmp_cr;

  handle = rsvg_handle_new_from_file(filePath, &error);

  if (handle == NULL) {
    g_warning("rsvg_handle_new_from_file(\"%s\") failed: %s", filePath,
              (error ? error->message : "unknown error"));
    if (error) g_error_free(error);
    return NULL;
  }

  tmp_surface = cairo_surface_create_similar(cairo_get_target(cr),
                                             CAIRO_CONTENT_COLOR_ALPHA, 32, 32);
  tmp_cr = cairo_create(tmp_surface);
  rsvg_handle_render_cairo(handle, tmp_cr);
  cairo_destroy(tmp_cr);
  g_object_unref(handle);
  return tmp_surface;
}

static void render(GtkWidget *widget) {
  GSMColorButtonPrivate *priv;
  GSMColorButton *color_button = GSM_COLOR_BUTTON(widget);

  priv = gsm_color_button_get_instance_private(color_button);
  GdkRGBA *color;
  GdkRGBA tmp_color = priv->color;
  color = &tmp_color;
  cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(widget));
  cairo_path_t *path = NULL;
  gint width, height;
  gdouble radius, arc_start, arc_end;
  gdouble highlight_factor;

  if (priv->highlight > 0) {
    highlight_factor = 0.125 * priv->highlight;

    if (color->red + highlight_factor > 1.0)
      color->red = 1.0;
    else
      color->red = color->red + highlight_factor;

    if (color->blue + highlight_factor > 1.0)
      color->blue = 1.0;
    else
      color->blue = color->blue + highlight_factor;

    if (color->green + highlight_factor > 1.0)
      color->green = 1.0;
    else
      color->green = color->green + highlight_factor;
  }
  gdk_cairo_set_source_rgba(cr, color);

  width = gdk_window_get_width(gtk_widget_get_window(widget));
  height = gdk_window_get_height(gtk_widget_get_window(widget));

  switch (priv->type) {
    case GSMCP_TYPE_CPU:
      // gtk_widget_set_size_request (widget, GSMCP_MIN_WIDTH,
      // GSMCP_MIN_HEIGHT);
      cairo_paint(cr);
      cairo_set_line_width(cr, 1);
      cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
      cairo_rectangle(cr, 0.5, 0.5, width - 1, height - 1);
      cairo_stroke(cr);
      cairo_set_line_width(cr, 1);
      cairo_set_source_rgba(cr, 1, 1, 1, 0.4);
      cairo_rectangle(cr, 1.5, 1.5, width - 3, height - 3);
      cairo_stroke(cr);
      break;
    case GSMCP_TYPE_PIE:
      if (width < 32)  // 32px minimum size
        gtk_widget_set_size_request(widget, 32, 32);
      if (width < height)
        radius = width / 2;
      else
        radius = height / 2;

      arc_start = -G_PI_2 + 2 * G_PI * priv->fraction;
      arc_end = -G_PI_2;

      cairo_set_line_width(cr, 1);

      // Draw external stroke and fill
      if (priv->fraction < 0.01) {
        cairo_arc(cr, (width / 2) + .5, (height / 2) + .5, 4.5, 0, 2 * G_PI);
      } else if (priv->fraction > 0.99) {
        cairo_arc(cr, (width / 2) + .5, (height / 2) + .5, radius - 2.25, 0,
                  2 * G_PI);
      } else {
        cairo_arc_negative(cr, (width / 2) + .5, (height / 2) + .5,
                           radius - 2.25, arc_start, arc_end);
        cairo_arc_negative(cr, (width / 2) + .5, (height / 2) + .5, 4.5,
                           arc_end, arc_start);
        cairo_arc_negative(cr, (width / 2) + .5, (height / 2) + .5,
                           radius - 2.25, arc_start, arc_start);
      }
      cairo_fill_preserve(cr);
      cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
      cairo_stroke(cr);

      // Draw internal highlight
      cairo_set_source_rgba(cr, 1, 1, 1, 0.45);
      cairo_set_line_width(cr, 1);

      if (priv->fraction < 0.03) {
        cairo_arc(cr, (width / 2) + .5, (height / 2) + .5, 3.25, 0, 2 * G_PI);
      } else if (priv->fraction > 0.99) {
        cairo_arc(cr, (width / 2) + .5, (height / 2) + .5, radius - 3.5, 0,
                  2 * G_PI);
      } else {
        cairo_arc_negative(cr, (width / 2) + .5, (height / 2) + .5,
                           radius - 3.5, arc_start + (1 / (radius - 3.75)),
                           arc_end - (1 / (radius - 3.75)));
        cairo_arc_negative(cr, (width / 2) + .5, (height / 2) + .5, 3.25,
                           arc_end - (1 / (radius - 3.75)),
                           arc_start + (1 / (radius - 3.75)));
        cairo_arc_negative(cr, (width / 2) + .5, (height / 2) + .5,
                           radius - 3.5, arc_start + (1 / (radius - 3.75)),
                           arc_start + (1 / (radius - 3.75)));
      }
      cairo_stroke(cr);

      // Draw external shape
      cairo_set_line_width(cr, 1);
      cairo_set_source_rgba(cr, 0, 0, 0, 0.2);
      cairo_arc(cr, (width / 2) + .5, (height / 2) + .5, radius - 1.25, 0,
                G_PI * 2);
      cairo_stroke(cr);

      break;
    case GSMCP_TYPE_NETWORK_IN:
      if (priv->image_buffer == NULL)
        priv->image_buffer = fill_image_buffer_from_file(
            cr, DATADIR "/pixmaps/mate-system-monitor/download.svg");
      gtk_widget_set_size_request(widget, 32, 32);
      cairo_move_to(cr, 8.5, 1.5);
      cairo_line_to(cr, 23.5, 1.5);
      cairo_line_to(cr, 23.5, 11.5);
      cairo_line_to(cr, 29.5, 11.5);
      cairo_line_to(cr, 16.5, 27.5);
      cairo_line_to(cr, 15.5, 27.5);
      cairo_line_to(cr, 2.5, 11.5);
      cairo_line_to(cr, 8.5, 11.5);
      cairo_line_to(cr, 8.5, 1.5);
      cairo_close_path(cr);
      path = cairo_copy_path(cr);
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
      cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
      cairo_set_line_width(cr, 1);
      cairo_fill_preserve(cr);
      cairo_set_miter_limit(cr, 5.0);
      cairo_stroke(cr);
      cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
      cairo_append_path(cr, path);
      cairo_path_destroy(path);
      cairo_stroke(cr);
      cairo_set_source_surface(cr, priv->image_buffer, 0.0, 0.0);
      cairo_paint(cr);

      break;
    case GSMCP_TYPE_NETWORK_OUT:
      if (priv->image_buffer == NULL)
        priv->image_buffer = fill_image_buffer_from_file(
            cr, DATADIR "/pixmaps/mate-system-monitor/upload.svg");
      gtk_widget_set_size_request(widget, 32, 32);
      cairo_move_to(cr, 16.5, 1.5);
      cairo_line_to(cr, 29.5, 17.5);
      cairo_line_to(cr, 23.5, 17.5);
      cairo_line_to(cr, 23.5, 27.5);
      cairo_line_to(cr, 8.5, 27.5);
      cairo_line_to(cr, 8.5, 17.5);
      cairo_line_to(cr, 2.5, 17.5);
      cairo_line_to(cr, 15.5, 1.5);
      cairo_line_to(cr, 16.5, 1.5);
      cairo_close_path(cr);
      path = cairo_copy_path(cr);
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
      cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
      cairo_set_line_width(cr, 1);
      cairo_fill_preserve(cr);
      cairo_set_miter_limit(cr, 5.0);
      cairo_stroke(cr);
      cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
      cairo_append_path(cr, path);
      cairo_path_destroy(path);
      cairo_stroke(cr);
      cairo_set_source_surface(cr, priv->image_buffer, 0.0, 0.0);
      cairo_paint(cr);

      break;
  }
  cairo_destroy(cr);
}

/* Handle exposure events for the color picker's drawing area */
static gboolean draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  render(GTK_WIDGET(data));

  return FALSE;
}

static void gsm_color_button_realize(GtkWidget *widget) {
  GTK_WIDGET_CLASS(gsm_color_button_parent_class)->realize(widget);
  render(widget);
}

static void gsm_color_button_get_preferred_width(GtkWidget *widget,
                                                 gint *minimum_width,
                                                 gint *natural_width) {
  g_return_if_fail(widget != NULL && minimum_width != NULL &&
                   natural_width != NULL);
  g_return_if_fail(GSM_IS_COLOR_BUTTON(widget));

  *minimum_width = GSMCP_MIN_WIDTH;
  *natural_width = GSMCP_MIN_WIDTH;
}
static void gsm_color_button_get_preferred_height(GtkWidget *widget,
                                                  gint *minimum_height,
                                                  gint *natural_height) {
  g_return_if_fail(widget != NULL && minimum_height != NULL &&
                   natural_height != NULL);
  g_return_if_fail(GSM_IS_COLOR_BUTTON(widget));

  *minimum_height = GSMCP_MIN_HEIGHT;
  *natural_height = GSMCP_MIN_HEIGHT;
}

static void gsm_color_button_size_allocate(GtkWidget *widget,
                                           GtkAllocation *allocation) {
  g_return_if_fail(widget != NULL || allocation != NULL);
  g_return_if_fail(GSM_IS_COLOR_BUTTON(widget));

  gtk_widget_set_allocation(widget, allocation);

  if (gtk_widget_get_realized(widget)) {
    gdk_window_move_resize(gtk_widget_get_window(widget), allocation->x,
                           allocation->y, allocation->width,
                           allocation->height);
  }
}

static void gsm_color_button_unrealize(GtkWidget *widget) {
  GTK_WIDGET_CLASS(gsm_color_button_parent_class)->unrealize(widget);
}

static void gsm_color_button_style_set(GtkWidget *widget,
                                       GtkStyle *previous_style) {
  GTK_WIDGET_CLASS(gsm_color_button_parent_class)
      ->style_set(widget, previous_style);
}

static void gsm_color_button_state_changed(GtkWidget *widget,
                                           GtkStateType previous_state) {}

static void gsm_color_button_drag_data_received(
    GtkWidget *widget, GdkDragContext *context, gint x, gint y,
    GtkSelectionData *selection_data, guint info, guint32 time,
    GSMColorButton *color_button) {
  gint length;
  guint16 *dropped;
  GSMColorButtonPrivate *priv;

  priv = gsm_color_button_get_instance_private(color_button);

  length = gtk_selection_data_get_length(selection_data);

  if (length < 0) return;

  /* We accept drops with the wrong format, since the KDE color
   * chooser incorrectly drops application/x-color with format 8.
   */
  if (length != 8) {
    g_warning(_("Received invalid color data\n"));
    return;
  }

  dropped = (guint16 *)gtk_selection_data_get_data(selection_data);

  priv->color.red = ((double)dropped[0]) / 65535.0;
  priv->color.green = ((double)dropped[1]) / 65535.0;
  priv->color.blue = ((double)dropped[2]) / 65535.0;

  gtk_widget_queue_draw(GTK_WIDGET(color_button));

  g_signal_emit(color_button, color_button_signals[COLOR_SET], 0);

  g_object_freeze_notify(G_OBJECT(color_button));
  g_object_notify(G_OBJECT(color_button), "color");
  g_object_thaw_notify(G_OBJECT(color_button));
}

static void set_color_icon(GdkDragContext *context, GdkRGBA *color) {
  GdkPixbuf *pixbuf;
  guint32 pixel;

  pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 48, 32);

  pixel = ((guint32)(color->red * 0xff) << 24) |
          ((guint32)(color->green * 0xff) << 16) |
          ((guint32)(color->blue * 0xff) << 8);

  gdk_pixbuf_fill(pixbuf, pixel);

  gtk_drag_set_icon_pixbuf(context, pixbuf, -2, -2);
  g_object_unref(pixbuf);
}

static void gsm_color_button_drag_begin(GtkWidget *widget,
                                        GdkDragContext *context,
                                        gpointer data) {
  GSMColorButtonPrivate *priv;
  GSMColorButton *color_button = data;

  priv = gsm_color_button_get_instance_private(color_button);

  set_color_icon(context, &priv->color);
}

static void gsm_color_button_drag_data_get(GtkWidget *widget,
                                           GdkDragContext *context,
                                           GtkSelectionData *selection_data,
                                           guint info, guint time,
                                           GSMColorButton *color_button) {
  guint16 dropped[4];
  GSMColorButtonPrivate *priv;

  priv = gsm_color_button_get_instance_private(color_button);

  dropped[0] = (guint16)(65535.0 * priv->color.red);
  dropped[1] = (guint16)(65535.0 * priv->color.green);
  dropped[2] = (guint16)(65535.0 * priv->color.blue);
  dropped[3] = 65535;  // This widget doesn't care about alpha

  gtk_selection_data_set(selection_data,
                         gtk_selection_data_get_target(selection_data), 16,
                         (guchar *)dropped, 8);
}

static void gsm_color_button_init(GSMColorButton *color_button) {
  GSMColorButtonPrivate *priv;

  priv = gsm_color_button_get_instance_private(color_button);

  priv->color.red = 0;
  priv->color.green = 0;
  priv->color.blue = 0;
  priv->fraction = 0.5;
  priv->type = GSMCP_TYPE_CPU;
  priv->image_buffer = NULL;
  priv->title = g_strdup(_("Pick a Color")); /* default title */
  priv->in_button = FALSE;
  priv->button_down = FALSE;

  gtk_drag_dest_set(GTK_WIDGET(color_button),
                    GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT |
                        GTK_DEST_DEFAULT_DROP,
                    drop_types, 1, GDK_ACTION_COPY);
  gtk_drag_source_set(GTK_WIDGET(color_button),
                      GDK_BUTTON1_MASK | GDK_BUTTON3_MASK, drop_types, 1,
                      GDK_ACTION_COPY);
  g_signal_connect(color_button, "drag_begin",
                   G_CALLBACK(gsm_color_button_drag_begin), color_button);
  g_signal_connect(color_button, "drag_data_received",
                   G_CALLBACK(gsm_color_button_drag_data_received),
                   color_button);
  g_signal_connect(color_button, "drag_data_get",
                   G_CALLBACK(gsm_color_button_drag_data_get), color_button);

  gtk_widget_add_events(GTK_WIDGET(color_button),
                        GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);

  gtk_widget_set_tooltip_text(GTK_WIDGET(color_button),
                              _("Click to set graph colors"));

  g_signal_connect(color_button, "draw", G_CALLBACK(draw), color_button);
}

static void gsm_color_button_finalize(GObject *object) {
  GSMColorButtonPrivate *priv;
  GSMColorButton *color_button = GSM_COLOR_BUTTON(object);

  priv = gsm_color_button_get_instance_private(color_button);

  if (priv->cc_dialog != NULL) gtk_widget_destroy(priv->cc_dialog);
  priv->cc_dialog = NULL;

  g_free(priv->title);
  priv->title = NULL;

  cairo_surface_destroy(priv->image_buffer);
  priv->image_buffer = NULL;

  G_OBJECT_CLASS(gsm_color_button_parent_class)->finalize(object);
}

GtkWidget *gsm_color_button_new(const GdkRGBA *color, guint type) {
  return g_object_new(GSM_TYPE_COLOR_BUTTON, "color", color, "type", type,
                      NULL);
}

static void dialog_response(GtkWidget *widget, GtkResponseType response,
                            gpointer data) {
  GSMColorButton *color_button = GSM_COLOR_BUTTON(data);
  GSMColorButtonPrivate *priv = gsm_color_button_get_instance_private(color_button);

  if (response == GTK_RESPONSE_OK) {
    GtkColorChooser *color_chooser = GTK_COLOR_CHOOSER(priv->cc_dialog);

    gtk_color_chooser_get_rgba(color_chooser, &priv->color);

    gtk_widget_hide(priv->cc_dialog);

    gtk_widget_queue_draw(GTK_WIDGET(color_button));

    g_signal_emit(color_button, color_button_signals[COLOR_SET], 0);

    g_object_freeze_notify(G_OBJECT(color_button));
    g_object_notify(G_OBJECT(color_button), "color");
    g_object_thaw_notify(G_OBJECT(color_button));
  } else /* (response == GTK_RESPONSE_CANCEL) */
    gtk_widget_hide(priv->cc_dialog);
}

static gboolean dialog_destroy(GtkWidget *widget, gpointer data) {
  GSMColorButtonPrivate *priv;
  GSMColorButton *color_button = GSM_COLOR_BUTTON(data);

  priv = gsm_color_button_get_instance_private(color_button);

  priv->cc_dialog = NULL;

  return FALSE;
}

static gint gsm_color_button_clicked(GtkWidget *widget, GdkEventButton *event) {
  GSMColorButtonPrivate *priv;
  GSMColorButton *color_button = GSM_COLOR_BUTTON(widget);

  priv = gsm_color_button_get_instance_private(color_button);

  /* if dialog already exists, make sure it's shown and raised */
  if (!priv->cc_dialog) {
    /* Create the dialog and connects its buttons */
    GtkWidget *cc_dialog;
    GtkWidget *parent;

    parent = gtk_widget_get_toplevel(GTK_WIDGET(color_button));
    if (!gtk_widget_is_toplevel(parent)) parent = NULL;

    cc_dialog = gtk_color_chooser_dialog_new(priv->title, GTK_WINDOW(parent));

    gtk_window_set_modal(GTK_WINDOW(cc_dialog), TRUE);

    g_signal_connect(cc_dialog, "response", G_CALLBACK(dialog_response),
                     color_button);

    g_signal_connect(cc_dialog, "destroy", G_CALLBACK(dialog_destroy),
                     color_button);

    priv->cc_dialog = cc_dialog;
  }

  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(priv->cc_dialog), &priv->color);

  gtk_window_present(GTK_WINDOW(priv->cc_dialog));

  return 0;
}

static gint gsm_color_button_pressed(GtkWidget *widget, GdkEventButton *event) {
  if ((event->type == GDK_BUTTON_PRESS) && (event->button == 1)) {
    GSMColorButtonPrivate *priv;
    GSMColorButton *color_button = GSM_COLOR_BUTTON(widget);
    priv = gsm_color_button_get_instance_private(color_button);
    priv->button_down = TRUE;
  }
  return 0;
}

static gint gsm_color_button_released(GtkWidget *widget,
                                      GdkEventButton *event) {
  GSMColorButtonPrivate *priv;
  GSMColorButton *color_button = GSM_COLOR_BUTTON(widget);

  priv = gsm_color_button_get_instance_private(color_button);
  if (priv->button_down && priv->in_button)
    gsm_color_button_clicked(widget, event);
  priv->button_down = FALSE;
  return 0;
}

static gboolean gsm_color_button_enter_notify(GtkWidget *widget,
                                              GdkEventCrossing *event) {
  GSMColorButtonPrivate *priv;
  GSMColorButton *color_button = GSM_COLOR_BUTTON(widget);

  priv = gsm_color_button_get_instance_private(color_button);
  priv->highlight = 1.0;
  priv->in_button = TRUE;
  gtk_widget_queue_draw(widget);
  return FALSE;
}

static gboolean gsm_color_button_leave_notify(GtkWidget *widget,
                                              GdkEventCrossing *event) {
  GSMColorButton *color_button = GSM_COLOR_BUTTON(widget);
  GSMColorButtonPrivate *priv;

  priv = gsm_color_button_get_instance_private(color_button);
  priv->highlight = 0;
  priv->in_button = FALSE;
  gtk_widget_queue_draw(widget);
  return FALSE;
}

guint gsm_color_button_get_cbtype(GSMColorButton *color_button) {
  GSMColorButtonPrivate *priv;
  g_return_val_if_fail(GSM_IS_COLOR_BUTTON(color_button), 0);

  priv = gsm_color_button_get_instance_private(color_button);

  return priv->type;
}

void gsm_color_button_set_cbtype(GSMColorButton *color_button, guint type) {
  GSMColorButtonPrivate *priv;
  g_return_if_fail(GSM_IS_COLOR_BUTTON(color_button));

  priv = gsm_color_button_get_instance_private(color_button);

  priv->type = type;

  gtk_widget_queue_draw(GTK_WIDGET(color_button));

  g_object_notify(G_OBJECT(color_button), "type");
}

gdouble gsm_color_button_get_fraction(GSMColorButton *color_button) {
  GSMColorButtonPrivate *priv;
  g_return_val_if_fail(GSM_IS_COLOR_BUTTON(color_button), 0);

  priv = gsm_color_button_get_instance_private(color_button);

  return priv->fraction;
}

void gsm_color_button_set_fraction(GSMColorButton *color_button,
                                   gdouble fraction) {
  GSMColorButtonPrivate *priv;
  g_return_if_fail(GSM_IS_COLOR_BUTTON(color_button));

  priv = gsm_color_button_get_instance_private(color_button);

  priv->fraction = fraction;

  gtk_widget_queue_draw(GTK_WIDGET(color_button));

  g_object_notify(G_OBJECT(color_button), "fraction");
}

void gsm_color_button_get_color(GSMColorButton *color_button, GdkRGBA *color) {
  GSMColorButtonPrivate *priv;
  g_return_if_fail(GSM_IS_COLOR_BUTTON(color_button));

  priv = gsm_color_button_get_instance_private(color_button);
  color->red = priv->color.red;
  color->green = priv->color.green;
  color->blue = priv->color.blue;
  color->alpha = priv->color.alpha;
}

void gsm_color_button_set_color(GSMColorButton *color_button,
                                const GdkRGBA *color) {
  GSMColorButtonPrivate *priv;
  g_return_if_fail(GSM_IS_COLOR_BUTTON(color_button));
  g_return_if_fail(color != NULL);

  priv = gsm_color_button_get_instance_private(color_button);

  priv->color.red = color->red;
  priv->color.green = color->green;
  priv->color.blue = color->blue;
  priv->color.alpha = color->alpha;

  gtk_widget_queue_draw(GTK_WIDGET(color_button));

  g_object_notify(G_OBJECT(color_button), "color");
}

void gsm_color_button_set_title(GSMColorButton *color_button,
                                const gchar *title) {
  gchar *old_title;
  GSMColorButtonPrivate *priv;

  g_return_if_fail(GSM_IS_COLOR_BUTTON(color_button));
  priv = gsm_color_button_get_instance_private(color_button);

  old_title = priv->title;
  priv->title = g_strdup(title);
  g_free(old_title);

  if (priv->cc_dialog)
    gtk_window_set_title(GTK_WINDOW(priv->cc_dialog), priv->title);

  g_object_notify(G_OBJECT(color_button), "title");
}

const gchar *gsm_color_button_get_title(GSMColorButton *color_button) {
  GSMColorButtonPrivate *priv;
  g_return_val_if_fail(GSM_IS_COLOR_BUTTON(color_button), NULL);

  priv = gsm_color_button_get_instance_private(color_button);

  return priv->title;
}

static void gsm_color_button_set_property(GObject *object, guint param_id,
                                          const GValue *value,
                                          GParamSpec *pspec) {
  GSMColorButton *color_button = GSM_COLOR_BUTTON(object);

  switch (param_id) {
    case PROP_PERCENTAGE:
      gsm_color_button_set_fraction(color_button, g_value_get_double(value));
      break;
    case PROP_TITLE:
      gsm_color_button_set_title(color_button, g_value_get_string(value));
      break;
    case PROP_COLOR:
      gsm_color_button_set_color(color_button, g_value_get_boxed(value));
      break;
    case PROP_TYPE:
      gsm_color_button_set_cbtype(color_button, g_value_get_uint(value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, param_id, pspec);
      break;
  }
}

static void gsm_color_button_get_property(GObject *object, guint param_id,
                                          GValue *value, GParamSpec *pspec) {
  GSMColorButton *color_button = GSM_COLOR_BUTTON(object);
  GdkRGBA color;

  switch (param_id) {
    case PROP_PERCENTAGE:
      g_value_set_double(value, gsm_color_button_get_fraction(color_button));
      break;
    case PROP_TITLE:
      g_value_set_string(value, gsm_color_button_get_title(color_button));
      break;
    case PROP_COLOR:
      gsm_color_button_get_color(color_button, &color);
      g_value_set_boxed(value, &color);
      break;
    case PROP_TYPE:
      g_value_set_uint(value, gsm_color_button_get_cbtype(color_button));
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, param_id, pspec);
      break;
  }
}
