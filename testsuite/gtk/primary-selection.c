/* Tests for the PRIMARY selection
 *
 * Text that was selected stays in PRIMARY after it is deselected,
 * replaced or deleted, until something else claims PRIMARY.
 */

#include <gtk/gtk.h>

#include "gtk/gtkwidgetprivate.h"

/* Compositors only let a focused client own PRIMARY */
static gboolean have_focus;

static gboolean
skip_without_focus (void)
{
  if (!have_focus)
    g_test_skip ("Can't get the focus, so we can't own PRIMARY");

  return !have_focus;
}

static GdkClipboard *
get_primary (void)
{
  return gdk_display_get_primary_clipboard (gdk_display_get_default ());
}

static gboolean
timed_out (gpointer data)
{
  g_error ("Timed out waiting for %s", (const char *) data);

  return G_SOURCE_REMOVE;
}

static void
text_received (GObject      *source,
               GAsyncResult *result,
               gpointer      data)
{
  char **text = data;

  *text = gdk_clipboard_read_text_finish (GDK_CLIPBOARD (source), result, NULL);
  if (*text == NULL)
    *text = g_strdup ("(none)");

  g_main_context_wakeup (NULL);
}

/* Returns "(none)" if there is nothing to paste */
static char *
read_primary (void)
{
  char *text = NULL;
  guint id;

  id = g_timeout_add_seconds (5, timed_out, (gpointer) "PRIMARY");
  gdk_clipboard_read_text_async (get_primary (), NULL, text_received, &text);

  while (text == NULL)
    g_main_context_iteration (NULL, TRUE);

  g_source_remove (id);

  return text;
}

#define assert_primary(expected) \
  G_STMT_START { \
    char *_text = read_primary (); \
    g_assert_cmpstr (_text, ==, expected); \
    g_free (_text); \
  } G_STMT_END

static void
set_primary (const char *text)
{
  gdk_clipboard_set_text (get_primary (), text);
}

static void
select_range (GtkTextBuffer *buffer,
              int            ins,
              int            bound)
{
  GtkTextIter ins_iter, bound_iter;

  gtk_text_buffer_get_iter_at_offset (buffer, &ins_iter, ins);
  gtk_text_buffer_get_iter_at_offset (buffer, &bound_iter, bound);
  gtk_text_buffer_select_range (buffer, &ins_iter, &bound_iter);
}

static void
place_cursor (GtkTextBuffer *buffer,
              int            pos)
{
  GtkTextIter iter;

  gtk_text_buffer_get_iter_at_offset (buffer, &iter, pos);
  gtk_text_buffer_place_cursor (buffer, &iter);
}

static GtkTextBuffer *
create_buffer (const char *text)
{
  GtkTextBuffer *buffer;

  buffer = gtk_text_buffer_new (NULL);
  gtk_text_buffer_set_text (buffer, text, -1);
  gtk_text_buffer_add_selection_clipboard (buffer, get_primary ());

  return buffer;
}

static void
test_text_buffer_deselect (void)
{
  GtkTextBuffer *buffer;
  GtkTextIter iter;

  if (skip_without_focus ())
    return;

  set_primary ("other");
  buffer = create_buffer ("Hello primary selection world");

  select_range (buffer, 23, 6);
  assert_primary ("primary selection");

  place_cursor (buffer, 2);
  g_assert_false (gtk_text_buffer_get_has_selection (buffer));
  assert_primary ("primary selection");

  /* A new selection replaces it */
  select_range (buffer, 5, 0);
  assert_primary ("Hello");

  /* Backwards selections, deselected by moving a single mark */
  select_range (buffer, 6, 23);
  assert_primary ("primary selection");
  gtk_text_buffer_get_iter_at_offset (buffer, &iter, 6);
  gtk_text_buffer_move_mark_by_name (buffer, "selection_bound", &iter);
  g_assert_false (gtk_text_buffer_get_has_selection (buffer));
  assert_primary ("primary selection");

  gtk_text_buffer_remove_selection_clipboard (buffer, get_primary ());
  g_object_unref (buffer);
}

