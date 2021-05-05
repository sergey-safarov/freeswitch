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
#include <prom_metric.h>
#include <prom_metric_sample.h>
#include <promhttp.h>

#define KAZOO_NODES_COUNT "kazoo::nodes"
#define MY_EVENT_SOFIA_STATISTICS "sofia::call_statistics"
#define MY_EVENT_CALL_RTP_STATISTICS "sofia::rtp_statistics"

static prom_gauge_t *kazoo_nodes_gauge;
static prom_gauge_t *sofia_call_stat_gauge;
static prom_gauge_t *sofia_rtp_stat_gauge;

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

static void sofia_profile_call_statistics_handler(switch_event_t *event) {
	char *profile = switch_event_get_header(event, "profile_name");
	char *calls_in = switch_event_get_header(event, "CALLS-IN");
	char *failed_calls_in = switch_event_get_header(event, "FAILED-CALLS-IN");
	char *calls_out = switch_event_get_header(event, "CALLS-OUT");
	char *failed_calls_out = switch_event_get_header(event, "FAILED-CALLS-OUT");

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG1, "profile_name='%s'; calls_in='%s'; failed_calls_in='%s'; calls_out='%s'; failed_calls_out='%s'\n",
			  switch_str_nil(profile),
			  switch_str_nil(calls_in),
			  switch_str_nil(failed_calls_in),
			  switch_str_nil(calls_out),
			  switch_str_nil(failed_calls_out));

	if (!zstr(profile)) {
		if (!zstr(calls_in))
			prom_gauge_set(sofia_call_stat_gauge, atoi(calls_in), (const char*[]) { profile, "CALLS-IN" });
		if (!zstr(failed_calls_in))
			prom_gauge_set(sofia_call_stat_gauge, atoi(failed_calls_in), (const char*[]) { profile, "FAILED-CALLS-IN" });
		if (!zstr(calls_out))
			prom_gauge_set(sofia_call_stat_gauge, atoi(calls_out), (const char*[]) { profile, "CALLS-OUT" });
		if (!zstr(failed_calls_out))
			prom_gauge_set(sofia_call_stat_gauge, atoi(failed_calls_out), (const char*[]) { profile, "FAILED-CALLS-OUT" });
	}
}

static void update_rtp_gauge_value(const char* profile, const char* media, const char* param_name, const char* param_value) {
	if (!zstr(profile) && !zstr(media) && !zstr(param_name) && !zstr(param_value)) {
		prom_metric_sample_t *sample = prom_metric_sample_from_labels(sofia_rtp_stat_gauge, (const char*[]) {profile, media, param_name});
		int param_value_count = atoi(param_value);
		double value = 0;
		if (sample && !prom_metric_sample_get(sample, &value) && param_value_count > 0) {
			prom_gauge_set(sofia_rtp_stat_gauge, value + param_value_count, (const char*[]) { profile, media, param_name });
		}
	}
}

static void update_if_more_than_max(const char* profile, const char* media, const char* param_name, const char* param_value) {
	if (!zstr(profile) && !zstr(media) && !zstr(param_name) && !zstr(param_value)) {
		prom_metric_sample_t *sample = prom_metric_sample_from_labels(sofia_rtp_stat_gauge, (const char*[]) {profile, media, param_name});
		int param_value_count = atoi(param_value);
		double value = 0;
		if (sample && !prom_metric_sample_get(sample, &value) && param_value_count > 0 && param_value_count > value) {
			prom_gauge_set(sofia_rtp_stat_gauge, param_value_count, (const char*[]) { profile, media, param_name });
		}
	}
}

static void update_if_less_than_min(const char* profile, const char* media, const char* param_name, const char* param_value) {
	if (!zstr(profile) && !zstr(media) && !zstr(param_name) && !zstr(param_value)) {
		prom_metric_sample_t *sample = prom_metric_sample_from_labels(sofia_rtp_stat_gauge, (const char*[]) {profile, media, param_name});
		int param_value_count = atoi(param_value);
		double value = 0;
		if (sample && !prom_metric_sample_get(sample, &value) && param_value_count > 0 && param_value_count < value) {
			prom_gauge_set(sofia_rtp_stat_gauge, param_value_count, (const char*[]) { profile, media, param_name });
		}
	}
}

