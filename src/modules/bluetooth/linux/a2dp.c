/*
 *
 * Copyright 2017 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <gio/gio.h>
#pragma GCC diagnostic pop
#include <glib.h>

#include "core.h"
#include "a2dp.h"

const gchar *A2DP_SOURCE_UUID = "0000110A-0000-1000-8000-00805F9B34FB";
artik_bt_a2dp_source *_endpoint;
static guint registration_id;

/* Introspection data for the service we are exporting */
static const gchar _introspection_xml[] =
"<node>"
	"<interface name='org.bluez.MediaEndpoint1'>"
		"<method name='SelectConfiguration'>"
			"<arg type='ay' name='capabilities' direction='in'/>"
			"<arg type='ay' name='configuration' direction='out'/>"
		"</method>"
		"<method name='SetConfiguration'>"
			"<arg type='o' name='transport' direction='in'/>"
			"<arg type='a{sv}' name='properties' direction='in'/>"
		"</method>"
		"<method name='ClearConfiguration'>"
			"<arg type='o' name='transport' direction='in'/>"
		"</method>"
		"<method name='Release'>"
		"</method>"
	"</interface>"
"</node>";

static GDBusNodeInfo *_introspection_data;

static void _handle_select_configuration(GVariant *parameters,
		GDBusMethodInvocation *invocation)
{
	unsigned char *cap, *return_cap;
	int i = 0;
	int len = 0;
	GVariantBuilder *p_builder = NULL;

	g_variant_get(parameters, "(ay)", &cap);
	if (_endpoint->select_callback) {
		return_cap = _endpoint->select_callback(cap, &len);

		p_builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));

		for (i = 0; i < len; i++)
			g_variant_builder_add(p_builder, "y", return_cap[i]);

		g_dbus_method_invocation_return_value(invocation,
				g_variant_new("(ay)", p_builder));
		g_variant_builder_unref(p_builder);
	}
}

static void _handle_set_configuration(GVariant *parameters,
		GDBusMethodInvocation *invocation)
{
	artik_bt_a2dp_source_property properties;
	GVariant *_g_property = NULL;
	GVariant *value = NULL, *internal = NULL;
	GVariantIter *iter = NULL;
	gchar *key = NULL;
	gint i = 0, interval_len = 0;

	memset(&properties, 0, sizeof(artik_bt_a2dp_source_property));

	if (_endpoint->transport_path)
		free(_endpoint->transport_path);

	g_variant_get(parameters, "(o@a{sv})", &_endpoint->transport_path,
		&_g_property);
	g_dbus_method_invocation_return_value(invocation, NULL);

	g_variant_get(_g_property, "a{sv}", &iter);
	while (g_variant_iter_loop(iter, "{&sv}", &key, &value)) {
		if (g_strcmp0(key, "Device") == 0) {
			g_variant_get(value, "&o", &properties.device);
			log_dbg("device is: %s\n", properties.device);
		} else if (g_strcmp0(key, "UUID") == 0) {
			g_variant_get(value, "&s", &properties.uuid);
			log_dbg("uuid is: %s\n", properties.uuid);
		} else if (g_strcmp0(key, "Codec") == 0)
			g_variant_get(value, "y", &properties.codec);
		else if (g_strcmp0(key, "Configuration") == 0) {
			interval_len = g_variant_n_children(value);
			if (interval_len > 0) {
				if (!properties.configuration) {
					properties.configuration = (unsigned char *) malloc
						((sizeof(unsigned char) * interval_len) + 1);
					for (i = 0; i < interval_len; i++) {
						internal = g_variant_get_child_value(value, i);
						g_variant_get(internal, "y",
							&properties.configuration[i]);
						g_variant_unref(internal);
					}
					properties.configuration[interval_len] = '\0';
				}
			}
		} else if (g_strcmp0(key, "State") == 0) {
			g_variant_get(value, "&s", &properties.state);
			log_dbg("state is: %s\n", properties.state);
		}
		g_variant_unref(value);
	}
	g_variant_iter_free(iter);

	if (_endpoint->set_callback)
		_endpoint->set_callback(&properties);

	g_variant_unref(_g_property);

	if (properties.configuration)
		free(properties.configuration);
}

