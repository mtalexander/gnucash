/*******************************************************************\
 * assistant-csv-account-import.c -- An assistant for importing     *
 *                                         Accounts from a file.    *
 *                                                                  *
 * Copyright (C) 2012 Robert Fewell                                 *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
\********************************************************************/
/** @file assistant-csv-account-import.c
    @brief CSV Import Assistant
    @author Copyright (c) 2012 Robert Fewell
*/
#include "config.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "dialog-utils.h"
#include "gnc-ui.h"
#include "gnc-uri-utils.h"
#include "gnc-ui-util.h"

#include "gnc-component-manager.h"

#include "assistant-utils.h"
#include "assistant-csv-account-import.h"
#include "csv-account-import.h"

#define GCONF_SECTION "dialogs/import/csv"
#define GNC_PREFS_GROUP "dialogs.import.csv"
#define ASSISTANT_CSV_IMPORT_CM_CLASS "assistant-csv-account-import"

/* This static indicates the debugging module that this .o belongs to.  */
static QofLogModule log_module = GNC_MOD_ASSISTANT;

/*************************************************************************/

void csv_import_assistant_prepare (GtkAssistant  *assistant, GtkWidget *page, gpointer user_data);
void csv_import_assistant_finish (GtkAssistant *gtkassistant, gpointer user_data);
void csv_import_assistant_cancel (GtkAssistant *gtkassistant, gpointer user_data);
void csv_import_assistant_close (GtkAssistant *gtkassistant, gpointer user_data);

void csv_import_assistant_start_page_prepare (GtkAssistant *gtkassistant, gpointer user_data);
void csv_import_assistant_account_page_prepare (GtkAssistant *gtkassistant, gpointer user_data);
void csv_import_assistant_file_page_prepare (GtkAssistant *assistant, gpointer user_data);
void csv_import_assistant_finish_page_prepare (GtkAssistant *assistant, gpointer user_data);
void csv_import_assistant_summary_page_prepare (GtkAssistant *assistant, gpointer user_data);

void csv_import_sep_cb (GtkWidget *radio, gpointer user_data );
void csv_import_hrows_cb (GtkWidget *spin, gpointer user_data );

void csv_import_file_chooser_confirm_cb (GtkWidget *button, CsvImportInfo *info);

static gchar *gnc_input_dialog (GtkWidget *parent, const gchar *title, const gchar *msg, const gchar *default_input);

static const gchar *finish_tree_string = N_(
            "The accounts will be imported from the file '%s' when you click 'Apply'.\n\n"
            "You can also go back and verify your selections by clicking on 'Back'"
            " or 'Cancel' to Abort Import.\n");

static const gchar *new_book_finish_tree_string = N_(
            "The accounts will be imported from the file '%s' when you click 'Apply'.\n\n"
            "You can also go back and verify your selections by clicking on 'Back'"
            " or 'Cancel' to Abort Import.\n\n"
            "If this is your initial import into a new file, you will first see "
            "a dialog for setting book options, since these can affect how "
            "imported data are converted to GnuCash transactions. If this is an "
            "existing file, the dialog will not be shown.\n");

/* Escape '_' in string */
static gchar *mnemonic_escape (const gchar *source);
static gchar *mnemonic_escape (const gchar *source)
{
    const guchar *p;
    gchar *dest;
    gchar *q;

    g_return_val_if_fail (source != NULL, NULL);

    p = (guchar *) source;
    q = dest = g_malloc (strlen (source) * 2 + 1);

    while (*p)
      {
        switch (*p)
          {
          case '_':
            *q++ = '_';
            *q++ = '_';
            break;
          default:
            *q++ = *p;
            break;
          }
        p++;
      }
    *q = 0;
    return dest;
}

/*************************************************************************/

/**************************************************
 * csv_file_chooser_confirm_cb
 *
 * call back for ok button in file chooser widget
 **************************************************/