static void
test_text_buffer_delete (void)
{
  GtkTextBuffer *buffer;
  GtkTextIter start, end;

  if (skip_without_focus ())
    return;

  set_primary ("other");
  buffer = create_buffer ("abcdefghij");

  /* Deleting a part of the selection keeps it alive */
  select_range (buffer, 8, 2);
  gtk_text_buffer_get_iter_at_offset (buffer, &start, 0);
  gtk_text_buffer_get_iter_at_offset (buffer, &end, 4);
  gtk_text_buffer_delete (buffer, &start, &end);
  g_assert_true (gtk_text_buffer_get_has_selection (buffer));
  assert_primary ("efgh");

  /* Deleting all of it keeps the text */
  gtk_text_buffer_delete_selection (buffer, TRUE, TRUE);
  g_assert_false (gtk_text_buffer_get_has_selection (buffer));
  assert_primary ("efgh");

  /* So does replacing all text */
  select_range (buffer, 2, 0);
  assert_primary ("ij");
  gtk_text_buffer_set_text (buffer, "Something else", -1);
  assert_primary ("ij");

  gtk_text_buffer_remove_selection_clipboard (buffer, get_primary ());
  g_object_unref (buffer);
}

static void
test_text_buffer_no_steal (void)
{
  GtkTextBuffer *buffer;

  if (skip_without_focus ())
    return;

  set_primary ("other");
  buffer = create_buffer ("Hello world");

  /* Losing PRIMARY deselects, and nothing claims it back */
  select_range (buffer, 11, 6);
  assert_primary ("world");
  set_primary ("other");
  g_assert_false (gtk_text_buffer_get_has_selection (buffer));
  place_cursor (buffer, 0);
  gtk_text_buffer_set_text (buffer, "", -1);
  assert_primary ("other");

  /* Selections that never owned PRIMARY don't take it when they go away */
  gtk_text_buffer_remove_selection_clipboard (buffer, get_primary ());
  gtk_text_buffer_set_text (buffer, "Hello world", -1);
  select_range (buffer, 5, 0);
  gtk_text_buffer_add_selection_clipboard (buffer, get_primary ());
  assert_primary ("other");
  place_cursor (buffer, 0);
  assert_primary ("other");
  select_range (buffer, 5, 0);
  assert_primary ("Hello");

  gtk_text_buffer_remove_selection_clipboard (buffer, get_primary ());
  g_object_unref (buffer);
}

static void
test_text_buffer_remove_clipboard (void)
{
  GtkTextBuffer *buffer;

  if (skip_without_focus ())
    return;

  set_primary ("other");
  buffer = create_buffer ("Hello world");

  select_range (buffer, 11, 6);
  assert_primary ("world");

  /* What happens when the last view goes away */
  gtk_text_buffer_remove_selection_clipboard (buffer, get_primary ());
  g_assert_false (gtk_text_buffer_get_has_selection (buffer));
  assert_primary ("world");

  g_object_unref (buffer);
  assert_primary ("world");
}

static void
paste_done (GtkTextBuffer *buffer,
            GdkClipboard  *clipboard,
            gboolean      *done)
{
  *done = TRUE;
  g_main_context_wakeup (NULL);
}