static void _handle_clear_configuration(void)
{
	if (_endpoint->clear_callback)
		_endpoint->clear_callback();
}

static void handle_method_call(GDBusConnection *connection,
		const gchar *sender, const gchar *object_path,
		const gchar *interface_name, const gchar *method_name,
		GVariant *parameters, GDBusMethodInvocation *invocation,
		gpointer user_data)
{
	if (g_strcmp0(method_name, "SelectConfiguration") == 0)
		_handle_select_configuration(parameters, invocation);
	if (g_strcmp0(method_name, "SetConfiguration") == 0)
		_handle_set_configuration(parameters, invocation);
	if (g_strcmp0(method_name, "ClearConfiguration") == 0)
		_handle_clear_configuration();
}

static artik_error _get_media_transport_path(void)
{
	GVariant *obj1 = NULL, *ar1 = NULL, *ar2 = NULL;
	GVariantIter *iter1 = NULL, *iter2 = NULL;
	char *dev_path = NULL, *itf = NULL;
	bool is_find = false;

	artik_error err = _get_managed_objects(&obj1);

	if (err != S_OK)
		return err;

	g_variant_get(obj1, "(a{oa{sa{sv}}})", &iter1);
	while (g_variant_iter_loop(iter1, "{&o@a{sa{sv}}}", &dev_path, &ar1)) {
		g_variant_get(ar1, "a{sa{sv}}", &iter2);
		while (g_variant_iter_loop(iter2, "{&s@a{sv}}", &itf, &ar2)) {
			if (strncmp(itf, DBUS_IF_MEDIA_TRANSPORT1,
				strlen(DBUS_IF_MEDIA_TRANSPORT1)) == 0) {
				strncpy(a2dp_path, dev_path, BT_TRANSPORT_PATH_LEN);
				is_find = true;
			}
			if (is_find) {
				g_variant_unref(ar2);
				break;
			}
		}
		g_variant_iter_free(iter2);
		if (is_find) {
			g_variant_unref(ar1);
			break;
		}
	}

	g_variant_iter_free(iter1);
	g_variant_unref(obj1);
	if (is_find) {
		log_dbg("media_transport_path[%s]\n", a2dp_path);
		return S_OK;
	}
	return E_BT_ERROR;
}

static artik_error _get_property(const char *property, GVariant **v)
{
	artik_error e = E_NOT_INITIALIZED;

	if (strlen(a2dp_path) == 0)
		_get_media_transport_path();
	if (strlen(a2dp_path) != 0) {
		GVariant *result = NULL;
		GError *error = NULL;

		result = g_dbus_connection_call_sync(
			hci.conn,
			DBUS_BLUEZ_BUS,
			a2dp_path,
			DBUS_IF_PROPERTIES,
			"Get",
			g_variant_new("(ss)", DBUS_IF_MEDIA_TRANSPORT1, property),
			G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE,
			G_MAXINT, NULL, &error);

		e = bt_check_error(error);
		if (e == S_OK) {
			g_variant_get(result, "(v)", v);
			g_variant_unref(result);
		}
	}
	return e;
}

static const GDBusInterfaceVTable _interface_vtable = {
		.method_call = handle_method_call,
		.get_property = NULL,
		.set_property = NULL };