void
csv_import_file_chooser_confirm_cb (GtkWidget *button, CsvImportInfo *info)
{
    GtkAssistant *assistant = GTK_ASSISTANT(info->window);
    gint num = gtk_assistant_get_current_page (assistant);
    GtkWidget *page = gtk_assistant_get_nth_page (assistant, num);

    gchar *file_name;
    csv_import_result res;

    gtk_assistant_set_page_complete (assistant, page, FALSE);

    file_name = gtk_file_chooser_get_filename ( GTK_FILE_CHOOSER(info->file_chooser ));

    if (file_name)
    {
        gchar *filepath = gnc_uri_get_path ( file_name );
        gchar *filedir = g_path_get_dirname( filepath );
        info->starting_dir = g_strdup(filedir);
        g_free ( filedir );
        g_free ( filepath );

        info->file_name = g_strdup(file_name);

        // generate preview
        gtk_list_store_clear (info->store);
        res = csv_import_read_file (info->file_name, info->regexp->str, info->store, 1 );
        if (res == RESULT_OPEN_FAILED)
            gnc_error_dialog (info->window, _("The input file can not be opened."));
        else if (res == RESULT_OK)
            gtk_assistant_set_page_complete (assistant, page, TRUE);
        else if (res == MATCH_FOUND)
            gtk_assistant_set_page_complete (assistant, page, TRUE);
    }
    g_free(file_name);

    DEBUG("file_name selected is %s", info->file_name);
    DEBUG("starting directory is %s", info->starting_dir);

    /* Step to next page if page is complete */
    if(gtk_assistant_get_page_complete(assistant, page))
        gtk_assistant_set_current_page (assistant, num + 1);

}


/*******************************************************
 * csv_import_hrows_cb
 *
 * call back for the start row / number of header rows
 *******************************************************/
void csv_import_hrows_cb (GtkWidget *spin, gpointer user_data )
{
    CsvImportInfo *info = user_data;

    GtkTreeIter iter;
    gboolean valid;
    int num_rows;

    /* Get number of rows for header */
    info->header_rows = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON(spin));

    /* Get number of rows displayed */
    num_rows = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(info->store), NULL);

    /* Modify background color for header rows */
    if (info->header_rows == 0)
    {
        valid = gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(info->store), &iter, NULL, 0 );
        if (valid)
            gtk_list_store_set (info->store, &iter, ROW_COLOR, NULL, -1);
    }
    else
    {
        if (info->header_rows - 1 < num_rows)
        {
            valid = gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(info->store), &iter, NULL, info->header_rows - 1 );
            if (valid)
                gtk_list_store_set (info->store, &iter, ROW_COLOR, "pink", -1);
            valid = gtk_tree_model_iter_next (GTK_TREE_MODEL(info->store), &iter);
            if (valid)
                gtk_list_store_set (info->store, &iter, ROW_COLOR, NULL, -1);
        }
    }
}


/*******************************************************
 * csv_import_sep_cb
 *
 * call back for type of separartor required
 *******************************************************/
