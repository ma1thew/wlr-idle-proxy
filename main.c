#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dbus/dbus.h>

#include "dbus/dbus-protocol.h"
#include "dbus/dbus-shared.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"
#include "wayland-client-core.h"
#include "wayland-util.h"
#include <wayland-client.h>

const char *introspection_data =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection "
    "1.0//EN"
    "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">"
    "<node name=\"/org/freedesktop/ScreenSaver\">"
    "    <interface name=\"org.freedesktop.ScreenSaver\">"
    "    <tp:docstring>"
    "        The Idle Inhibition Service manages the inhibition requests."
    "    </tp:docstring>"
    "    <method name=\"Inhibit\">"
    "      <tp:docstring>Inhibit idleness for the caller "
    "application.</tp:docstring>"
    "      <arg name=\"application_name\" type=\"s\" direction=\"in\">"
    "         <tp:docstring>A unique identifier for the application, usually a "
    "reverse domain (such as 'org.freedesktop.example').</tp:docstring>"
    "      </arg>"
    "      <arg name=\"reason_for_inhibit\" type=\"s\" direction=\"in\">"
    "        <tp:docstring>A human-readable and possibly translated string "
    "explaining the reason why idleness is inhibited (such as 'Playing a "
    "movie').</tp:docstring>"
    "      </arg>"
    "      <arg name=\"cookie\" type=\"u\" direction=\"out\">"
    "        <tp:docstring>A cookie uniquely representing the inhibition "
    "request, to be passed to UnInhibit when done.</tp:docstring>"
    "      </arg>"
    "    </method>"
    "    <method name=\"UnInhibit\">"
    "      <tp:docstring>Disable inhibit idleness for the caller "
    "application.</tp:docstring>"
    "      <arg name=\"cookie\" type=\"u\" direction=\"in\">"
    "        <tp:docstring>A cookie representing the inhibition request, as "
    "returned by the 'Inhibit' function.</tp:docstring>"
    "      </arg>"
    "    </method>"
    "  </interface>"
    "</node>";

struct Inhibitor {
  struct wl_list link;
  char *application_name;
  char *reason_for_inhibit;
  uint32_t cookie;
};

static struct wl_list inhibitor_list;

static struct wl_surface *surface = NULL;
struct wl_display *display = NULL;
static struct wl_compositor *compositor = NULL;
static struct wl_seat *seat = NULL;
static struct zwp_idle_inhibit_manager_v1 *idle_inhibit_manager = NULL;
static struct zwp_idle_inhibitor_v1 *idle_inhibitor = NULL;

static void handle_global(void *data, struct wl_registry *registry,
                          uint32_t name, const char *interface,
                          uint32_t version) {
  if (strcmp(interface, "wl_compositor") == 0) {
    compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
  } else if (strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) ==
             0) {
    idle_inhibit_manager = wl_registry_bind(
        registry, name, &zwp_idle_inhibit_manager_v1_interface, 1);
  } else if (strcmp(interface, wl_seat_interface.name) == 0) {
    seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
  }
}

