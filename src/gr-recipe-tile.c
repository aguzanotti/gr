/* gr-recipe-tile.c:
 *
 * Copyright (C) 2016 Matthias Clasen <mclasen@redhat.com>
 *
 * Licensed under the GNU General Public License Version 3
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "gr-recipe-tile.h"
#include "gr-window.h"
#include "gr-utils.h"
#include "gr-images.h"


struct _GrRecipeTile
{
        GtkButton parent_instance;

        GrRecipe *recipe;

        GtkWidget *label;
        GtkWidget *author;
        GtkWidget *image;
        GtkWidget *box;
};

G_DEFINE_TYPE (GrRecipeTile, gr_recipe_tile, GTK_TYPE_BUTTON)

static void
show_details (GrRecipeTile *tile)
{
        GtkWidget *window;

        window = gtk_widget_get_ancestor (GTK_WIDGET (tile), GR_TYPE_WINDOW);
        gr_window_show_recipe (GR_WINDOW (window), tile->recipe);
}

static void
recipe_tile_set_recipe (GrRecipeTile *tile,
                        GrRecipe     *recipe)
{
        const char *name;
        const char *author;
        g_autofree char *image_path = NULL;
        g_autofree char *tmp = NULL;
        g_autoptr(GArray) images = NULL;
        const char *color;

        g_set_object (&tile->recipe, recipe);
        if (!recipe)
                return;

        name = gr_recipe_get_name (recipe);
        author = gr_recipe_get_author (recipe);

        g_object_get (recipe, "images", &images, NULL);

        if (images->len > 0) {
                g_autofree char *css = NULL;
                GrRotatedImage *ri = &g_array_index (images, GrRotatedImage, 0);

                css = g_strdup_printf ("  background: url('%s');\n"
                                       "  background-size: 100%%;\n"
                                       "  background-repeat: no-repeat;\n", ri->path);
                gr_utils_widget_set_css_simple (tile->box, css);

                if (ri->dark_text)
                        color = "black";
                else
                        color = "white";
        }
        else {
                color = "black";
        }

        if (color) {
                g_autofree char *css = NULL;
                css = g_strdup_printf (" color: %s;\n"
                                       " margin: 0 20px 0 20px;\n"
                                       " font-weight: bold;\n"
                                       " font-size: 24pt;\n", color);
                gr_utils_widget_set_css_simple (tile->label, css);

                g_free (css);
                css = g_strdup_printf (" color: %s;\n"
                                       " margin: 10px 20px 20px 20px;\n"
                                       " font-size: 12pt;\n", color);
                gr_utils_widget_set_css_simple (tile->author, css);
        }

        gtk_label_set_label (GTK_LABEL (tile->label), name);
        tmp = g_strdup_printf (_("by %s"), author);
        gtk_label_set_label (GTK_LABEL (tile->author), tmp);
}

static void
recipe_tile_finalize (GObject *object)
{
        GrRecipeTile *tile = GR_RECIPE_TILE (object);

        g_clear_object (&tile->recipe);

        G_OBJECT_CLASS (gr_recipe_tile_parent_class)->finalize (object);
}

static void
gr_recipe_tile_init (GrRecipeTile *tile)
{
        gtk_widget_set_has_window (GTK_WIDGET (tile), FALSE);
        gtk_widget_init_template (GTK_WIDGET (tile));
}

static void
gr_recipe_tile_class_init (GrRecipeTileClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->finalize = recipe_tile_finalize;

        gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Recipes/gr-recipe-tile.ui");

        gtk_widget_class_bind_template_child (widget_class, GrRecipeTile, label);
        gtk_widget_class_bind_template_child (widget_class, GrRecipeTile, author);
        gtk_widget_class_bind_template_child (widget_class, GrRecipeTile, image);
        gtk_widget_class_bind_template_child (widget_class, GrRecipeTile, box);

        gtk_widget_class_bind_template_callback (widget_class, show_details);
}

GtkWidget *
gr_recipe_tile_new (GrRecipe *recipe)
{
        GrRecipeTile *tile;

        tile = g_object_new (GR_TYPE_RECIPE_TILE, NULL);
        recipe_tile_set_recipe (GR_RECIPE_TILE (tile), recipe);

        return GTK_WIDGET (tile);
}

GrRecipe *
gr_recipe_tile_get_recipe (GrRecipeTile *tile)
{
        return tile->recipe;
}