static void sofia_profile_rtp_statistics_handler(switch_event_t *event) {
	char *profile = switch_event_get_header(event, "profile_name");

	char* in_raw_bytes = switch_event_get_header(event, "audio_in_raw_bytes");
	char* in_media_bytes = switch_event_get_header(event, "audio_in_media_bytes");
	char* in_packet_count = switch_event_get_header(event, "audio_in_packet_count");
	char* in_media_packet_count = switch_event_get_header(event, "audio_in_media_packet_count");
	char* in_skip_packet_count = switch_event_get_header(event, "audio_in_skip_packet_count");
	char* in_jitter_packet_count = switch_event_get_header(event, "audio_in_jitter_packet_count");
	char* in_dtmf_packet_count = switch_event_get_header(event, "audio_in_dtmf_packet_count");
	char* in_cng_packet_count = switch_event_get_header(event, "audio_in_cng_packet_count");
	char* in_flush_packet_count = switch_event_get_header(event, "audio_in_flush_packet_count");
	char* in_largest_jb_size = switch_event_get_header(event, "audio_in_largest_jb_size");
	char* in_jitter_min_variance = switch_event_get_header(event, "audio_in_jitter_min_variance");
	char* in_jitter_max_variance = switch_event_get_header(event, "audio_in_jitter_max_variance");
	char* in_jitter_loss_rate = switch_event_get_header(event, "audio_in_jitter_loss_rate");
	char* in_jitter_burst_rate = switch_event_get_header(event, "audio_in_jitter_burst_rate");
	char* in_mean_interval = switch_event_get_header(event, "audio_in_mean_interval");
	char* in_flaw_total = switch_event_get_header(event, "audio_in_flaw_total");
	char* in_quality_percentage = switch_event_get_header(event, "audio_in_quality_percentage");
	char* in_mos = switch_event_get_header(event, "audio_in_mos");
	char* out_raw_bytes = switch_event_get_header(event, "audio_out_raw_bytes");
	char* out_media_bytes = switch_event_get_header(event, "audio_out_media_bytes");
	char* out_packet_count = switch_event_get_header(event, "audio_out_packet_count");
	char* out_media_packet_count = switch_event_get_header(event, "audio_out_media_packet_count");
	char* out_skip_packet_count = switch_event_get_header(event, "audio_out_skip_packet_count");
	char* out_dtmf_packet_count = switch_event_get_header(event, "audio_out_dtmf_packet_count");
	char* cng_packet_count = switch_event_get_header(event, "audio_cng_packet_count");
	char* rtcp_packet_count = switch_event_get_header(event, "audio_rtcp_packet_count");
	char* rtcp_octet_count =  switch_event_get_header(event, "audio_rtcp_octet_count");

	if (zstr(profile)) {
		return;
	}

	switch_log_printf(SWITCH_CHANNEL_LOG,
			SWITCH_LOG_DEBUG1,
			"%s Call audio statistics:\n"
			"in_raw_bytes: %s\n"
			"in_media_bytes: %s\n"
			"in_packet_count: %s\n"
			"in_media_packet_count: %s\n"
			"in_skip_packet_count: %s\n"
			"in_jitter_packet_count: %s\n"
			"in_dtmf_packet_count: %s\n"
			"in_cng_packet_count: %s\n"
			"in_flush_packet_count: %s\n"
			"in_largest_jb_size: %s\n\n"
			"in_jitter_min_variance: %s\n"
			"in_jitter_max_variance: %s\n"
			"in_jitter_loss_rate: %s\n"
			"in_jitter_burst_rate: %s\n"
			"in_mean_interval: %s\n\n"
			"in_flaw_total: %s\n"
			"in_quality_percentage: %s\n"
			"in_mos: %s\n\n"
			"out_raw_bytes: %s\n"
			"out_media_bytes: %s\n"
			"out_packet_count: %s\n"
			"out_media_packet_count: %s\n"
			"out_skip_packet_count: %s\n"
			"out_dtmf_packet_count: %s\n"
			"out_cng_packet_count: %s\n\n"
			"rtcp_packet_count: %s\n"
			"rtcp_octet_count: %s\n",
		profile,
		  switch_str_nil(in_raw_bytes),
		  switch_str_nil(in_media_bytes),
		  switch_str_nil(in_packet_count),
		  switch_str_nil(in_media_packet_count),
		  switch_str_nil(in_skip_packet_count),
		  switch_str_nil(in_jitter_packet_count),
		  switch_str_nil(in_dtmf_packet_count),
		  switch_str_nil(in_cng_packet_count),
		  switch_str_nil(in_flush_packet_count),
		  switch_str_nil(in_largest_jb_size),
		  switch_str_nil(in_jitter_min_variance),
		  switch_str_nil(in_jitter_max_variance),
		  switch_str_nil(in_jitter_loss_rate),
		  switch_str_nil(in_jitter_burst_rate),
		  switch_str_nil(in_mean_interval),
		  switch_str_nil(in_flaw_total),
		  switch_str_nil(in_quality_percentage),
		  switch_str_nil(in_mos),
		  switch_str_nil(out_raw_bytes),
		  switch_str_nil(out_media_bytes),
		  switch_str_nil(out_packet_count),
		  switch_str_nil(out_media_packet_count),
		  switch_str_nil(out_skip_packet_count),
		  switch_str_nil(out_dtmf_packet_count),
		  switch_str_nil(cng_packet_count),
		  switch_str_nil(rtcp_packet_count),
		  switch_str_nil(rtcp_octet_count)
			);

	update_rtp_gauge_value(profile, "audio", "audio_in_raw_bytes", in_raw_bytes);
	update_rtp_gauge_value(profile, "audio", "audio_in_media_bytes", in_media_bytes);
	update_rtp_gauge_value(profile, "audio", "audio_in_packet_count", in_packet_count);
	update_rtp_gauge_value(profile, "audio", "audio_in_media_packet_count", in_media_packet_count);
	update_rtp_gauge_value(profile, "audio", "audio_in_skip_packet_count", in_skip_packet_count);
	update_rtp_gauge_value(profile, "audio", "audio_in_jitter_packet_count", in_jitter_packet_count);
	update_rtp_gauge_value(profile, "audio", "audio_in_dtmf_packet_count", in_dtmf_packet_count);
	update_rtp_gauge_value(profile, "audio", "audio_in_cng_packet_count", in_cng_packet_count);
	update_rtp_gauge_value(profile, "audio", "audio_in_flush_packet_count", in_flush_packet_count);
	update_if_more_than_max(profile, "audio", "audio_in_largest_jb_size", in_largest_jb_size);
	update_if_less_than_min(profile, "audio", "audio_in_jitter_min_variance", in_jitter_min_variance);
	update_if_more_than_max(profile, "audio", "audio_in_jitter_max_variance", in_jitter_max_variance);
	//TODO: these params are unclear how to process for taking an average value
	//update_rtp_gauge_value(profile, "audio", "audio_in_jitter_loss_rate", in_jitter_loss_rate);
	//update_rtp_gauge_value(profile, "audio", "audio_in_jitter_burst_rate", in_jitter_burst_rate);
	//update_rtp_gauge_value(profile, "audio", "audio_in_mean_interval", in_mean_interval);
	//update_rtp_gauge_value(profile, "audio", "audio_in_quality_percentage", in_quality_percentage);
	//update_rtp_gauge_value(profile, "audio", "audio_in_mos", in_mos);
	update_rtp_gauge_value(profile, "audio", "audio_in_flaw_total", in_flaw_total);
	update_rtp_gauge_value(profile, "audio", "audio_out_raw_bytes", out_raw_bytes);
	update_rtp_gauge_value(profile, "audio", "audio_out_media_bytes", out_media_bytes);
	update_rtp_gauge_value(profile, "audio", "audio_out_packet_count", out_packet_count);
	update_rtp_gauge_value(profile, "audio", "audio_out_media_packet_count", out_media_packet_count);
	update_rtp_gauge_value(profile, "audio", "audio_out_skip_packet_count", out_skip_packet_count);
	update_rtp_gauge_value(profile, "audio", "audio_out_dtmf_packet_count", out_dtmf_packet_count);
	update_rtp_gauge_value(profile, "audio", "audio_cng_packet_count", cng_packet_count);
	update_rtp_gauge_value(profile, "audio", "audio_rtcp_packet_count", rtcp_packet_count);
	update_rtp_gauge_value(profile, "audio", "audio_rtcp_octet_count", rtcp_octet_count);


	in_raw_bytes = switch_event_get_header(event, "video_in_raw_bytes");
	in_media_bytes = switch_event_get_header(event, "video_in_media_bytes");
	in_packet_count = switch_event_get_header(event, "video_in_packet_count");
	in_media_packet_count = switch_event_get_header(event, "video_in_media_packet_count");
	in_skip_packet_count = switch_event_get_header(event, "video_in_skip_packet_count");
	in_jitter_packet_count = switch_event_get_header(event, "video_in_jitter_packet_count");
	in_dtmf_packet_count = switch_event_get_header(event, "video_in_dtmf_packet_count");
	in_cng_packet_count = switch_event_get_header(event, "video_in_cng_packet_count");
	in_flush_packet_count = switch_event_get_header(event, "video_in_flush_packet_count");
	in_largest_jb_size = switch_event_get_header(event, "video_in_largest_jb_size");
	in_jitter_min_variance = switch_event_get_header(event, "video_in_jitter_min_variance");
	in_jitter_max_variance = switch_event_get_header(event, "video_in_jitter_max_variance");
	in_jitter_loss_rate = switch_event_get_header(event, "video_in_jitter_loss_rate");
	in_jitter_burst_rate = switch_event_get_header(event, "video_in_jitter_burst_rate");
	in_mean_interval = switch_event_get_header(event, "video_in_mean_interval");
	in_flaw_total = switch_event_get_header(event, "video_in_flaw_total");
	in_quality_percentage = switch_event_get_header(event, "video_in_quality_percentage");
	in_mos = switch_event_get_header(event, "video_in_mos");
	out_raw_bytes = switch_event_get_header(event, "video_out_raw_bytes");
	out_media_bytes = switch_event_get_header(event, "video_out_media_bytes");
	out_packet_count = switch_event_get_header(event, "video_out_packet_count");
	out_media_packet_count = switch_event_get_header(event, "video_out_media_packet_count");
	out_skip_packet_count = switch_event_get_header(event, "video_out_skip_packet_count");
	out_dtmf_packet_count = switch_event_get_header(event, "video_out_dtmf_packet_count");
	cng_packet_count = switch_event_get_header(event, "video_cng_packet_count");
	rtcp_packet_count = switch_event_get_header(event, "video_rtcp_packet_count");
	rtcp_octet_count =  switch_event_get_header(event, "video_rtcp_octet_count");

	switch_log_printf(SWITCH_CHANNEL_LOG,
			SWITCH_LOG_DEBUG1,
			"%s Call video statistics:\n"
			"in_raw_bytes: %s\n"
			"in_media_bytes: %s\n"
			"in_packet_count: %s\n"
			"in_media_packet_count: %s\n"
			"in_skip_packet_count: %s\n"
			"in_jitter_packet_count: %s\n"
			"in_dtmf_packet_count: %s\n"
			"in_cng_packet_count: %s\n"
			"in_flush_packet_count: %s\n"
			"in_largest_jb_size: %s\n\n"
			"in_jitter_min_variance: %s\n"
			"in_jitter_max_variance: %s\n"
			"in_jitter_loss_rate: %s\n"
			"in_jitter_burst_rate: %s\n"
			"in_mean_interval: %s\n\n"
			"in_flaw_total: %s\n"
			"in_quality_percentage: %s\n"
			"in_mos: %s\n\n"
			"out_raw_bytes: %s\n"
			"out_media_bytes: %s\n"
			"out_packet_count: %s\n"
			"out_media_packet_count: %s\n"
			"out_skip_packet_count: %s\n"
			"out_dtmf_packet_count: %s\n"
			"out_cng_packet_count: %s\n\n"
			"rtcp_packet_count: %s\n"
			"rtcp_octet_count: %s\n",
		profile,
		switch_str_nil(in_raw_bytes),
		switch_str_nil(in_media_bytes),
		switch_str_nil(in_packet_count),
		switch_str_nil(in_media_packet_count),
		switch_str_nil(in_skip_packet_count),
		switch_str_nil(in_jitter_packet_count),
		switch_str_nil(in_dtmf_packet_count),
		switch_str_nil(in_cng_packet_count),
		switch_str_nil(in_flush_packet_count),
		switch_str_nil(in_largest_jb_size),
		switch_str_nil(in_jitter_min_variance),
		switch_str_nil(in_jitter_max_variance),
		switch_str_nil(in_jitter_loss_rate),
		switch_str_nil(in_jitter_burst_rate),
		switch_str_nil(in_mean_interval),
		switch_str_nil(in_flaw_total),
		switch_str_nil(in_quality_percentage),
		switch_str_nil(in_mos),
		switch_str_nil(out_raw_bytes),
		switch_str_nil(out_media_bytes),
		switch_str_nil(out_packet_count),
		switch_str_nil(out_media_packet_count),
		switch_str_nil(out_skip_packet_count),
		switch_str_nil(out_dtmf_packet_count),
		switch_str_nil(cng_packet_count),
		switch_str_nil(rtcp_packet_count),
		switch_str_nil(rtcp_octet_count)
			);

	update_rtp_gauge_value(profile, "video", "video_in_raw_bytes", in_raw_bytes);
	update_rtp_gauge_value(profile, "video", "video_in_media_bytes", in_media_bytes);
	update_rtp_gauge_value(profile, "video", "video_in_packet_count", in_packet_count);
	update_rtp_gauge_value(profile, "video", "video_in_media_packet_count", in_media_packet_count);
	update_rtp_gauge_value(profile, "video", "video_in_skip_packet_count", in_skip_packet_count);
	update_rtp_gauge_value(profile, "video", "video_in_jitter_packet_count", in_jitter_packet_count);
	update_rtp_gauge_value(profile, "video", "video_in_dtmf_packet_count", in_dtmf_packet_count);
	update_rtp_gauge_value(profile, "video", "video_in_cng_packet_count", in_cng_packet_count);
	update_rtp_gauge_value(profile, "video", "video_in_flush_packet_count", in_flush_packet_count);
	update_if_more_than_max(profile, "video", "video_in_largest_jb_size", in_largest_jb_size);
	update_if_less_than_min(profile, "video", "video_in_jitter_min_variance", in_jitter_min_variance);
	update_if_more_than_max(profile, "video", "video_in_jitter_max_variance", in_jitter_max_variance);
	//TODO: these params are unclear how to process for taking an average value
	//update_rtp_gauge_value(profile, "video", "video_in_jitter_loss_rate", in_jitter_loss_rate);
	//update_rtp_gauge_value(profile, "video", "video_in_jitter_burst_rate", in_jitter_burst_rate);
	//update_rtp_gauge_value(profile, "video", "video_in_mean_interval", in_mean_interval);
	//update_rtp_gauge_value(profile, "video", "video_in_quality_percentage", in_quality_percentage);
	//update_rtp_gauge_value(profile, "video", "video_in_mos", in_mos);
	update_rtp_gauge_value(profile, "video", "video_in_flaw_total", in_flaw_total);
	update_rtp_gauge_value(profile, "video", "video_out_raw_bytes", out_raw_bytes);
	update_rtp_gauge_value(profile, "video", "video_out_media_bytes", out_media_bytes);
	update_rtp_gauge_value(profile, "video", "video_out_packet_count", out_packet_count);
	update_rtp_gauge_value(profile, "video", "video_out_media_packet_count", out_media_packet_count);
	update_rtp_gauge_value(profile, "video", "video_out_skip_packet_count", out_skip_packet_count);
	update_rtp_gauge_value(profile, "video", "video_out_dtmf_packet_count", out_dtmf_packet_count);
	update_rtp_gauge_value(profile, "video", "video_cng_packet_count", cng_packet_count);
	update_rtp_gauge_value(profile, "video", "video_rtcp_packet_count", rtcp_packet_count);
	update_rtp_gauge_value(profile, "video", "video_rtcp_octet_count", rtcp_octet_count);

	in_raw_bytes = switch_event_get_header(event, "text_in_raw_bytes");
	in_media_bytes = switch_event_get_header(event, "text_in_media_bytes");
	in_packet_count = switch_event_get_header(event, "text_in_packet_count");
	in_media_packet_count = switch_event_get_header(event, "text_in_media_packet_count");
	in_skip_packet_count = switch_event_get_header(event, "text_in_skip_packet_count");
	in_jitter_packet_count = switch_event_get_header(event, "text_in_jitter_packet_count");
	in_dtmf_packet_count = switch_event_get_header(event, "text_in_dtmf_packet_count");
	in_cng_packet_count = switch_event_get_header(event, "text_in_cng_packet_count");
	in_flush_packet_count = switch_event_get_header(event, "text_in_flush_packet_count");
	in_largest_jb_size = switch_event_get_header(event, "text_in_largest_jb_size");
	in_jitter_min_variance = switch_event_get_header(event, "text_in_jitter_min_variance");
	in_jitter_max_variance = switch_event_get_header(event, "text_in_jitter_max_variance");
	in_jitter_loss_rate = switch_event_get_header(event, "text_in_jitter_loss_rate");
	in_jitter_burst_rate = switch_event_get_header(event, "text_in_jitter_burst_rate");
	in_mean_interval = switch_event_get_header(event, "text_in_mean_interval");
	in_flaw_total = switch_event_get_header(event, "text_in_flaw_total");
	in_quality_percentage = switch_event_get_header(event, "text_in_quality_percentage");
	in_mos = switch_event_get_header(event, "text_in_mos");
	out_raw_bytes = switch_event_get_header(event, "text_out_raw_bytes");
	out_media_bytes = switch_event_get_header(event, "text_out_media_bytes");
	out_packet_count = switch_event_get_header(event, "text_out_packet_count");
	out_media_packet_count = switch_event_get_header(event, "text_out_media_packet_count");
	out_skip_packet_count = switch_event_get_header(event, "text_out_skip_packet_count");
	out_dtmf_packet_count = switch_event_get_header(event, "text_out_dtmf_packet_count");
	cng_packet_count = switch_event_get_header(event, "text_cng_packet_count");
	rtcp_packet_count = switch_event_get_header(event, "text_rtcp_packet_count");
	rtcp_octet_count =  switch_event_get_header(event, "text_rtcp_octet_count");

	switch_log_printf(SWITCH_CHANNEL_LOG,
			SWITCH_LOG_DEBUG1,
			"%s Call text statistics:\n"
			"in_raw_bytes: %s\n"
			"in_media_bytes: %s\n"
			"in_packet_count: %s\n"
			"in_media_packet_count: %s\n"
			"in_skip_packet_count: %s\n"
			"in_jitter_packet_count: %s\n"
			"in_dtmf_packet_count: %s\n"
			"in_cng_packet_count: %s\n"
			"in_flush_packet_count: %s\n"
			"in_largest_jb_size: %s\n\n"
			"in_jitter_min_variance: %s\n"
			"in_jitter_max_variance: %s\n"
			"in_jitter_loss_rate: %s\n"
			"in_jitter_burst_rate: %s\n"
			"in_mean_interval: %s\n\n"
			"in_flaw_total: %s\n"
			"in_quality_percentage: %s\n"
			"in_mos: %s\n\n"
			"out_raw_bytes: %s\n"
			"out_media_bytes: %s\n"
			"out_packet_count: %s\n"
			"out_media_packet_count: %s\n"
			"out_skip_packet_count: %s\n"
			"out_dtmf_packet_count: %s\n"
			"out_cng_packet_count: %s\n\n"
			"rtcp_packet_count: %s\n"
			"rtcp_octet_count: %s\n",
		profile,
		switch_str_nil(in_raw_bytes),
		switch_str_nil(in_media_bytes),
		switch_str_nil(in_packet_count),
		switch_str_nil(in_media_packet_count),
		switch_str_nil(in_skip_packet_count),
		switch_str_nil(in_jitter_packet_count),
		switch_str_nil(in_dtmf_packet_count),
		switch_str_nil(in_cng_packet_count),
		switch_str_nil(in_flush_packet_count),
		switch_str_nil(in_largest_jb_size),
		switch_str_nil(in_jitter_min_variance),
		switch_str_nil(in_jitter_max_variance),
		switch_str_nil(in_jitter_loss_rate),
		switch_str_nil(in_jitter_burst_rate),
		switch_str_nil(in_mean_interval),
		switch_str_nil(in_flaw_total),
		switch_str_nil(in_quality_percentage),
		switch_str_nil(in_mos),
		switch_str_nil(out_raw_bytes),
		switch_str_nil(out_media_bytes),
		switch_str_nil(out_packet_count),
		switch_str_nil(out_media_packet_count),
		switch_str_nil(out_skip_packet_count),
		switch_str_nil(out_dtmf_packet_count),
		switch_str_nil(cng_packet_count),
		switch_str_nil(rtcp_packet_count),
		switch_str_nil(rtcp_octet_count)
			);

	update_rtp_gauge_value(profile, "text", "text_in_raw_bytes", in_raw_bytes);
	update_rtp_gauge_value(profile, "text", "text_in_media_bytes", in_media_bytes);
	update_rtp_gauge_value(profile, "text", "text_in_packet_count", in_packet_count);
	update_rtp_gauge_value(profile, "text", "text_in_media_packet_count", in_media_packet_count);
	update_rtp_gauge_value(profile, "text", "text_in_skip_packet_count", in_skip_packet_count);
	update_rtp_gauge_value(profile, "text", "text_in_jitter_packet_count", in_jitter_packet_count);
	update_rtp_gauge_value(profile, "text", "text_in_dtmf_packet_count", in_dtmf_packet_count);
	update_rtp_gauge_value(profile, "text", "text_in_cng_packet_count", in_cng_packet_count);
	update_rtp_gauge_value(profile, "text", "text_in_flush_packet_count", in_flush_packet_count);
	update_if_more_than_max(profile, "text", "text_in_largest_jb_size", in_largest_jb_size);
	update_if_less_than_min(profile, "text", "text_in_jitter_min_variance", in_jitter_min_variance);
	update_if_more_than_max(profile, "text", "text_in_jitter_max_variance", in_jitter_max_variance);
	//TODO: these params are unclear how to process for taking an average value
	//update_rtp_gauge_value(profile, "text", "text_in_jitter_loss_rate", in_jitter_loss_rate);
	//update_rtp_gauge_value(profile, "text", "text_in_jitter_burst_rate", in_jitter_burst_rate);
	//update_rtp_gauge_value(profile, "text", "text_in_mean_interval", in_mean_interval);
	//update_rtp_gauge_value(profile, "text", "text_in_quality_percentage", in_quality_percentage);
	//update_rtp_gauge_value(profile, "text", "text_in_mos", in_mos);
	update_rtp_gauge_value(profile, "text", "text_in_flaw_total", in_flaw_total);
	update_rtp_gauge_value(profile, "text", "text_out_raw_bytes", out_raw_bytes);
	update_rtp_gauge_value(profile, "text", "text_out_media_bytes", out_media_bytes);
	update_rtp_gauge_value(profile, "text", "text_out_packet_count", out_packet_count);
	update_rtp_gauge_value(profile, "text", "text_out_media_packet_count", out_media_packet_count);
	update_rtp_gauge_value(profile, "text", "text_out_skip_packet_count", out_skip_packet_count);
	update_rtp_gauge_value(profile, "text", "text_out_dtmf_packet_count", out_dtmf_packet_count);
	update_rtp_gauge_value(profile, "text", "text_cng_packet_count", cng_packet_count);
	update_rtp_gauge_value(profile, "text", "text_rtcp_packet_count", rtcp_packet_count);
	update_rtp_gauge_value(profile, "text", "text_rtcp_octet_count", rtcp_octet_count);

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
	if (switch_core_new_memory_pool(&pool) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "OH OH no pool\n");
		return SWITCH_STATUS_FALSE;
	}

	memset(&globals, 0, sizeof(globals));
	set_global_ip("0.0.0.0");
	globals.pool = pool;
	globals.port = (switch_port_t)9100;
	globals.debug = 1;

	load_config();
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Prometheus config has been loaded\n");

	if (globals.port) {
		globals.running = 1;

		prom_collector_registry_default_init();
		sofia_call_stat_gauge = prom_collector_registry_must_register_metric(prom_gauge_new("sofia_call_statistics", "sofia calls statistics", 2, (const char*[]) { "profile", "metric"}));
		sofia_rtp_stat_gauge = prom_collector_registry_must_register_metric(prom_gauge_new("sofia_rtp_statistics", "sofia rtp statistics", 3, (const char*[]) { "profile", "media", "param"}));
		kazoo_nodes_gauge = prom_collector_registry_must_register_metric(prom_gauge_new("kazoo_nodes_count", "Kazoo Nodes Count", 0, NULL));

		promhttp_set_active_collector_registry(NULL);
		prometheus_daemon = promhttp_start_daemon(MHD_USE_SELECT_INTERNALLY, globals.port, NULL, NULL);

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Trying to start prometheus  on IP/port: [%s:%d]\n", globals.ip, globals.port);
	}

	return prometheus_daemon != NULL ? SWITCH_STATUS_SUCCESS : SWITCH_STATUS_FALSE;
}

