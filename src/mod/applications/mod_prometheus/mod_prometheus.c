/*
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2021
 *
 * Version: MPL 1.1
 *
 * mod_prometheus.c -- Prometheus data exporter module
 *
 */

#include <switch.h>
#include <prom.h>
#include <promhttp.h>

#define HTTP_BUFF_SIZE (SWITCH_RTP_MAX_BUF_LEN - 32)
#define KAZOO_NODES_COUNT "kazoo::nodes"
#define MY_EVENT_SOFIA_STATISTICS "sofia::statistics"

static prom_gauge_t *foo_gauge;
static prom_gauge_t *foo_gauge_number;
static prom_gauge_t *kazoo_nodes_gauge;

static struct MHD_Daemon *prometheus_daemon;

/* Prototypes */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_prometheus_shutdown);
SWITCH_MODULE_LOAD_FUNCTION(mod_prometheus_load);
SWITCH_MODULE_DEFINITION(mod_prometheus, mod_prometheus_load, mod_prometheus_shutdown, NULL);

static struct {
	int running;
	int debug;
	switch_memory_pool_t *pool;
	char *ip;
	switch_port_t port;
} globals;

static void kazoo_nodes_count_handler(switch_event_t *event) {
	char *count_str = switch_event_get_header(event, "kazoo-nodes-count");

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Kazoo nodes count has changed: kazoo-nodes-count='%s'\n", switch_str_nil(count_str));
	if (!zstr(count_str)) {
		int count = atoi(count_str);
		prom_gauge_set(kazoo_nodes_gauge, count, NULL);
	}
}

static void sofia_profile_statistics_handler(switch_event_t *event) {
	char *profile = switch_event_get_header(event, "profile_name");
	char *calls_in = switch_event_get_header(event, "CALLS-IN");
	char *failed_calls_in = switch_event_get_header(event, "FAILED-CALLS-IN");
	char *calls_out = switch_event_get_header(event, "CALLS-OUT");
	char *failed_calls_out = switch_event_get_header(event, "FAILED-CALLS-OUT");

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "profile_name='%s'; calls_in='%s'; failed_calls_in='%s'; calls_out='%s'; failed_calls_out='%s'\n",
			  switch_str_nil(profile),
			  switch_str_nil(calls_in),
			  switch_str_nil(failed_calls_in),
			  switch_str_nil(calls_out),
			  switch_str_nil(failed_calls_out));
}

static void set_global_ip(const char *string) {
    if (!string)
	return;

    if (globals.ip) {
	free(globals.ip);
	globals.ip = NULL;
    }
    globals.ip = strdup(string);
}

static switch_status_t load_config()
{
	char *cf = "prometheus.conf";
	switch_xml_t cfg, xml = NULL, settings, param;
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	if (!(xml = switch_xml_open_cfg(cf, &cfg, NULL))) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Open of %s failed\n", cf);
		status = SWITCH_STATUS_FALSE;
		return status;
	}

	if ((settings = switch_xml_child(cfg, "settings"))) {
		for (param = switch_xml_child(settings, "param"); param; param = param->next) {
			char *var = (char *) switch_xml_attr_soft(param, "name");
			char *val = (char *) switch_xml_attr_soft(param, "value");
			if (!strcasecmp(var, "listen-ip")) {
				set_global_ip(val);
			} else if (!strcasecmp(var, "listen-port")) {
				globals.port = atoi(val);
			} else if (!strcasecmp(var, "debug")) {
				globals.debug = switch_true(val);
			}
		}
	}

	switch_xml_free(xml);

	return status;
}