void csv_import_sep_cb (GtkWidget *radio, gpointer user_data )
{
    CsvImportInfo *info = user_data;
    const gchar *name;
    gchar *temp;

    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio)))
    {
        LEAVE("1st callback of pair. Defer to 2nd callback.");
        return;
    }

    name = gtk_buildable_get_name(GTK_BUILDABLE(radio));

    if (g_strcmp0(name, g_strdup("radio_semi")) == 0)
        g_string_assign (info->regexp, "^(?<type>[^;]*);(?<full_name>[^;]*);(?<name>[^;]*);(?<code>[^;]*);?(?<description>[^;]*);?(?<color>[^;]*);?(?<notes>[^;]*);?(?<commoditym>[^;]*);?(?<commodityn>[^;]*);?(?<hidden>[^;]*);?(?<tax>[^;]*);?(?<place_holder>[^;]*)$");

    if (g_strcmp0(name, g_strdup("radio_comma")) == 0)
        g_string_assign (info->regexp, "^(?<type>[^,]*),(?<full_name>[^,]*),(?<name>[^,]*),(?<code>[^,]*),?(?<description>[^,]*),?(?<color>[^,]*),?(?<notes>[^,]*),?(?<commoditym>[^,]*),?(?<commodityn>[^,]*),?(?<hidden>[^,]*),?(?<tax>[^,]*),?(?<place_holder>[^,]*)$");

    if (g_strcmp0(name, g_strdup("radio_semiq")) == 0)
        g_string_assign (info->regexp, "^((?<type>[^\";]*)|\"(?<type>[^\"]*)\");((?<full_name>[^\";]*)|\"(?<full_name>[^\"]*)\");((?<name>[^\";]*)|\"(?<name>[^\"]*)\");((?<code>[^\";]*)|\"(?<code>[^\"]*)\");((?<description>[^\";]*)|\"(?<description>[^\"]*)\");((?<color>[^\";]*)|\"(?<color>[^\"]*)\");((?<notes>[^\";]*)|\"(?<notes>[^\"]*)\");((?<commoditym>[^\";]*)|\"(?<commoditym>[^\"]*)\");((?<commodityn>[^\";]*)|\"(?<commodityn>[^\"]*)\");((?<hidden>[^\";]*)|\"(?<hidden>[^\"]*)\");((?<tax>[^\";]*)|\"(?<tax>[^\"]*)\");((?<place_holder>[^\";]*)|\"(?<place_holder>[^\"]*)\")$");

    if (g_strcmp0(name, g_strdup("radio_commaq")) == 0)
        g_string_assign (info->regexp, "^((?<type>[^\",]*)|\"(?<type>[^\"]*)\"),((?<full_name>[^\",]*)|\"(?<full_name>[^\"]*)\"),((?<name>[^\",]*)|\"(?<name>[^\"]*)\"),((?<code>[^\",]*)|\"(?<code>[^\"]*)\"),((?<description>[^\",]*)|\"(?<description>[^\"]*)\"),((?<color>[^\",]*)|\"(?<color>[^\"]*)\"),((?<notes>[^\",]*)|\"(?<notes>[^\"]*)\"),((?<commoditym>[^\",]*)|\"(?<commoditym>[^\"]*)\"),((?<commodityn>[^\",]*)|\"(?<commodityn>[^\"]*)\"),((?<hidden>[^\",]*)|\"(?<hidden>[^\"]*)\"),((?<tax>[^\",]*)|\"(?<tax>[^\"]*)\"),((?<place_holder>[^\",]*)|\"(?<place_holder>[^\"]*)\")$");

    if (g_strcmp0(name, g_strdup("radio_custom")) == 0)
    {
        temp = gnc_input_dialog (0, _("Adjust regular expression used for import"), _("This regular expression is used to parse the import file. Modify according to your needs.\n"), info->regexp->str);
        if (temp)
        {
            g_string_assign (info->regexp, temp);
            g_free (temp);
        }
    }

    /* Generate preview */
    gtk_list_store_clear (info->store);

    if (csv_import_read_file (info->file_name, info->regexp->str, info->store, 11 ) == MATCH_FOUND)
        gtk_widget_set_sensitive (info->header_row_spin, TRUE);
    else
        gtk_widget_set_sensitive (info->header_row_spin, FALSE);

    /* Reset Header spin to 0 */
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(info->header_row_spin), 0 );
}


/*******************************************************
 * load_settings
 *
 * load the default settings for the assistant
 *******************************************************/
static
void load_settings (CsvImportInfo *info)
{
    info->header_rows = 0;
    info->error = "";
    info->starting_dir = NULL;
    info->file_name = NULL;
    info->error = "";

    /* The default directory for the user to select files. */
    info->starting_dir = gnc_get_default_directory(GNC_PREFS_GROUP);
}


/* =============================================================== */


/********************************************************************\
 * gnc_input_dialog                                                 *
 *   simple convenience dialog to get a single value from the user  *
 *   user may choose between "Ok" and "Cancel"                      *
 *                                                                  *
 * NOTE: This function does not return until the dialog is closed   *
 *                                                                  *
 * Args:   parent  - the parent window or NULL                      *
 *         title   - the title of the dialog                        *
 *         msg     - the message to display                         *
 *         default_input - will be displayed as default input       *
 * Return: the input (text) the user entered, if pressed "Ok"       *
 *         NULL, if pressed "Cancel"                                *
\********************************************************************/
static gchar *
gnc_input_dialog (GtkWidget *parent, const gchar *title, const gchar *msg, const gchar *default_input)
{
    GtkWidget *dialog, *label, *content_area;
    gint result;
    GtkWidget *view;
    GtkTextBuffer *buffer;
    gchar *user_input;
    GtkTextIter start, end;

    /* Create the widgets */
    dialog = gtk_dialog_new_with_buttons (title, GTK_WINDOW (parent),
                                          GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                          NULL);

    content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

    // add a label
    label = gtk_label_new (msg);
    gtk_container_add (GTK_CONTAINER (content_area), label);

    // add a textview
    view = gtk_text_view_new ();
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (view), GTK_WRAP_WORD_CHAR);
    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
    gtk_text_buffer_set_text (buffer, default_input, -1);
    gtk_container_add (GTK_CONTAINER (content_area), view);

    // run the dialog
    gtk_widget_show_all (dialog);
    result = gtk_dialog_run (GTK_DIALOG (dialog));

    if (result == GTK_RESPONSE_REJECT)
        user_input = 0;
    else
    {
        gtk_text_buffer_get_start_iter (buffer, &start);
        gtk_text_buffer_get_end_iter (buffer, &end);
        user_input = gtk_text_buffer_get_text (buffer,
                                               &start, &end, FALSE);
    }

    gtk_widget_destroy (dialog);

    return user_input;
}