static void
test_text_buffer_paste (void)
{
  GtkTextBuffer *buffer, *other;
  GtkTextIter start, end;
  gboolean done = FALSE;
  char *text;
  guint id;

  if (skip_without_focus ())
    return;

  set_primary ("other");
  buffer = create_buffer ("Hello world");
  other = gtk_text_buffer_new (NULL);

  select_range (buffer, 5, 0);
  place_cursor (buffer, 0);

  g_signal_connect (other, "paste-done", G_CALLBACK (paste_done), &done);
  id = g_timeout_add_seconds (5, timed_out, (gpointer) "the paste");
  gtk_text_buffer_paste_clipboard (other, get_primary (), NULL, TRUE);
  while (!done)
    g_main_context_iteration (NULL, TRUE);
  g_source_remove (id);

  gtk_text_buffer_get_bounds (other, &start, &end);
  text = gtk_text_buffer_get_text (other, &start, &end, FALSE);
  g_assert_cmpstr (text, ==, "Hello");
  g_free (text);

  g_object_unref (other);
  gtk_text_buffer_remove_selection_clipboard (buffer, get_primary ());
  g_object_unref (buffer);
}

static void
test_label (void)
{
  GtkWidget *label;

  if (skip_without_focus ())
    return;

  set_primary ("other");
  label = g_object_ref_sink (gtk_label_new ("Selectable label text"));
  gtk_label_set_selectable (GTK_LABEL (label), TRUE);

  gtk_label_select_region (GTK_LABEL (label), 0, 10);
  assert_primary ("Selectable");

  gtk_label_select_region (GTK_LABEL (label), 3, 3);
  assert_primary ("Selectable");

  gtk_label_select_region (GTK_LABEL (label), 16, 11);
  assert_primary ("label");

  gtk_label_set_text (GTK_LABEL (label), "Something else");
  assert_primary ("label");

  /* Losing PRIMARY deselects, and nothing claims it back */
  gtk_label_select_region (GTK_LABEL (label), 0, 9);
  assert_primary ("Something");
  set_primary ("other");
  gtk_label_select_region (GTK_LABEL (label), 0, 0);
  assert_primary ("other");

  gtk_label_select_region (GTK_LABEL (label), 10, 14);
  assert_primary ("else");
  g_object_unref (label);
  assert_primary ("else");
}

static void
label_destroyed (GtkLabel *label)
{
  /* After dispose, the label must not hand out a provider that
   * would outlive it
   */
  gtk_label_select_region (label, 0, 5);
  gtk_label_select_region (label, 0, 0);
  gtk_label_select_region (label, 6, 11);
}

static void
test_label_dispose (void)
{
  GtkWidget *label;
  char *text;

  if (skip_without_focus ())
    return;

  set_primary ("other");
  label = g_object_ref_sink (gtk_label_new ("Label being destroyed"));
  gtk_label_set_selectable (GTK_LABEL (label), TRUE);
  g_signal_connect (label, "destroy", G_CALLBACK (label_destroyed), NULL);

  g_object_run_dispose (G_OBJECT (label));
  g_object_unref (label);

  text = read_primary ();
  g_free (text);
}

/* GtkText only claims PRIMARY while it is realized and has the focus */
static GtkWidget *
create_text (GtkWidget  **window,
             const char  *contents)
{
  GtkWidget *text;

  *window = gtk_window_new ();
  text = gtk_text_new ();
  gtk_window_set_child (GTK_WINDOW (*window), text);
  gtk_editable_set_text (GTK_EDITABLE (text), contents);

  gtk_widget_realize (text);
  gtk_widget_set_has_focus (text, TRUE);

  return text;
}

static void
test_text (void)
{
  GtkWidget *window, *text;

  if (skip_without_focus ())
    return;

  set_primary ("other");
  text = create_text (&window, "Hello primary selection world");

  gtk_editable_select_region (GTK_EDITABLE (text), 6, 23);
  assert_primary ("primary selection");

  gtk_editable_select_region (GTK_EDITABLE (text), 3, 3);
  assert_primary ("primary selection");

  /* Backwards selections */
  gtk_editable_select_region (GTK_EDITABLE (text), 5, 0);
  assert_primary ("Hello");
  gtk_editable_set_position (GTK_EDITABLE (text), 0);
  assert_primary ("Hello");

  /* Deleting a part of the selection keeps it alive */
  gtk_editable_select_region (GTK_EDITABLE (text), 6, 23);
  gtk_editable_delete_text (GTK_EDITABLE (text), 0, 14);
  assert_primary ("selection");

  /* Deleting all of it keeps the text */
  g_signal_emit_by_name (text, "backspace");
  g_assert_cmpstr (gtk_editable_get_text (GTK_EDITABLE (text)), ==, " world");
  assert_primary ("selection");

  /* So does replacing all text */
  gtk_editable_select_region (GTK_EDITABLE (text), 1, 6);
  gtk_editable_set_text (GTK_EDITABLE (text), "Something else");
  assert_primary ("world");

  gtk_window_destroy (GTK_WINDOW (window));
}