static void bt_a2dp_source_create(
		unsigned char codec, bool delay_reporting, const char *path,
		const unsigned char *capabilities, int cap_size)
{
	if (!_endpoint) {
		_endpoint = (artik_bt_a2dp_source *) malloc
				(sizeof(artik_bt_a2dp_source));
		if (_endpoint) {
			memset(_endpoint, 0, sizeof(artik_bt_a2dp_source));
			_endpoint->codec = codec;
			_endpoint->delay_reporting = delay_reporting;
			_endpoint->endpoint_path = (char *) malloc(strlen(path) + 1);
			if (_endpoint->endpoint_path)
				strncpy(_endpoint->endpoint_path, path, strlen(path) + 1);
			else {
				free(_endpoint);
				_endpoint = NULL;
				return;
			}
			if (cap_size) {
				_endpoint->capabilities = (char *) malloc(cap_size);
				if (_endpoint->capabilities) {
					memcpy(_endpoint->capabilities, capabilities, cap_size);
					_endpoint->cap_size = cap_size;
				} else {
					free(_endpoint->endpoint_path);
					free(_endpoint);
					_endpoint = NULL;
					return;
				}
			}
		}
	}
}
static void bt_a2dp_source_destroy(void)
{
	if (_endpoint) {
		if (_endpoint->endpoint_path)
			free(_endpoint->endpoint_path);
		if (_endpoint->capabilities)
			free(_endpoint->capabilities);
		if (_endpoint->transport_path)
			free(_endpoint->transport_path);
		free(_endpoint);
		_endpoint = NULL;
	}
}

artik_error bt_a2dp_source_register(unsigned char codec,
		bool delay_reporting, const char *path,
		const unsigned char *capabilities, int cap_size)
{
	GVariant *result;
	GError *error = NULL;
	GVariantBuilder *builder = NULL;
	GVariantBuilder *cap_builder = NULL;
	int i = 0;

	bt_a2dp_source_create(codec, delay_reporting, path, capabilities, cap_size);

	builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	cap_builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));

	g_variant_builder_add(builder, "{sv}", "UUID",
			g_variant_new_string(A2DP_SOURCE_UUID));
	g_variant_builder_add(builder, "{sv}", "Codec",
			g_variant_new_byte(_endpoint->codec));
	g_variant_builder_add(builder, "{sv}", "DelayReporting",
			g_variant_new_boolean(_endpoint->delay_reporting));
	for (i = 0; i < _endpoint->cap_size; i++)
		g_variant_builder_add(cap_builder, "y", _endpoint->capabilities[i]);
	g_variant_builder_add(builder, "{sv}", "Capabilities",
			g_variant_new("ay", cap_builder));
	g_variant_builder_unref(cap_builder);

	result = g_dbus_connection_call_sync(hci.conn,
		DBUS_BLUEZ_BUS,
		DBUS_BLUEZ_OBJECT_PATH_HCI0,
		DBUS_IF_MEDIA1,
		"RegisterEndpoint",
		g_variant_new("(oa{sv})", _endpoint->endpoint_path, builder),
		NULL, G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL, &error);

	g_variant_builder_unref(builder);

	if (error) {
		log_dbg("Register endpoint failed :%s\n", error->message);
		g_clear_error(&error);
		return E_BT_ERROR;
	}

	g_variant_unref(result);

	_introspection_data = g_dbus_node_info_new_for_xml(_introspection_xml,
			NULL);

	GDBusInterfaceInfo *interface = g_dbus_node_info_lookup_interface(
			_introspection_data, DBUS_IF_MEDIA_ENDPOINT1);

	registration_id = g_dbus_connection_register_object(hci.conn,
			_endpoint->endpoint_path, interface,
			&_interface_vtable, NULL, NULL, &error);
	if (error) {
		log_dbg("g_dbus_connection_register_object failed :%s\n", error->message);
		g_clear_error(&error);
		return E_BT_ERROR;
	}

	log_dbg("registration id : %d\n", registration_id);

	return S_OK;
}

artik_error bt_a2dp_source_unregister(void)
{
	GVariant *result;
	GError *error = NULL;

	/*TODO:unregister the object path */
	result = g_dbus_connection_call_sync(hci.conn,
		DBUS_BLUEZ_BUS,
		DBUS_BLUEZ_OBJECT_PATH_HCI0,
		DBUS_IF_MEDIA1,
		"UnregisterEndpoint",
		g_variant_new("(o)", _endpoint->endpoint_path),
		NULL, G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL, &error);

	if (error) {
		log_dbg("Register endpoint failed :%s\n", error->message);
		g_clear_error(&error);
		return E_BT_ERROR;
	}

	bt_a2dp_source_destroy();
	g_variant_unref(result);
	g_dbus_connection_unregister_object(hci.conn, registration_id);
	g_dbus_node_info_unref(_introspection_data);
	return S_OK;
}

