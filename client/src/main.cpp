#include <adwaita.h>

static void on_activate(GApplication *app, gpointer user_data) {
  // Ensure resources are loaded
  GError *error = NULL;
  GtkBuilder *builder =
      gtk_builder_new_from_resource("/com/accel/lsw/ui/installer.ui");
  if (!builder) {
    g_error("Failed to load UI from resource");
    return;
  }

  GtkWindow *window = GTK_WINDOW(gtk_builder_get_object(builder, "window"));
  gtk_window_set_default_size(window, 800, 600);
  gtk_window_set_title(window, "LSW Installer");

  gtk_window_set_application(window, GTK_APPLICATION(app));
  gtk_window_present(window);

  g_object_unref(builder);
}

int main(int argc, char **argv) {
  g_autoptr(AdwApplication) app =
      adw_application_new("com.accel.lsw", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  return g_application_run(G_APPLICATION(app), argc, argv);
}