static void
test_text_no_steal (void)
{
  GtkWidget *window, *text;
  int start, end;

  if (skip_without_focus ())
    return;

  set_primary ("other");
  text = create_text (&window, "Hello world");

  /* Losing PRIMARY deselects at the cursor, and nothing claims it back */
  gtk_editable_select_region (GTK_EDITABLE (text), 0, 5);
  assert_primary ("Hello");
  set_primary ("other");
  g_assert_false (gtk_editable_get_selection_bounds (GTK_EDITABLE (text), &start, &end));
  g_assert_cmpint (gtk_editable_get_position (GTK_EDITABLE (text)), ==, 5);
  gtk_editable_delete_text (GTK_EDITABLE (text), 0, -1);
  assert_primary ("other");

  /* Selections that never owned PRIMARY don't take it when they go away */
  gtk_editable_set_text (GTK_EDITABLE (text), "Hello world");
  gtk_widget_set_has_focus (text, FALSE);
  gtk_editable_select_region (GTK_EDITABLE (text), 6, 11);
  gtk_widget_set_has_focus (text, TRUE);
  assert_primary ("other");
  gtk_editable_set_position (GTK_EDITABLE (text), 0);
  assert_primary ("other");
  gtk_editable_select_region (GTK_EDITABLE (text), 6, 11);
  assert_primary ("world");

  gtk_window_destroy (GTK_WINDOW (window));
}

static void
test_text_unrealize (void)
{
  GtkWidget *window, *text;
  int start, end;

  if (skip_without_focus ())
    return;

  set_primary ("other");
  text = create_text (&window, "Hello world");

  gtk_editable_select_region (GTK_EDITABLE (text), 0, 5);
  assert_primary ("Hello");

  gtk_widget_unrealize (text);
  g_assert_false (gtk_editable_get_selection_bounds (GTK_EDITABLE (text), &start, &end));
  assert_primary ("Hello");

  gtk_window_destroy (GTK_WINDOW (window));
  assert_primary ("Hello");
}

static void
test_text_set_buffer (void)
{
  GtkWidget *window, *text;
  GtkEntryBuffer *buffer;

  if (skip_without_focus ())
    return;

  set_primary ("other");
  text = create_text (&window, "Hello world");

  gtk_editable_select_region (GTK_EDITABLE (text), 0, 5);
  buffer = gtk_entry_buffer_new ("Something else", -1);
  gtk_text_set_buffer (GTK_TEXT (text), buffer);
  g_object_unref (buffer);
  assert_primary ("Hello");

  gtk_window_destroy (GTK_WINDOW (window));
}