artik_error bt_a2dp_source_acquire(int *fd,
		unsigned short *mtu_read, unsigned short *mtu_write)
{
	GVariant *result = NULL;
	GVariant *v;
	GError *error = NULL;
	char *state;

	if (_endpoint->transport_path) {
		result = g_dbus_connection_call_sync(
				hci.conn,
				DBUS_BLUEZ_BUS,
				_endpoint->transport_path,
				DBUS_IF_PROPERTIES,
				"Get",
				g_variant_new("(ss)", DBUS_IF_MEDIA_TRANSPORT1, "State"),
				G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE,
				G_MAXINT, NULL, &error);

		bt_check_error(error);

		g_variant_get(result, "(v)", &v);
		g_variant_get(v, "s", &state);

		g_variant_unref(result);
		g_variant_unref(v);
		if (!strncmp(state, STATE_ACTIVE, strlen(STATE_ACTIVE)))
			return E_BT_ERROR;
		if (!strncmp(state, STATE_PENDING, strlen(STATE_PENDING))) {
			result = g_dbus_connection_call_sync(hci.conn,
				DBUS_BLUEZ_BUS,
				_endpoint->transport_path,
				DBUS_IF_MEDIA_TRANSPORT1,
				"TryAcquire", NULL,
				G_VARIANT_TYPE("(hqq)"),
				G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL, &error);
		} else if (!strncmp(state, STATE_IDLE, strlen(STATE_IDLE))) {
			result = g_dbus_connection_call_sync(hci.conn,
				DBUS_BLUEZ_BUS,
				_endpoint->transport_path,
				DBUS_IF_MEDIA_TRANSPORT1,
				"Acquire", NULL,
				G_VARIANT_TYPE("(hqq)"), G_DBUS_CALL_FLAGS_NONE,
				G_MAXINT, NULL, &error);
		}
		if (error) {
			log_dbg("Acquire failed :%s\n", error->message);
			g_clear_error(&error);
			return E_BT_ERROR;
		}
		if (result) {
			g_variant_get(result, "(hqq)", fd, mtu_read, mtu_write);
			g_variant_unref(result);
			return S_OK;
		} else
			return E_BT_ERROR;
	}
	return E_INVALID_VALUE;
}