/* =============================================================== */


/*******************************************************
 * Assistant page prepare functions
 *******************************************************/
void
csv_import_assistant_start_page_prepare (GtkAssistant *assistant,
        gpointer user_data)
{
    gint num = gtk_assistant_get_current_page (assistant);
    GtkWidget *page = gtk_assistant_get_nth_page (assistant, num);

    /* Enable the Assistant Buttons */
    gtk_assistant_set_page_complete (assistant, page, TRUE);
}


void
csv_import_assistant_file_page_prepare (GtkAssistant *assistant,
                                        gpointer user_data)
{
    CsvImportInfo *info = user_data;
    gint num = gtk_assistant_get_current_page (assistant);
    GtkWidget *page = gtk_assistant_get_nth_page (assistant, num);

    /* Set the default directory */
    if (info->starting_dir)
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(info->file_chooser), info->starting_dir);

    /* Disable the Forward Assistant Button */
    gtk_assistant_set_page_complete (assistant, page, FALSE);
}


void
csv_import_assistant_account_page_prepare (GtkAssistant *assistant,
        gpointer user_data)
{
    CsvImportInfo *info = user_data;

    gtk_list_store_clear (info->store);

    if (csv_import_read_file (info->file_name, info->regexp->str, info->store, 11 ) == MATCH_FOUND)
        gtk_widget_set_sensitive (info->header_row_spin, TRUE);
    else
        gtk_widget_set_sensitive (info->header_row_spin, FALSE);
}


void
csv_import_assistant_finish_page_prepare (GtkAssistant *assistant,
        gpointer user_data)
{
    CsvImportInfo *info = user_data;
    gint num = gtk_assistant_get_current_page (assistant);
    GtkWidget *page = gtk_assistant_get_nth_page (assistant, num);
    gchar *text;

    /* Set Finish page text */
    /* Before creating accounts, if this is a new book, tell user they can
     * specify book options, since they affect how transactions are created */
    if (info->new_book)
    {
        text = g_strdup_printf (gettext (new_book_finish_tree_string), info->file_name);
    }
    else
    {
        text = g_strdup_printf (gettext (finish_tree_string), info->file_name);
    }
    gtk_label_set_text (GTK_LABEL(info->finish_label), text);
    g_free(text);

    /* Save the Window size and directory */
    gnc_set_default_directory(GNC_PREFS_GROUP, info->starting_dir);

    /* Enable the Assistant Buttons */
    gtk_assistant_set_page_complete (assistant, page, TRUE);
}


void
csv_import_assistant_summary_page_prepare (GtkAssistant *assistant,
        gpointer user_data)
{
    CsvImportInfo *info = user_data;
    gchar *text, *errtext, *mtext;

    /* Before creating accounts, if this is a new book, let user specify
     * book options, since they affect how transactions are created */
    if (info->new_book)
        info->new_book = gnc_new_book_option_display();

    if (!g_strcmp0(info->error, "") == 0)
    {
        GtkTextBuffer *buffer;

        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (info->summary_error_view));
        text = g_strdup_printf(gettext ("Import completed but with errors!\n\nThe number of Accounts added was %u and "
                                        "updated was %u.\n\nSee below for errors..." ), info->num_new, info->num_updates );
        errtext = g_strdup_printf ( "%s", info->error);
        gtk_text_buffer_set_text (buffer, errtext, -1);
        g_free(errtext);
        g_free(info->error);
    }
    else
        text = g_strdup_printf(gettext ("Import completed successfully!\n\nThe number of Accounts added was %u and "
                                        "updated was %u.\n" ), info->num_new, info->num_updates );

    mtext = g_strdup_printf("<span size=\"medium\"><b>%s</b></span>", text);
    gtk_label_set_markup(GTK_LABEL(info->summary_label), mtext);

    g_free(text);
    g_free(mtext);
}