static switch_status_t prometheus_init()
{
	switch_memory_pool_t *pool;
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "prometheus_init 1\n");
	if (switch_core_new_memory_pool(&pool) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "OH OH no pool\n");
		return SWITCH_STATUS_FALSE;
	}
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "prometheus_init 2\n");
	memset(&globals, 0, sizeof(globals));
	set_global_ip("0.0.0.0");
	globals.pool = pool;
	globals.port = (switch_port_t)9100;
	globals.debug = 1;

	load_config();
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "prometheus_init 3\n");

	if (globals.port) {
		const char* value_array[] = { "one", "two" };
		globals.running = 1;
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "prometheus_init 4\n");
		prom_collector_registry_default_init();
		foo_gauge_number = prom_collector_registry_must_register_metric(prom_gauge_new("foo_gauge_number", "gauge for foo", 0, NULL));
		foo_gauge = prom_collector_registry_must_register_metric(prom_gauge_new("foo", "foo is a gauge with labels", 2, value_array));
		kazoo_nodes_gauge = prom_collector_registry_must_register_metric(prom_gauge_new("kazoo_nodes_count", "Kazoo Nodes Count", 0, NULL));
		prom_gauge_inc(foo_gauge, (const char*[]) { "bar1", "bang1" });
		prom_gauge_inc(foo_gauge, (const char*[]) { "bar2", "bang2" });
		prom_gauge_inc(foo_gauge, (const char*[]) { "bar3", "bang3" });
		prom_gauge_inc(foo_gauge, (const char*[]) { "bar4", "bang4" });
		prom_gauge_inc(foo_gauge, (const char*[]) { "bar5", "bang5" });
		for (int i = 0; i < 12; i++) {
		     prom_gauge_inc(foo_gauge_number, NULL);
		}

		prom_gauge_dec(foo_gauge, (const char*[]) { "bar4", "bang4" });
		prom_gauge_dec(foo_gauge_number, NULL);

		prom_gauge_add(foo_gauge, 22, (const char*[]) { "bar22", "bang22" });
		prom_gauge_add(foo_gauge, 23, (const char*[]) { "bar23", "bang23" });
		prom_gauge_add(foo_gauge_number, 22, NULL);
		prom_gauge_add(foo_gauge_number, 23, NULL);


		prom_gauge_sub(foo_gauge, 22, (const char*[]) { "bar22", "bang22" });
		prom_gauge_sub(foo_gauge_number, 22, NULL);

		prom_gauge_set(foo_gauge, 24, (const char*[]) { "bar4", "bang4" });
		prom_gauge_set(foo_gauge_number, 24, NULL);

		promhttp_set_active_collector_registry(NULL);
		prometheus_daemon = promhttp_start_daemon(MHD_USE_SELECT_INTERNALLY, globals.port, NULL, NULL);

switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "prometheus_init 5\n");
	}

	return prometheus_daemon != NULL ? SWITCH_STATUS_SUCCESS : SWITCH_STATUS_FALSE;
}

SWITCH_STANDARD_APP(prometheus_app)
{
	//TODO: prometheus main loop
	switch_status_t status = prometheus_init();
	const char* init_status = SWITCH_STATUS_SUCCESS == status ? "Success" : "Failure";
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Prometheus initialization status '%s'\n", init_status);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Prometheus initialization status '%s'\n", init_status);
}

/* Macro expands to: switch_status_t mod_prometheus_load(switch_loadable_module_interface_t **module_interface, switch_memory_pool_t *pool) */
SWITCH_MODULE_LOAD_FUNCTION(mod_prometheus_load)
{
	switch_application_interface_t *app_interface;
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "mod_prometheus_load 1\n");
	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	SWITCH_ADD_APP(app_interface, "prometheus", "prometheus", "prometheus", prometheus_app, NULL, SAF_NONE);
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "mod_prometheus_load 2\n");

	if (switch_event_bind(modname, SWITCH_EVENT_CUSTOM, KAZOO_NODES_COUNT, kazoo_nodes_count_handler, NULL) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't subscribe to kazoo statistics events!\n");
	}

	if (switch_event_bind(modname, SWITCH_EVENT_CUSTOM, MY_EVENT_SOFIA_STATISTICS, sofia_profile_statistics_handler, NULL) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't subscribe to sofia statistics events!\n");
	}

	prometheus_app(NULL, NULL);
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "mod_prometheus_load 3\n");
	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/*
  Called when the system shuts down
  Macro expands to: switch_status_t mod_prometheus_shutdown() */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_prometheus_shutdown)
{
	switch_status_t st = SWITCH_STATUS_SUCCESS;

	globals.running = 0;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "destroying thread\n");

	//TODO: remove
	switch_safe_free(globals.ip);

	prom_collector_registry_destroy(PROM_COLLECTOR_REGISTRY_DEFAULT);
	PROM_COLLECTOR_REGISTRY_DEFAULT = NULL;

	// Stop the HTTP server
	MHD_stop_daemon(prometheus_daemon);

	return st;
}


/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet
 */