artik_error bt_a2dp_source_get_properties(
		artik_bt_a2dp_source_property **properties)
{
	(*properties) = (artik_bt_a2dp_source_property *) malloc(
				sizeof(artik_bt_a2dp_source_property));
	memset(*properties, 0, sizeof(artik_bt_a2dp_source_property));
	GVariant *v, *tuple;
	GError *error = NULL;

	gchar *device;
	gchar *uuid;
	unsigned char codec;
	unsigned char *configuration;
	gchar *state;

	if (_endpoint->transport_path) {
		tuple = g_dbus_connection_call_sync(hci.conn, DBUS_BLUEZ_BUS,
				_endpoint->transport_path,
				DBUS_IF_PROPERTIES, "Get",
				g_variant_new("(ss)", DBUS_IF_MEDIA_TRANSPORT1, "Device"),
				G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL,
				&error);

		bt_check_error(error);

		g_variant_get(tuple, "(v)", &v);
		g_variant_get(v, "o", &device);

		(*properties)->device = (char *) malloc(strlen(device) + 1);
		strncpy((*properties)->device, device, strlen(device) + 1);

		g_variant_unref(v);
		g_variant_unref(tuple);

		tuple = g_dbus_connection_call_sync(hci.conn, DBUS_BLUEZ_BUS,
				_endpoint->transport_path,
				DBUS_IF_PROPERTIES, "Get",
				g_variant_new("(ss)", DBUS_IF_MEDIA_TRANSPORT1, "UUID"),
				G_VARIANT_TYPE("(v)"),
				G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL, &error);

		bt_check_error(error);

		g_variant_get(tuple, "(v)", &v);
		g_variant_get(v, "s", &uuid);
		(*properties)->uuid = (char *) malloc(strlen(uuid) + 1);
		strncpy((*properties)->uuid, uuid, strlen(uuid) + 1);

		g_variant_unref(v);
		g_variant_unref(tuple);

		tuple = g_dbus_connection_call_sync(hci.conn, DBUS_BLUEZ_BUS,
				_endpoint->transport_path,
				DBUS_IF_PROPERTIES, "Get",
				g_variant_new("(ss)", DBUS_IF_MEDIA_TRANSPORT1, "Codec"),
				G_VARIANT_TYPE("(v)"),
				G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL, &error);

		bt_check_error(error);

		g_variant_get(tuple, "(v)", &v);
		g_variant_get(v, "y", &codec);
		(*properties)->codec = codec;
		g_variant_unref(v);
		g_variant_unref(tuple);

		tuple = g_dbus_connection_call_sync(hci.conn, DBUS_BLUEZ_BUS,
				_endpoint->transport_path,
				DBUS_IF_PROPERTIES, "Get",
				g_variant_new("(ss)",
					DBUS_IF_MEDIA_TRANSPORT1, "Configuration"),
				G_VARIANT_TYPE("(v)"),
				G_DBUS_CALL_FLAGS_NONE, G_MAXINT, NULL, &error);

		bt_check_error(error);

		g_variant_get(tuple, "(v)", &v);
		g_variant_get(v, "ay", &configuration);

		(*properties)->configuration = (unsigned char *) malloc
			(strlen((char *)configuration) + 1);
		memcpy((*properties)->configuration, configuration, strlen((char *)configuration) + 1);

		g_variant_unref(v);
		g_variant_unref(tuple);

		tuple = g_dbus_connection_call_sync(
				hci.conn,
				DBUS_BLUEZ_BUS,
				_endpoint->transport_path,
				DBUS_IF_PROPERTIES,
				"Get",
				g_variant_new("(ss)", DBUS_IF_MEDIA_TRANSPORT1, "State"),
				G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE,
				G_MAXINT, NULL, &error);

		bt_check_error(error);

		g_variant_get(tuple, "(v)", &v);
		g_variant_get(v, "s", &state);
		(*properties)->state = (char *) malloc(strlen(state)+1);
		strncpy((*properties)->state, state, strlen(state) + 1);
		log_dbg("Current state is :%s\n", (*properties)->state);

		return S_OK;
	} else
		return E_BT_ERROR;
}

artik_error bt_a2dp_source_release(void)
{
	GVariant *result;
	GError *error = NULL;

	if (_endpoint->transport_path) {
		result = g_dbus_connection_call_sync(hci.conn,
				DBUS_BLUEZ_BUS,
				_endpoint->transport_path,
				DBUS_IF_MEDIA_TRANSPORT1, "Release", NULL,
				NULL, G_DBUS_CALL_FLAGS_NONE,
				G_MAXINT, NULL, &error);

		if (error) {
			log_dbg("Release failed :%s\n", error->message);
			g_clear_error(&error);
			return E_BT_ERROR;
		}

		g_variant_unref(result);

		return S_OK;
	} else
		return E_BT_ERROR;
}

artik_error bt_a2dp_source_set_callback(select_config_callback select_func,
		set_config_callback set_func, clear_config_callback clear_func)
{
	if (_endpoint) {
		_endpoint->select_callback = select_func;
		_endpoint->set_callback = set_func;
		_endpoint->clear_callback = clear_func;
		return S_OK;
	} else {
		return E_INVALID_VALUE;
	}
}

artik_error bt_a2dp_source_get_state(char **state)
{
	GVariant *v = NULL;
	artik_error e = _get_property("State", &v);

	if (e == S_OK && v) {
		g_variant_get(v, "s", state);
		g_variant_unref(v);
		log_dbg("Current streaming state is :%s\n", *state);
	}
	return e;
}