static void handle_global_remove(void *data, struct wl_registry *registry,
                                 uint32_t name) {
  // who cares
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

static DBusHandlerResult handle_message(DBusConnection *conn,
                                        DBusMessage *message, void *data) {
  const char *interface_name = dbus_message_get_interface(message);
  const char *member_name = dbus_message_get_member(message);
  if (!strncmp(interface_name, "org.freedesktop.DBus.Introspectable",
               strlen("org.freedesktop.DBus.Introspectable"))) {
    if (!strncmp(member_name, "Introspect", strlen("Introspect"))) {
      DBusMessage *reply = dbus_message_new_method_return(message);
      dbus_message_append_args(reply, DBUS_TYPE_STRING, &introspection_data,
                               DBUS_TYPE_INVALID);
      dbus_connection_send(conn, reply, NULL);
      dbus_message_unref(reply);
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  } else if (!strncmp(interface_name, "org.freedesktop.ScreenSaver",
                      strlen("org.freedesktop.ScreenSaver"))) {
    if (!strncmp(member_name, "Inhibit", strlen("Inhibit"))) {
      DBusMessage *reply = dbus_message_new_method_return(message);
      DBusError err;
      char *application_name;
      char *reason_for_inhibit;
      dbus_error_init(&err);
      struct Inhibitor *new_inhibitor = malloc(sizeof(struct Inhibitor));
      if (!new_inhibitor) {
        fprintf(stderr, "Memory allocation failed!");
        exit(EXIT_FAILURE);
      }
      dbus_message_get_args(message, &err, DBUS_TYPE_STRING, &application_name,
                            DBUS_TYPE_STRING, &reason_for_inhibit,
                            DBUS_TYPE_INVALID);
      if (dbus_error_is_set(&err)) {
        reply = dbus_message_new_error(message, "wrong_arguments",
                                       "Illegal arguments");
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
      }
      new_inhibitor->application_name = malloc(strlen(application_name) + 1);
      if (!new_inhibitor->application_name) {
        fprintf(stderr, "Memory allocation failed!");
        exit(EXIT_FAILURE);
      }
      strcpy(new_inhibitor->application_name, application_name);
      new_inhibitor->reason_for_inhibit =
          malloc(strlen(reason_for_inhibit) + 1);
      if (!new_inhibitor->reason_for_inhibit) {
        fprintf(stderr, "Memory allocation failed!");
        exit(EXIT_FAILURE);
      }
      strcpy(new_inhibitor->reason_for_inhibit, reason_for_inhibit);
      uint32_t cookie = rand();
      bool cookie_in_list = true;
      while (cookie_in_list) {
        cookie_in_list = false;
        struct Inhibitor *elm;
        wl_list_for_each(elm, &inhibitor_list, link) {
          if (elm->cookie == cookie) {
            cookie_in_list = true;
            cookie = rand();
            break;
          }
        }
      }
      new_inhibitor->cookie = cookie;
      wl_list_insert(inhibitor_list.prev, &new_inhibitor->link);
      reply = dbus_message_new_method_return(message);
      dbus_message_append_args(reply, DBUS_TYPE_UINT32, &cookie,
                               DBUS_TYPE_INVALID);
      dbus_connection_send(conn, reply, NULL);
      dbus_message_unref(reply);
      printf("Added inhibitor: %s, %s, %d\n", new_inhibitor->application_name,
             new_inhibitor->reason_for_inhibit, new_inhibitor->cookie);
      if (!wl_list_empty(&inhibitor_list) && idle_inhibitor == NULL) {
        printf("Began inhibiting over wayland...\n");
        idle_inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(
            idle_inhibit_manager, surface);
	if (wl_display_roundtrip(display) == -1) {
	  fprintf(stderr, "Failed to commit wayland events!");
	  exit(EXIT_FAILURE);
	}
      }
      return DBUS_HANDLER_RESULT_HANDLED;
    } else if (!strncmp(member_name, "UnInhibit", strlen("UnInhibit"))) {
      DBusMessage *reply = dbus_message_new_method_return(message);
      DBusError err;
      uint32_t cookie;
      struct Inhibitor *to_remove = NULL;
      struct Inhibitor *elm;
      dbus_error_init(&err);
      dbus_message_get_args(message, &err, DBUS_TYPE_UINT32, &cookie,
                            DBUS_TYPE_INVALID);
      if (dbus_error_is_set(&err)) {
        reply = dbus_message_new_error(message, "wrong_arguments",
                                       "Illegal arguments");
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
      }
      wl_list_for_each(elm, &inhibitor_list, link) {
        if (elm->cookie == cookie) {
          to_remove = elm;
          break;
        }
      }
      if (!to_remove) {
	printf("Recieved illegal cookie!");
        reply = dbus_message_new_error(message, "wrong_arguments",
                                       "Illegal cookie");
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
      }
      printf("Removed inhibitor: %s, %s, %d\n", to_remove->application_name,
             to_remove->reason_for_inhibit, to_remove->cookie);
      wl_list_remove(&to_remove->link);
      free(to_remove->application_name);
      free(to_remove->reason_for_inhibit);
      free(to_remove);
      if (wl_list_empty(&inhibitor_list) && idle_inhibitor != NULL) {
        printf("Stopped inhibiting over wayland...\n");
        zwp_idle_inhibitor_v1_destroy(idle_inhibitor);
        idle_inhibitor = NULL;
	if (wl_display_roundtrip(display) == -1) {
	  fprintf(stderr, "Failed to commit wayland events!");
	  exit(EXIT_FAILURE);
	}
      }
    }
  }
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int main(int argc, char **argv) {
  srand(time(NULL));
  display = wl_display_connect(NULL);
  if (display == NULL) {
    fprintf(stderr, "Failed to create display\n");
    return EXIT_FAILURE;
  }

  struct wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(display);

  if (compositor == NULL) {
    fprintf(stderr, "wl-compositor not available\n");
    return EXIT_FAILURE;
  }
  if (idle_inhibit_manager == NULL) {
    fprintf(stderr, "idle-inhibit not available\n");
    return EXIT_FAILURE;
  }

  surface = wl_compositor_create_surface(compositor);

  wl_surface_commit(surface);
  wl_display_roundtrip(display);

  DBusConnection *conn;
  DBusError err;
  DBusObjectPathVTable vtable;
  int ret;

  dbus_error_init(&err);
  conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "DBus connection error: %s\n", err.message);
    dbus_error_free(&err);
  }
  if (conn == NULL) {
    fprintf(stderr, "DBus connection failed.\n");
    exit(EXIT_FAILURE);
  }

  ret = dbus_bus_request_name(conn, "org.freedesktop.ScreenSaver",
                              DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "DBus name error: %s\n", err.message);
    dbus_error_free(&err);
  }
  if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
    fprintf(stderr, "DBus: Failed to become primary owner of "
                    "org.freedesktop.ScreenSaver\n");
    exit(EXIT_FAILURE);
  }

  vtable.message_function = handle_message;
  vtable.unregister_function = NULL;

  dbus_connection_try_register_object_path(conn, "/org/freedesktop/ScreenSaver",
                                           &vtable, NULL, &err);
  if (dbus_error_is_set(&err)) {
    fprintf(stderr, "DBus object error: %s\n", err.message);
    dbus_error_free(&err);
    exit(EXIT_FAILURE);
  }

  wl_list_init(&inhibitor_list);

  // TODO: wl_display_dispatch
  while (true) {
    dbus_connection_read_write_dispatch(conn, 1000);
  }

  return EXIT_SUCCESS;
}
