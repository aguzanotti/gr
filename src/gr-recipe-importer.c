/* gr-recipe-importer.c:
 *
 * Copyright (C) 2016 Matthias Clasen <mclasen@redhat.com>
 *
 * Licensed under the GNU General Public License Version 3
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#include <stdlib.h>
#include <glib/gi18n.h>
#include <gnome-autoar/gnome-autoar.h>
#include "glnx-shutil.h"

#include "gr-recipe-importer.h"
#include "gr-images.h"
#include "gr-chef.h"
#include "gr-recipe.h"
#include "gr-recipe-store.h"
#include "gr-app.h"
#include "gr-utils.h"


struct _GrRecipeImporter
{
        GObject parent_instance;

        GtkWindow *window;

        AutoarExtractor *extractor;
        GFile *output;
        char *dir;

        char *chef_name;
        char *chef_fullname;
        char *chef_description;
        char *chef_image_path;

        char *recipe_name;
        char *recipe_description;
        char *recipe_cuisine;
        char *recipe_season;
        char *recipe_category;
        char *recipe_prep_time;
        char *recipe_cook_time;
        char *recipe_ingredients;
        char *recipe_instructions;
        char *recipe_notes;
        char **recipe_paths;
        int *recipe_angles;
        gboolean *recipe_dark;
        int recipe_serves;
        GrDiets recipe_diets;
        GDateTime *recipe_ctime;
        GDateTime *recipe_mtime;
};

G_DEFINE_TYPE (GrRecipeImporter, gr_recipe_importer, G_TYPE_OBJECT)

static void
gr_recipe_importer_finalize (GObject *object)
{
        GrRecipeImporter *importer = GR_RECIPE_IMPORTER (object);

        g_clear_object (&importer->extractor);
        g_clear_object (&importer->output);
        g_free (importer->dir);
        g_free (importer->chef_name);
        g_free (importer->chef_fullname);
        g_free (importer->chef_description);
        g_free (importer->chef_image_path);

        g_free (importer->recipe_name);
        g_free (importer->recipe_description);
        g_free (importer->recipe_cuisine);
        g_free (importer->recipe_season);
        g_free (importer->recipe_category);
        g_free (importer->recipe_prep_time);
        g_free (importer->recipe_cook_time);
        g_free (importer->recipe_ingredients);
        g_free (importer->recipe_instructions);
        g_free (importer->recipe_notes);
        g_strfreev (importer->recipe_paths);
        g_free (importer->recipe_angles);
        g_free (importer->recipe_dark);
        g_clear_pointer (&importer->recipe_ctime, g_date_time_unref);
        g_clear_pointer (&importer->recipe_mtime, g_date_time_unref);

        G_OBJECT_CLASS (gr_recipe_importer_parent_class)->finalize (object);
}

static guint done_signal;

static void
gr_recipe_importer_class_init (GrRecipeImporterClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gr_recipe_importer_finalize;

        done_signal = g_signal_new ("done",
                                    G_TYPE_FROM_CLASS (klass),
                                    G_SIGNAL_RUN_LAST,
                                    0,
                                    NULL, NULL,
                                    NULL,
                                    G_TYPE_NONE, 1, GR_TYPE_RECIPE);
}

static void
gr_recipe_importer_init (GrRecipeImporter *self)
{
}

GrRecipeImporter *
gr_recipe_importer_new (GtkWindow *parent)
{
        GrRecipeImporter *importer;

        importer = g_object_new (GR_TYPE_RECIPE_IMPORTER, NULL);

        importer->window = parent;

        return importer;
}

static void
cleanup_import (GrRecipeImporter *importer)
{
        g_autoptr(GError) error = NULL;

        if (!glnx_shutil_rm_rf_at (-1, importer->dir, NULL, &error))
                g_warning ("Failed to clean up temp directory %s: %s", importer->dir, error->message);

        g_clear_pointer (&importer->dir, g_free);
        g_clear_object (&importer->extractor);
        g_clear_object (&importer->output);
        g_clear_pointer (&importer->chef_name, g_free);
        g_clear_pointer (&importer->chef_fullname, g_free);
        g_clear_pointer (&importer->chef_description, g_free);
        g_clear_pointer (&importer->chef_image_path, g_free);

        g_clear_pointer (&importer->recipe_name, g_free);
        g_clear_pointer (&importer->recipe_description, g_free);
        g_clear_pointer (&importer->recipe_cuisine, g_free);
        g_clear_pointer (&importer->recipe_season, g_free);
        g_clear_pointer (&importer->recipe_category, g_free);
        g_clear_pointer (&importer->recipe_prep_time, g_free);
        g_clear_pointer (&importer->recipe_cook_time, g_free);
        g_clear_pointer (&importer->recipe_ingredients, g_free);
        g_clear_pointer (&importer->recipe_instructions, g_free);
        g_clear_pointer (&importer->recipe_notes, g_free);
        g_clear_pointer (&importer->recipe_paths, g_strfreev);
        g_clear_pointer (&importer->recipe_angles, g_free);
        g_clear_pointer (&importer->recipe_dark, g_free);
        g_clear_pointer (&importer->recipe_ctime, g_date_time_unref);
        g_clear_pointer (&importer->recipe_mtime, g_date_time_unref);
}

static gboolean
copy_image (GrRecipeImporter  *importer,
            const char        *path,
            char             **new_path,
            GError           **error)
{
        g_autofree char *srcpath = NULL;
        g_autofree char *destpath = NULL;
        g_autoptr(GFile) source = NULL;
        g_autoptr(GFile) dest = NULL;
        g_autofree char *orig_dest = NULL;
        int i;

        srcpath = g_build_filename (importer->dir, path, NULL);
        source = g_file_new_for_path (srcpath);
        orig_dest = g_build_filename (g_get_user_data_dir (), "recipes", path, NULL);

        destpath = g_strdup (orig_dest);
        for (i = 1; i < 10; i++) {
                if (!g_file_test (destpath, G_FILE_TEST_EXISTS))
                        break;
                g_free (destpath);
                destpath = g_strdup_printf ("%s%d", orig_dest, i);
        }
        dest = g_file_new_for_path (destpath);

        if (!g_file_copy (source, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, error)) {
                return FALSE;
        }

        *new_path = g_strdup (destpath);

        return TRUE;
}

static void
error_cb (AutoarExtractor  *extractor,
          GError           *error,
          GrRecipeImporter *importer)
{
        GtkWidget *dialog;

        dialog = gtk_message_dialog_new (importer->window,
                                         GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_OK,
                                         _("Error while importing recipe:\n%s"),
                                         error->message);
        g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
        gtk_widget_show (dialog);

        cleanup_import (importer);
}

static void
do_import_recipe (GrRecipeImporter *importer)
{
        GrRecipeStore *store;
        g_autoptr(GArray) images = NULL;
        g_autoptr(GrRecipe) recipe = NULL;
        g_autoptr(GError) error = NULL;

        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));

        images = gr_rotated_image_array_new ();
        if (importer->recipe_paths) {
                int i;
                for (i = 0; importer->recipe_paths[i]; i++) {
                        GrRotatedImage ri;
                        char *new_path;

                        if (!copy_image (importer, importer->recipe_paths[i], &new_path, &error)) {
                                error_cb (importer->extractor, error, importer);
                                return;
                        }

                        ri.path = new_path;
                        ri.angle = importer->recipe_angles[i];
                        ri.dark_text = importer->recipe_dark[i];
                        g_array_append_val (images, ri);
                }
        }

        recipe = gr_recipe_new ();
        g_object_set (recipe,
                      "name", importer->recipe_name,
                      "author", importer->chef_name,
                      "description", importer->recipe_description,
                      "cuisine", importer->recipe_cuisine,
                      "season", importer->recipe_season,
                      "category", importer->recipe_category,
                      "prep-time", importer->recipe_prep_time,
                      "cook-time", importer->recipe_cook_time,
                      "ingredients", importer->recipe_ingredients,
                      "instructions", importer->recipe_instructions,
                      "notes", importer->recipe_notes,
                      "serves", importer->recipe_serves,
                      "diets", importer->recipe_diets,
                      "images", images,
                      "ctime", importer->recipe_ctime,
                      "mtime", importer->recipe_mtime,
                      NULL);

        if (!gr_recipe_store_add (store, recipe, &error)) {
                error_cb (importer->extractor, error, importer);
                return;
        }

        g_signal_emit (importer, done_signal, 0, recipe);

        cleanup_import (importer);
}

static void
recipe_dialog_response (GtkWidget        *dialog,
                        int               response_id,
                        GrRecipeImporter *importer)
{
        if (response_id == GTK_RESPONSE_CANCEL) {
                g_message ("Not importing recipe %s", importer->recipe_name);
        }
        else {
                g_free (importer->recipe_name);
                importer->recipe_name = g_strdup (g_object_get_data (G_OBJECT (dialog), "name"));
                g_message ("Renaming recipe to %s while importing", importer->recipe_name);
                do_import_recipe (importer);
        }

        gtk_widget_destroy (dialog);
}

static void
recipe_name_changed (GtkEntry         *entry,
                     GrRecipeImporter *importer)
{
        GrRecipeStore *store;
        const char *name;
        GtkWidget *dialog;
        g_autoptr(GrRecipe) recipe = NULL;

        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));

        name = gtk_entry_get_text (entry);
        recipe = gr_recipe_store_get (store, name);

        dialog = gtk_widget_get_ancestor (GTK_WIDGET (entry), GTK_TYPE_DIALOG);
        g_object_set_data_full (G_OBJECT (dialog), "name", g_strdup (name), g_free);
        gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
                                           GTK_RESPONSE_APPLY,
                                           name[0] != 0 && recipe == NULL);
}

static void
show_recipe_conflict_dialog (GrRecipeImporter *importer)
{
        g_autoptr(GtkBuilder) builder = NULL;
        GtkWidget *dialog;
        GtkWidget *name_entry;

        builder = gtk_builder_new_from_resource ("/org/gnome/Recipes/recipe-conflict-dialog.ui");
        dialog = GTK_WIDGET (gtk_builder_get_object (builder, "dialog"));
        gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (importer->window));

        name_entry = GTK_WIDGET (gtk_builder_get_object (builder, "name_entry"));
        gtk_entry_set_text (GTK_ENTRY (name_entry), importer->recipe_name);

        g_signal_connect (name_entry, "changed", G_CALLBACK (recipe_name_changed), importer);
        g_signal_connect (dialog, "response", G_CALLBACK (recipe_dialog_response), importer);
        gtk_widget_show (dialog);
}

#define handle_or_clear_error(error) \
        if (error) { \
                if (!g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) { \
                        error_cb (importer->extractor, error, importer); \
                        return; \
                } \
                g_clear_error (&error); \
        }

#define key_file_get_string(keyfile, group, key) ({ \
                g_autoptr(GError) my_error = NULL; \
                char *my_tmp = g_key_file_get_string (keyfile, group, key, &my_error); \
                handle_or_clear_error (my_error) \
                my_tmp; })

static void
import_recipe (GrRecipeImporter *importer)
{
        g_autoptr(GKeyFile) keyfile = NULL;
        g_autofree char *path = NULL;
        g_auto(GStrv) groups = NULL;
        char *tmp;
        GrRecipeStore *store;
        g_autoptr(GrRecipe) recipe = NULL;
        gsize length2, length3;
        g_autoptr(GError) error = NULL;

        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));

        keyfile = g_key_file_new ();
        path = g_build_filename (importer->dir, "recipes.db", NULL);
        if (!g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, &error)) {
                error_cb (importer->extractor, error, importer);
                return;
        }

        groups = g_key_file_get_groups (keyfile, NULL);
        if (!groups || !groups[0]) {
                g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED, _("No recipe found"));
                error_cb (importer->extractor, error, importer);
                return;
        }

        importer->recipe_name = g_key_file_get_string (keyfile, groups[0], "Name", &error);
        if (error) {
                error_cb (importer->extractor, error, importer);
                return;
        }
        importer->recipe_description = key_file_get_string (keyfile, groups[0], "Description");
        importer->recipe_cuisine = key_file_get_string (keyfile, groups[0], "Cuisine");
        importer->recipe_season = key_file_get_string (keyfile, groups[0], "Season");
        importer->recipe_category = key_file_get_string (keyfile, groups[0], "Category");
        importer->recipe_prep_time = key_file_get_string (keyfile, groups[0], "PrepTime");
        importer->recipe_cook_time = key_file_get_string (keyfile, groups[0], "CookTime");
        importer->recipe_ingredients = key_file_get_string (keyfile, groups[0], "Ingredients");
        importer->recipe_instructions = key_file_get_string (keyfile, groups[0], "Instructions");
        importer->recipe_notes = key_file_get_string (keyfile, groups[0], "Notes");
        importer->recipe_serves = g_key_file_get_integer (keyfile, groups[0], "Serves", &error);
        handle_or_clear_error (error);
        importer->recipe_diets = g_key_file_get_integer (keyfile, groups[0], "Diets", &error);
        handle_or_clear_error (error);
        tmp = key_file_get_string (keyfile, groups[0], "Created");
        if (tmp) {
               importer->recipe_ctime = date_time_from_string (tmp);
               if (!importer->recipe_ctime) {
                        g_free (tmp);
                        g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                     _("Failed to load recipe: Couldn't parse Created key"));
                        error_cb (importer->extractor, error, importer);
                        return;
                }
        }
        tmp = key_file_get_string (keyfile, groups[0], "Modified");
        if (tmp) {
               importer->recipe_mtime = date_time_from_string (tmp);
               if (!importer->recipe_mtime) {
                        g_free (tmp);
                        g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                     _("Failed to load recipe: Couldn't parse Modified key"));
                        error_cb (importer->extractor, error, importer);
                        return;
                }
        }
        importer->recipe_paths = g_key_file_get_string_list (keyfile, groups[0], "Images", &length2, &error);
        handle_or_clear_error (error);
        importer->recipe_angles = g_key_file_get_integer_list (keyfile, groups[0], "Angles", &length3, &error);
        handle_or_clear_error (error);
        if (length2 != length3) {
                g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             _("Failed to load recipe: Images and Angles length mismatch"));
                error_cb (importer->extractor, error, importer);
                return;
        }
        importer->recipe_dark = g_key_file_get_boolean_list (keyfile, groups[0], "DarkText", &length3, &error);
        handle_or_clear_error (error);
        if (length2 != length3) {
                g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                             _("Failed to load recipe: Images and DarkText length mismatch"));
                error_cb (importer->extractor, error, importer);
                return;
        }

        recipe = gr_recipe_store_get (store, importer->recipe_name);
        if (!recipe) {
                g_message ("Recipe %s not yet known; importing", importer->recipe_name);
                do_import_recipe (importer);
                return;
        }

        show_recipe_conflict_dialog (importer);
}

static void
import_chef (GrRecipeImporter *importer)
{
        GrRecipeStore *store;
        g_autoptr(GrChef) chef = NULL;
        g_autoptr(GError) error = NULL;

        if (importer->chef_image_path) {
                char *new_path;

                if (!copy_image (importer, importer->chef_image_path, &new_path, &error)) {
                        error_cb (importer->extractor, error, importer);
                        return;
                }

                g_free (importer->chef_image_path);
                importer->chef_image_path = new_path;
        }
        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));

        chef = g_object_new (GR_TYPE_CHEF,
                             "name", importer->chef_name,
                             "fullname", importer->chef_fullname,
                             "description", importer->chef_description,
                             "image-path", importer->chef_image_path,
                             NULL);

        if (!gr_recipe_store_add_chef (store, chef, &error)) {
                error_cb (importer->extractor, error, importer);
                return;
        }

        import_recipe (importer);
}

static void
chef_dialog_response (GtkWidget        *dialog,
                      int               response_id,
                      GrRecipeImporter *importer)
{
        if (response_id == GTK_RESPONSE_CANCEL) {
                g_message ("Chef %s known after all; not importing", importer->chef_name);
                import_recipe (importer);
        }
        else {
                g_free (importer->chef_name);
                importer->chef_name = g_strdup (g_object_get_data (G_OBJECT (dialog), "name"));
                g_message ("Renaming chef to %s while importing", importer->chef_name);
                import_chef (importer);
        }

        gtk_widget_destroy (dialog);
}

static void
chef_name_changed (GtkEntry         *entry,
                   GrRecipeImporter *importer)
{
        GrRecipeStore *store;
        const char *name;
        GtkWidget *dialog;
        g_autoptr(GrChef) chef = NULL;

        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));

        name = gtk_entry_get_text (entry);
        chef = gr_recipe_store_get_chef (store, name);

        dialog = gtk_widget_get_ancestor (GTK_WIDGET (entry), GTK_TYPE_DIALOG);
        g_object_set_data_full (G_OBJECT (dialog), "name", g_strdup (name), g_free);
        gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
                                           GTK_RESPONSE_APPLY,
                                           name[0] != 0 && chef == NULL);
}

static void
show_chef_conflict_dialog (GrRecipeImporter *importer,
                           GrChef           *chef)
{
        g_autoptr(GtkBuilder) builder = NULL;
        GtkWidget *dialog;
        GtkWidget *old_chef_name;
        GtkWidget *old_chef_fullname;
        GtkWidget *old_chef_description;
        GtkWidget *old_chef_picture;
        GtkWidget *new_chef_name;
        GtkWidget *new_chef_fullname;
        GtkWidget *new_chef_description;
        GtkWidget *new_chef_picture;
        GtkTextBuffer *buffer;

        builder = gtk_builder_new_from_resource ("/org/gnome/Recipes/chef-conflict-dialog.ui");
        dialog = GTK_WIDGET (gtk_builder_get_object (builder, "dialog"));
        gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (importer->window));

        old_chef_name = GTK_WIDGET (gtk_builder_get_object (builder, "old_chef_name"));
        old_chef_fullname = GTK_WIDGET (gtk_builder_get_object (builder, "old_chef_fullname"));
        old_chef_description = GTK_WIDGET (gtk_builder_get_object (builder, "old_chef_description"));
        old_chef_picture = GTK_WIDGET (gtk_builder_get_object (builder, "old_chef_picture"));
        gtk_entry_set_text (GTK_ENTRY (old_chef_name), gr_chef_get_name (chef));
        gtk_entry_set_text (GTK_ENTRY (old_chef_fullname), gr_chef_get_fullname (chef));
        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (old_chef_description));
        gtk_text_buffer_set_text (buffer, gr_chef_get_description (chef), -1);
        if (gr_chef_get_image (chef) != NULL) {
                g_autoptr(GdkPixbuf) pixbuf = NULL;
                pixbuf = load_pixbuf_fit_size (gr_chef_get_image (chef), 0, 64, 64, TRUE);
                gtk_image_set_from_pixbuf (GTK_IMAGE (old_chef_picture), pixbuf);
        }

        new_chef_name = GTK_WIDGET (gtk_builder_get_object (builder, "new_chef_name"));
        new_chef_fullname = GTK_WIDGET (gtk_builder_get_object (builder, "new_chef_fullname"));
        new_chef_description = GTK_WIDGET (gtk_builder_get_object (builder, "new_chef_description"));
        new_chef_picture = GTK_WIDGET (gtk_builder_get_object (builder, "new_chef_picture"));
        gtk_entry_set_text (GTK_ENTRY (new_chef_name), importer->chef_name);
        gtk_entry_set_text (GTK_ENTRY (new_chef_fullname), importer->chef_fullname);
        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (new_chef_description));
        gtk_text_buffer_set_text (buffer, importer->chef_description, -1);
        if (importer->chef_image_path != NULL) {
                g_autoptr(GdkPixbuf) pixbuf = NULL;
                g_autofree char *path = NULL;
                path = g_build_filename (importer->dir, importer->chef_image_path, NULL);
                pixbuf = load_pixbuf_fit_size (path, 0, 64, 64, TRUE);
                gtk_image_set_from_pixbuf (GTK_IMAGE (new_chef_picture), pixbuf);
        }

        g_signal_connect (new_chef_name, "changed", G_CALLBACK (chef_name_changed), importer);
        g_signal_connect (dialog, "response", G_CALLBACK (chef_dialog_response), importer);
        gtk_widget_show (dialog);
}

static void
finish_import (GrRecipeImporter *importer)
{
        g_autoptr(GKeyFile) keyfile = NULL;
        g_autofree char *path = NULL;
        g_auto(GStrv) groups = NULL;
        g_autofree char *name = NULL;
        g_autofree char *fullname = NULL;
        g_autofree char *description = NULL;
        g_autofree char *image_path = NULL;
        GrRecipeStore *store;
        g_autoptr(GrChef) chef = NULL;
        g_autoptr(GError) error = NULL;

        store = gr_app_get_recipe_store (GR_APP (g_application_get_default ()));

        keyfile = g_key_file_new ();
        path = g_build_filename (importer->dir, "chefs.db", NULL);
        if (!g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, &error)) {
                error_cb (importer->extractor, error, importer);
                return;
        }

        groups = g_key_file_get_groups (keyfile, NULL);
        if (!groups || !groups[0]) {
                g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED, _("No recipe found"));
                error_cb (importer->extractor, error, importer);
                return;
        }

        name = g_key_file_get_string (keyfile, groups[0], "Name", &error);
        if (error) {
                error_cb (importer->extractor, error, importer);
                return;
        }
        fullname = key_file_get_string (keyfile, groups[0], "Fullname");
        description = key_file_get_string (keyfile, groups[0], "Description");
        image_path = key_file_get_string (keyfile, groups[0], "Image");

        importer->chef_name = g_strdup (name);
        importer->chef_fullname = g_strdup (fullname);
        importer->chef_description = g_strdup (description);
        importer->chef_image_path = g_strdup (image_path);

        chef = gr_recipe_store_get_chef (store, name);
        if (!chef) {
                g_message ("Chef %s not yet known; importing", name);
                import_chef (importer);
                return;
        }

        if (g_strcmp0 (fullname, gr_chef_get_fullname (chef)) == 0 &&
            g_strcmp0 (description, gr_chef_get_description (chef)) == 0) {
                g_message ("Chef %s already known, not importing", name);
                import_recipe (importer);
                return;
        }

        show_chef_conflict_dialog (importer, chef);
}

static void
completed_cb (AutoarExtractor  *extractor,
              GrRecipeImporter *importer)
{
        finish_import (importer);
}

void
gr_recipe_importer_import_from (GrRecipeImporter *importer,
                                GFile            *file)
{
        importer->dir = g_mkdtemp (g_build_filename (g_get_tmp_dir (), "recipeXXXXXX", NULL));
        importer->output = g_file_new_for_path (importer->dir);

        importer->extractor = autoar_extractor_new (file, importer->output);
        autoar_extractor_set_output_is_dest (importer->extractor, TRUE);

        g_signal_connect (importer->extractor, "completed", G_CALLBACK (completed_cb), importer);
        g_signal_connect (importer->extractor, "error", G_CALLBACK (error_cb), importer);

        autoar_extractor_start_async (importer->extractor, NULL);
}