void
csv_import_assistant_prepare (GtkAssistant *assistant, GtkWidget *page,
                              gpointer user_data)
{
    gint currentpage = gtk_assistant_get_current_page(assistant);

    switch (currentpage)
    {
    case 0:
        /* Current page is Import Start page */
        csv_import_assistant_start_page_prepare (assistant, user_data);
        break;
    case 1:
        /* Current page is File select page */
        csv_import_assistant_file_page_prepare (assistant, user_data);
        break;
    case 2:
        /* Current page is Account page */
        csv_import_assistant_account_page_prepare (assistant, user_data);
        break;
    case 3:
        /* Current page is Finish page */
        csv_import_assistant_finish_page_prepare (assistant, user_data);
        break;
    case 4:
        /* Current page is Summary page */
        csv_import_assistant_summary_page_prepare (assistant, user_data);
        break;
    }
}


/*******************************************************
 * Assistant call back functions
 *******************************************************/
static void
csv_import_assistant_destroy_cb (GtkObject *object, gpointer user_data)
{
    CsvImportInfo *info = user_data;
    gnc_unregister_gui_component_by_data (ASSISTANT_CSV_IMPORT_CM_CLASS, info);
    g_free (info);
}

void
csv_import_assistant_cancel (GtkAssistant *assistant, gpointer user_data)
{
    CsvImportInfo *info = user_data;
    gnc_close_gui_component_by_data (ASSISTANT_CSV_IMPORT_CM_CLASS, info);
}

void
csv_import_assistant_close (GtkAssistant *assistant, gpointer user_data)
{
    CsvImportInfo *info = user_data;
    gnc_close_gui_component_by_data (ASSISTANT_CSV_IMPORT_CM_CLASS, info);
}

void
csv_import_assistant_finish (GtkAssistant *assistant, gpointer user_data)
{
    CsvImportInfo *info = user_data;

    gtk_list_store_clear (info->store);
    csv_import_read_file (info->file_name, info->regexp->str, info->store, 0 );
    csv_account_import (info);
}

static void
csv_import_close_handler (gpointer user_data)
{
    CsvImportInfo *info = user_data;

    g_free(info->starting_dir);
    g_free(info->file_name);
    g_string_free(info->regexp, TRUE);

    gnc_save_window_size(GCONF_SECTION, GTK_WINDOW(info->window));
    gtk_widget_destroy (info->window);
}

/*******************************************************
 * Create the Assistant
 *******************************************************/