SWITCH_STANDARD_APP(prometheus_app)
{
	//TODO: prometheus main loop
	switch_status_t status = prometheus_init();
	const char* init_status = SWITCH_STATUS_SUCCESS == status ? "Success" : "Failure";
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Prometheus initialization status '%s'\n", init_status);
}

/* Macro expands to: switch_status_t mod_prometheus_load(switch_loadable_module_interface_t **module_interface, switch_memory_pool_t *pool) */
SWITCH_MODULE_LOAD_FUNCTION(mod_prometheus_load)
{
	switch_application_interface_t *app_interface;

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	SWITCH_ADD_APP(app_interface, "prometheus", "prometheus", "prometheus", prometheus_app, NULL, SAF_NONE);

	if (switch_event_bind(modname, SWITCH_EVENT_CUSTOM, KAZOO_NODES_COUNT, kazoo_nodes_count_handler, NULL) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't subscribe to kazoo statistics events!\n");
	}

	if (switch_event_bind(modname, SWITCH_EVENT_CUSTOM, MY_EVENT_SOFIA_STATISTICS, sofia_profile_call_statistics_handler, NULL) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't subscribe to sofia calls statistics events!\n");
	}

	if (switch_event_bind(modname, SWITCH_EVENT_CUSTOM, MY_EVENT_CALL_RTP_STATISTICS, sofia_profile_rtp_statistics_handler, NULL) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't subscribe to rtp statistics events!\n");
	}

	prometheus_app(NULL, NULL);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "prometheus module has been succesfully loaded\n");
	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/*
  Called when the system shuts down
  Macro expands to: switch_status_t mod_prometheus_shutdown() */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_prometheus_shutdown)
{
	globals.running = 0;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "destroying thread\n");

	switch_event_unbind_callback(sofia_profile_rtp_statistics_handler);
	switch_event_unbind_callback(sofia_profile_call_statistics_handler);
	switch_event_unbind_callback(kazoo_nodes_count_handler);

	//TODO: remove
	switch_safe_free(globals.ip);

	prom_collector_registry_destroy(PROM_COLLECTOR_REGISTRY_DEFAULT);
	PROM_COLLECTOR_REGISTRY_DEFAULT = NULL;

	// Stop the HTTP server
	MHD_stop_daemon(prometheus_daemon);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "prometheus module has been succesfully stopped\n");
	return SWITCH_STATUS_SUCCESS;
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