static void
test_text_password (void)
{
  GtkWidget *window, *text;
  GtkEntryBuffer *buffer;

  if (skip_without_focus ())
    return;

  /* Invisible text */
  set_primary ("other");
  text = create_text (&window, "secret");
  gtk_text_set_visibility (GTK_TEXT (text), FALSE);
  gtk_editable_select_region (GTK_EDITABLE (text), 0, 3);
  gtk_editable_select_region (GTK_EDITABLE (text), 0, 0);
  assert_primary ("(none)");
  gtk_window_destroy (GTK_WINDOW (window));

  /* Revealed passwords */
  set_primary ("other");
  text = create_text (&window, "secret");
  gtk_text_set_visibility (GTK_TEXT (text), FALSE);
  gtk_text_set_visibility (GTK_TEXT (text), TRUE);
  gtk_editable_select_region (GTK_EDITABLE (text), 0, 3);
  assert_primary ("sec");
  gtk_editable_select_region (GTK_EDITABLE (text), 0, 0);
  assert_primary ("(none)");
  gtk_window_destroy (GTK_WINDOW (window));

  set_primary ("other");
  text = create_text (&window, "secret");
  gtk_text_set_input_purpose (GTK_TEXT (text), GTK_INPUT_PURPOSE_PASSWORD);
  gtk_editable_select_region (GTK_EDITABLE (text), 0, 3);
  assert_primary ("sec");
  gtk_editable_delete_selection (GTK_EDITABLE (text));
  assert_primary ("(none)");
  gtk_window_destroy (GTK_WINDOW (window));

  set_primary ("other");
  text = create_text (&window, "");
  buffer = gtk_password_entry_buffer_new ();
  gtk_text_set_buffer (GTK_TEXT (text), buffer);
  g_object_unref (buffer);
  gtk_editable_set_text (GTK_EDITABLE (text), "secret");
  gtk_editable_select_region (GTK_EDITABLE (text), 0, 3);
  assert_primary ("sec");
  gtk_editable_select_region (GTK_EDITABLE (text), 0, 0);
  assert_primary ("(none)");

  /* ...also when the buffer is replaced */
  gtk_editable_select_region (GTK_EDITABLE (text), 0, 3);
  assert_primary ("sec");
  buffer = gtk_entry_buffer_new ("Something else", -1);
  gtk_text_set_buffer (GTK_TEXT (text), buffer);
  g_object_unref (buffer);
  assert_primary ("(none)");
  gtk_window_destroy (GTK_WINDOW (window));
}

static gboolean
stop_waiting (gpointer data)
{
  gboolean *waiting = data;

  *waiting = FALSE;
  g_main_context_wakeup (NULL);

  return G_SOURCE_REMOVE;
}

int
main (int   argc,
      char *argv[])
{
  GtkWidget *window;
  gboolean waiting = TRUE;
  guint id;
  int result;

  gtk_test_init (&argc, &argv);

  /* On Wayland, a client can only own PRIMARY while it has the focus */
  window = gtk_window_new ();
  gtk_window_present (GTK_WINDOW (window));
  id = g_timeout_add_seconds (10, stop_waiting, &waiting);
  while (waiting && !gtk_window_is_active (GTK_WINDOW (window)))
    g_main_context_iteration (NULL, TRUE);
  if (waiting)
    g_source_remove (id);
  have_focus = gtk_window_is_active (GTK_WINDOW (window));

  g_test_add_func ("/primary-selection/text-buffer/deselect", test_text_buffer_deselect);
  g_test_add_func ("/primary-selection/text-buffer/delete", test_text_buffer_delete);
  g_test_add_func ("/primary-selection/text-buffer/no-steal", test_text_buffer_no_steal);
  g_test_add_func ("/primary-selection/text-buffer/remove-clipboard", test_text_buffer_remove_clipboard);
  g_test_add_func ("/primary-selection/text-buffer/paste", test_text_buffer_paste);
  g_test_add_func ("/primary-selection/label", test_label);
  g_test_add_func ("/primary-selection/label/dispose", test_label_dispose);
  g_test_add_func ("/primary-selection/text", test_text);
  g_test_add_func ("/primary-selection/text/no-steal", test_text_no_steal);
  g_test_add_func ("/primary-selection/text/unrealize", test_text_unrealize);
  g_test_add_func ("/primary-selection/text/set-buffer", test_text_set_buffer);
  g_test_add_func ("/primary-selection/text/password", test_text_password);

  result = g_test_run ();

  gtk_window_destroy (GTK_WINDOW (window));

  return result;
}