static GtkWidget *
csv_import_assistant_create (CsvImportInfo *info)
{
    GtkBuilder *builder;
    GtkWidget *window;
    GtkWidget *box, *h_box;
    GtkWidget *button;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    builder = gtk_builder_new();
    gnc_builder_add_from_file  (builder , "assistant-csv-account-import.glade", "num_hrows_adj");
    gnc_builder_add_from_file  (builder , "assistant-csv-account-import.glade", "CSV Account Import Assistant");
    window = GTK_WIDGET(gtk_builder_get_object (builder, "CSV Account Import Assistant"));
    info->window = window;

    /* Set the assistant colors */
    gnc_assistant_set_colors (GTK_ASSISTANT (info->window));

    /* Load default settings */
    load_settings (info);

    /* Enable buttons on all page. */
    gtk_assistant_set_page_complete (GTK_ASSISTANT (window),
                                     GTK_WIDGET(gtk_builder_get_object(builder, "start_page")),
                                     TRUE);
    gtk_assistant_set_page_complete (GTK_ASSISTANT (window),
                                     GTK_WIDGET(gtk_builder_get_object(builder, "file_page")),
                                     FALSE);
    gtk_assistant_set_page_complete (GTK_ASSISTANT (window),
                                     GTK_WIDGET(gtk_builder_get_object(builder, "import_tree_page")),
                                     TRUE);
    gtk_assistant_set_page_complete (GTK_ASSISTANT (window),
                                     GTK_WIDGET(gtk_builder_get_object(builder, "end_page")),
                                     FALSE);
    gtk_assistant_set_page_complete (GTK_ASSISTANT (window),
                                     GTK_WIDGET(gtk_builder_get_object(builder, "summary_page")),
                                     TRUE);

    /* Start Page */

    /* File chooser Page */
    info->file_chooser = gtk_file_chooser_widget_new (GTK_FILE_CHOOSER_ACTION_OPEN);
    g_signal_connect (G_OBJECT (info->file_chooser), "file-activated",
                      G_CALLBACK (csv_import_file_chooser_confirm_cb), info);
    button = gtk_button_new_from_stock(GTK_STOCK_OK);
    gtk_widget_set_size_request (button, 100, -1);
    gtk_widget_show (button);
    h_box = gtk_hbox_new(TRUE, 0);
    gtk_box_pack_start(GTK_BOX(h_box), button, FALSE, FALSE, 0);
    gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER(info->file_chooser), h_box);
    g_signal_connect (G_OBJECT (button), "clicked",
                      G_CALLBACK (csv_import_file_chooser_confirm_cb), info);

    box = GTK_WIDGET(gtk_builder_get_object(builder, "file_page"));
    gtk_box_pack_start (GTK_BOX (box), info->file_chooser, TRUE, TRUE, 6);
    gtk_widget_show (info->file_chooser);

    /* Account Tree Page */
    info->header_row_spin = GTK_WIDGET(gtk_builder_get_object (builder, "num_hrows"));
    info->tree_view = GTK_WIDGET(gtk_builder_get_object (builder, "treeview"));

    info->regexp = g_string_new ( "^(?<type>[^;]*);(?<full_name>[^;]*);(?<name>[^;]*);(?<code>[^;]*);?(?<description>[^;]*);?(?<color>[^;]*);?(?<notes>[^;]*);?(?<commoditym>[^;]*);?(?<commodityn>[^;]*);?(?<hidden>[^;]*);?(?<tax>[^;]*);?(?<place_holder>[^;]*)$");

    /* create model and bind to view */
    info->store = gtk_list_store_new (N_COLUMNS,
                                      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_view_set_model( GTK_TREE_VIEW(info->tree_view), GTK_TREE_MODEL(info->store) );
#define CREATE_COLUMN(description,column_id) \
  renderer = gtk_cell_renderer_text_new (); \
  column = gtk_tree_view_column_new_with_attributes (mnemonic_escape(_(description)), renderer, "text", column_id, NULL); \
  gtk_tree_view_column_add_attribute(column, renderer, "background", ROW_COLOR); \
  gtk_tree_view_column_set_resizable (column, TRUE); \
  gtk_tree_view_append_column (GTK_TREE_VIEW (info->tree_view), column);
    CREATE_COLUMN ("type", TYPE);
    CREATE_COLUMN ("full_name", FULL_NAME);
    CREATE_COLUMN ("name", NAME);
    CREATE_COLUMN ("code", CODE);
    CREATE_COLUMN ("description", DESCRIPTION);
    CREATE_COLUMN ("color", COLOR);
    CREATE_COLUMN ("notes", NOTES);
    CREATE_COLUMN ("commoditym", COMMODITYM);
    CREATE_COLUMN ("commodityn", COMMODITYN);
    CREATE_COLUMN ("hidden", HIDDEN);
    CREATE_COLUMN ("tax", TAX);
    CREATE_COLUMN ("place_holder", PLACE_HOLDER);

    /* Finish Page */
    info->finish_label = GTK_WIDGET(gtk_builder_get_object(builder, "end_page"));
    /* Summary Page */
    info->summary_label = GTK_WIDGET(gtk_builder_get_object(builder, "summary_label"));
    info->summary_error_view = GTK_WIDGET(gtk_builder_get_object(builder, "summary_error_view"));

    g_signal_connect (G_OBJECT(window), "destroy",
                      G_CALLBACK (csv_import_assistant_destroy_cb), info);

    gnc_restore_window_size (GCONF_SECTION, GTK_WINDOW(info->window));

    gtk_builder_connect_signals(builder, info);
    g_object_unref(G_OBJECT(builder));
    return window;
}


/********************************************************************\
 * gnc_file_csv_account_import                                      *
 * opens up a assistant to import accounts.                         *
 *                                                                  *
 * Args:   import_type                                              *
 * Return: nothing                                                  *
\********************************************************************/
void
gnc_file_csv_account_import(void)
{
    CsvImportInfo *info;

    info = g_new0 (CsvImportInfo, 1);

    /* In order to trigger a book options display on the creation of a new book,
     * we need to detect when we are dealing with a new book. */
    info->new_book = gnc_is_new_book();

    csv_import_assistant_create (info);

    gnc_register_gui_component (ASSISTANT_CSV_IMPORT_CM_CLASS,
				NULL, csv_import_close_handler,
				info);

    gtk_widget_show_all (info->window);

    gnc_window_adjust_for_screen (GTK_WINDOW(info->window));
}