/*
obs-fightrecorder
Copyright (C) 2024-2025, Jesse Swale

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/
#include <obs-module.h>
#include <plugin-support.h>
#include <obs-frontend-api.h>
#include <obs-source.h>
#include <util/platform.h>
#include <util/config-file.h>
#include <util/threading.h>
#include <graphics/graphics.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>

#include "plugin-main.h"


obs_source_t *fightrecorder_source = NULL;
fightrecorder_data_t *fightrecorder = NULL;
fightrecorder_rec_t *recording = NULL;

obs_properties_t *dummy_source_properties(void *fightrecorder_data)
{
	struct fightrecorder_data *data = fightrecorder_data;

	obs_properties_t *props = obs_properties_create();

	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	obs_properties_t *group_main = obs_properties_create();

	obs_properties_add_bool(group_main, "fightrecorder_active", "Enable Fight Recorder");

	obs_properties_add_path(group_main, "fightrecorder_logs_dir",
				"Game Logs Dir", OBS_PATH_DIRECTORY,
				NULL, NULL);

	obs_properties_add_int(group_main, "fightrecorder_grace_period",
			       "Combat grace period (seconds)", 10, 99999, 1);

	obs_properties_add_bool(group_main, "fightrecorder_active", "Active");

	
	obs_property_t *concat_property = obs_properties_add_bool(group_main, "fightrecorder_concat",
				"Enable Concatenation ");

	obs_property_set_long_description(concat_property, "Concatenates (merges) the replay buffer and the recording file in a single output file when the fight is over.\nThe original files are not deleted.");

	obs_properties_add_group(props, "Main", "Main settings",
				 OBS_GROUP_NORMAL, group_main);

	obs_properties_t *group_adv = obs_properties_create();

	obs_properties_add_group(props, "Advanced", "Advanced settings",
				 OBS_GROUP_NORMAL, group_adv);

	
	obs_property_t *group_replaybuffer = obs_properties_add_list(
		group_adv, "fightrecorder_replay_buffer_always_on", "Replay buffer",
		OBS_COMBO_TYPE_RADIO, OBS_COMBO_FORMAT_BOOL);

	obs_property_list_add_bool(group_replaybuffer,
				   obs_module_text("Always on"),
				   true);
	obs_property_list_add_bool(group_replaybuffer,
				   obs_module_text("Start/Stop based on active Eve clients"),
				   false);
	
	return props;
}

void save_replay_buffer_start_recording()
{
	if (!fightrecorder->active)
		return;
	if (fightrecorder->started_recording)
		return;

	obs_frontend_recording_start();
	blog(LOG_DEBUG, "obs-fightrecorder started recording");
	fightrecorder->started_recording = true;

	if (obs_frontend_replay_buffer_active()) {
		obs_frontend_replay_buffer_save();
		blog(LOG_DEBUG, "obs-fightrecorder stopped replay buffer");
	}
}

void stop_recording()
{
	if (fightrecorder->started_recording) {
		obs_frontend_recording_stop();
		blog(LOG_DEBUG, "obs-fightrecorder stopped recording");
	}
}

void open_fightrecorder_settings(void *data)
{
	UNUSED_PARAMETER(data);
	obs_frontend_open_source_properties(fightrecorder_source);
}

bool check_file(FILE *file, const char *word, long *position)
{
	char line[1024];
	while (fgets(line, sizeof(line), file)) {
		if (strstr(line, word)) {
			fclose(file);
			return true;
		}
	}
	return false;
}


void free_logfiles(logfile_t *head) {
	logfile_t *current = head;
	while (current != NULL) {
		logfile_t *next = current->next;

		if (current->fp) {
			fclose(current->fp);
		}
		if (current->file_path) {
			bfree(current->file_path);
		}
		bfree(current);
		current = next;
	}
}

logfile_t *create_logfile_node(const char *file_path)
{
	logfile_t *node = bzalloc(sizeof(logfile_t));

	if (node == NULL) {
		obs_log(LOG_ERROR, "error bzalloc");
		return NULL;
	}

	node->file_path = bstrdup(file_path);
	node->fp = fopen(node->file_path, "r");

	fseek(node->fp, 0, SEEK_END);
	node->file_position = os_ftelli64(node->fp);

	node->next = NULL;
	return node;
}

void add_logfile_if_not_exists(logfile_t **head, const char *file_path)
{
	bool exists = false;

	if (*head == NULL) {
		logfile_t *node = create_logfile_node(file_path);

		if (node == NULL) {
			obs_log(LOG_ERROR, "error create_node");
			return;
		}

		obs_log(LOG_DEBUG, "obs-fightrecorder is watching file %s",
			node->file_path);
		*head = node;
	} else {
		logfile_t *temp = *head;
		if (strcmp(temp->file_path, file_path) == 0) {
			exists = true;
		}

		while (temp->next != NULL) {
			temp = temp->next;
			if (strcmp(temp->file_path, file_path) == 0) {
				exists = true;
			}
		}

		if (!exists) {
			obs_log(LOG_DEBUG,
				"obs-fightrecorder is watching file %s",
				file_path);
			logfile_t *node = create_logfile_node(file_path);

			if (node == NULL) {
				obs_log(LOG_ERROR, "error create_node");
				return;
			}
			temp->next = node;
		}
	}
}

#ifdef _WIN32
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	char title[512];

	if (IsWindowVisible(hwnd) &&
	    GetWindowTextA(hwnd, title, sizeof(title) / sizeof(WCHAR)) > 0) {
		
		if (strstr(title, "EVE - ") != NULL) {
			fightrecorder->active_eve_clients = true;
		}
	}

	return TRUE;
}
#endif

bool observer_thread_needs_shutdown() {
	pthread_mutex_lock(&fightrecorder->pthread_shutdown_lock);
	bool result = fightrecorder->pthread_shutdown;
	pthread_mutex_unlock(&fightrecorder->pthread_shutdown_lock);
	return result;
}

void *monitor_file_and_control_recording(fightrecorder_data_t *arg)
{
	int sleep = 1000;
	int update_files_sleep = 30000;
	int active_windows_sleep = 30000;
	int update_files_iter = 30000;
	int replaybuffer_timeout = 0;
	
	time_t end_recording_time = time(NULL);
	logfile_t *head = NULL;
	logfile_t *curr = head;
	char line[MAX_LINE_LENGTH];
	bool combat = false;
	time_t current_time_dir;
	time_t current_time;
	struct os_dirent *entry;
	char file_path_dir[1024];

	if (fightrecorder == NULL) {
		return NULL;
	}

	while (true) {
		if (observer_thread_needs_shutdown()) {
			obs_log(LOG_DEBUG,
				"shutdown flag is set; Exiting observer thread");
			break;
		}

#ifdef _WIN32
		if (!fightrecorder->replaybuffer_alwayson) {
			if (update_files_iter >= update_files_sleep) {
				fightrecorder->active_eve_clients = false;
				EnumWindows(EnumWindowsProc, 0);
				// if active_eve_clients are still false here we know there are no active Eve clients
				if (fightrecorder->active_eve_clients) {
					if (obs_frontend_replay_buffer_active()) {  // Eve clients, and active replay buffer
						replaybuffer_timeout = 0;
					} 
					else { // Eve clients and no active replay buffer
						obs_log(LOG_DEBUG,
							"Active Eve clients, but replay buffer is stopped, starting now.");
						obs_frontend_replay_buffer_start();
						replaybuffer_timeout = 0;
					}
				} else {
					if (obs_frontend_replay_buffer_active()) { // No Eve clients and active replay buffer
						if (replaybuffer_timeout >= 5) {
							obs_log(LOG_DEBUG,
								"No active Eve clients, stopping replay buffer");
							obs_frontend_replay_buffer_stop();
							replaybuffer_timeout =
								0;
						} else {
							++replaybuffer_timeout;
						}
					} 
				}
			}
		}
#endif

		// ~30s check for new files
		if (update_files_iter >= update_files_sleep) {
			update_files_iter = 0;

			os_dir_t *dir = os_opendir(arg->logs_dir);
			entry = NULL;
			while ((entry = os_readdir(dir)) != NULL) {
				if (strcmp(entry->d_name, ".") == 0 ||
				    strcmp(entry->d_name, "..") == 0) {
					continue;
				}
				snprintf(file_path_dir, sizeof(file_path_dir),
					 "%s/%s",
					 arg->logs_dir, entry->d_name);

				struct stat file_stat;
				if (os_stat(file_path_dir, &file_stat) == -1) {
					perror("stat");
					continue;
				}
				current_time_dir = time(NULL);
				double time_diff = difftime(current_time_dir,
							    file_stat.st_mtime);

				if (time_diff > 86400)
					continue;

				add_logfile_if_not_exists(&head, file_path_dir);

			}
			os_closedir(dir);
		}

		//~1s check for new content in the files
		curr = head;
		while (curr != NULL) {
			//obs_log(LOG_ERROR, "%s fseek to %d", curr->file_path, curr->file_position);
			if (os_fseeki64(curr->fp, curr->file_position,
					SEEK_SET) != 0) {
				obs_log(LOG_ERROR,
					"error setting position in file %s",
					curr->file_path);
			}

			while (fgets(line, sizeof(line), curr->fp)) {
				if (strstr(line, "has applied bonuses to") != NULL ||
				    strstr(line, "combat") != NULL) {

					obs_log(LOG_INFO, "Fightrecorder found combat");
					combat = true;
				}
			}
			curr->file_position = os_ftelli64(curr->fp);
			curr = curr->next;
		}
		
		// ~1s Combat is found, refresh the timer and check if timer is finished
		// to stop recording
		current_time = time(NULL);

		if (combat) {
			end_recording_time = current_time + arg->grace_period;

			if (!fightrecorder->started_recording) {
				save_replay_buffer_start_recording();
			}
		} else {
			if (fightrecorder->started_recording &&
			    current_time > end_recording_time) {
				stop_recording();
				combat = false;
			}
		}

		os_sleep_ms(sleep);
		combat = false;
		update_files_iter += sleep;
	}

	obs_log(LOG_DEBUG, "Freeing logfiles");
	free_logfiles(head);

	obs_log(LOG_INFO, "Observer thread stopped gracefully");
	return NULL;
}

void start_observer_thread()
{
	pthread_mutex_lock(&fightrecorder->pthread_shutdown_lock);
	fightrecorder->pthread_shutdown = false;
	pthread_mutex_unlock(&fightrecorder->pthread_shutdown_lock);

	pthread_create(&fightrecorder->thread_id, NULL,
		       monitor_file_and_control_recording, fightrecorder);
	obs_log(LOG_INFO, "Observer thread started");
}

void stop_observer_thread() 
{
	pthread_mutex_lock(&fightrecorder->pthread_shutdown_lock);
	fightrecorder->pthread_shutdown = true;
	obs_log(LOG_DEBUG, "Set pthread_shutdown=true");
	pthread_mutex_unlock(&fightrecorder->pthread_shutdown_lock);
	int x = pthread_join(fightrecorder->thread_id, NULL);
	obs_log(LOG_DEBUG, "pthread_join exit code=%d", x);
}

void start_replaybuffer_if_active()
{
	if (fightrecorder->active && fightrecorder->replaybuffer_alwayson) {
		if (!obs_frontend_replay_buffer_active()) {
			obs_frontend_replay_buffer_start();
		}
	}
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "Plugin unload start");
	stop_observer_thread();
	pthread_mutex_destroy(&fightrecorder->pthread_shutdown_lock);

	bfree(fightrecorder->adv_options);
	bfree(fightrecorder->logs_dir);
	bfree(fightrecorder->logs_regex);
	bfree(fightrecorder->output_dir);
	bfree(fightrecorder);
	obs_log(LOG_INFO, "Plugin unloaded");
}

const char* dummy_source_name(void *data) 
{
	UNUSED_PARAMETER(data);
	return obs_module_text("Name");
}

void dummy_source_destroy(void *data)
{
	UNUSED_PARAMETER(data);
}

void dummy_source_defaults(obs_data_t *settings)
{
	obs_log(LOG_INFO, "dummy_source_defaults");
	char* default_path_logs = bzalloc(250 * sizeof(char));
	sprintf_s(default_path_logs, 250 * sizeof(char), "%s%s%s%s%s%s%s%s%s",
		  getenv(HOME_DIR),
		  FILE_SEPARATOR, "Documents", FILE_SEPARATOR, "Eve",
		  FILE_SEPARATOR, "logs", FILE_SEPARATOR,
		  "Gamelogs");

	obs_data_set_default_string(settings, "fightrecorder_logs_dir",
				    default_path_logs);

	//config_t* profile_config = obs_frontend_get_profile_config();

	// Load useful default values depending on the current profile
	//char *output_mode = config_get_string(profile_config, "Output", "Mode");
	//char *rec_file_path =  config_get_string(profile_config, "AdvOut", "RecFilePath");
	//if (strcmp(output_mode, "Advanced") == 0)
	//{
	//	obs_data_set_default_string(settings, "fightrecorder_output_dir", rec_file_path);
	//}
	//else
	//{
	//	obs_data_set_default_string(
	//		settings, "fightrecorder_output_dir",
	//		config_get_string(profile_config, "SimpleOutput",
	//				  "FilePath"));
	//}

	obs_data_set_default_bool(settings, "fightrecorder_active", true);
	//obs_data_set_default_bool(settings, "fightrecorder_delete", true);
	obs_data_set_default_int(settings, "fightrecorder_grace_period", 120);
	obs_data_set_default_bool(settings, "fightrecorder_replay_buffer_always_on", true);
	//obs_data_set_default_bool(settings, "fightrecorder_concat", true);
	obs_data_set_default_bool(settings, "fightrecorder_concat", true);
	//obs_data_set_default_string(settings, "fight_recorder_logs_regex", "\\(combat\\)|has applied bonuses to");
	//obs_data_set_default_string(settings, "fight_recorder_advanced_options", "");

	bfree(default_path_logs);
}

void dummy_source_update(fightrecorder_data_t *data, obs_data_t *settings)
{
	bool active_past = data->active;
	data->active = obs_data_get_bool(settings, "fightrecorder_active");
	data->concatdelete = obs_data_get_bool(settings, "fightrecorder_delete");
	data->concat = obs_data_get_bool(settings, "fightrecorder_concat");
	bfree(data->logs_dir);
	data->logs_dir = bstrdup(obs_data_get_string(settings, "fightrecorder_logs_dir"));
	bfree(data->logs_regex);
	data->logs_regex = bstrdup(obs_data_get_string(
		settings, "fight_recorder_logs_regex"));
	//data->output_dir = obs_data_get_string(settings, "fightrecorder_output_dir");
	data->grace_period = obs_data_get_int(settings, "fightrecorder_grace_period");
	bfree(data->adv_options);
	data->adv_options = bstrdup(obs_data_get_string(
		settings, "fight_recorder_advanced_options"));

	bool replay_buffer_alwayson =
		obs_data_get_bool(settings, "fightrecorder_replay_buffer_always_on");
	data->replaybuffer_alwayson = replay_buffer_alwayson;

	if (!active_past && data->active) {
		obs_log(LOG_DEBUG, "fightrecorder_active [false -> true]");
		start_observer_thread();
		start_replaybuffer_if_active();
	}
	else if (active_past && !data->active) {
		obs_log(LOG_DEBUG, "fightrecorder_active [true -> false]");
		obs_log(LOG_DEBUG, "Observer thread should stop anytime now...");
		stop_observer_thread();
	}
	obs_log(LOG_DEBUG, "fightrecorder_logs_dir: %s", data->logs_dir);
	// obs_log(LOG_DEBUG, "fight_recorder_logs_regex: %s", data->logs_regex);
	obs_log(LOG_DEBUG, "fightrecorder_grace_period: %d", data->grace_period);
	obs_log(LOG_DEBUG, "fightrecorder_replay_buffer_always_on: %d",
		data->replaybuffer_alwayson);
	obs_log(LOG_DEBUG, "fightrecorder_concat: %d", data->concat);
}


void *dummy_source_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(source);
	fightrecorder_data_t *fightrecorder_args = bzalloc(sizeof(struct fightrecorder_data));
	fightrecorder = fightrecorder_args;

	fightrecorder_rec_t *recording_tuple =
		bzalloc(sizeof(struct fightrecorder_rec));
	recording = recording_tuple;

	pthread_mutex_init(&fightrecorder->pthread_shutdown_lock, NULL);
	dummy_source_update(fightrecorder_args, settings);

	return fightrecorder_args;
}


static void source_defaults_frontend_event_cb(enum obs_frontend_event event,
					      void *data)
{
	UNUSED_PARAMETER(data);
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		on_obs_frontend_event_finished_loading();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		on_obs_frontend_event_exit();
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED:
		char *replay_output = obs_frontend_get_last_replay();

		if (fightrecorder->started_recording) {
			obs_log(LOG_INFO, "Replaybuffer %s saved as part of the fight", replay_output);
			recording->file_replaybuffer = replay_output;
		} else {
			obs_log(LOG_WARNING,
				"Replay buffer %s saved, but not as part of Fight recorder, button pressed manually",
				replay_output);
		}
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		char *recording_output = obs_frontend_get_last_recording();
		if (fightrecorder->started_recording) {
			obs_log(LOG_INFO,
				"Recording %s saved as part of the fight",
				recording_output);
			fightrecorder->started_recording = false;
			recording->file_recording = recording_output;
		} else {
			obs_log(LOG_WARNING,
				"Recording %s saved, but not as part of Fight recorder, button pressed manually",
				recording_output);
		}
		fightrecorder->started_recording = false;

		if (fightrecorder->concat) {
			concat_recording_tuple();
		}
		break;
	}
}

void on_obs_frontend_event_exit()
{
	char *json_path = obs_module_config_path(config_file_string);
	obs_data_t *settings = obs_source_get_settings(fightrecorder_source);
	obs_data_save_json(settings, json_path);
	obs_data_release(settings);

	bfree(json_path);
	obs_source_release(fightrecorder_source);
}

bool obs_module_load(void)
{
	struct obs_source_info fightrecorder_source = {
		.id = "fightrecorder-source",
		.type = OBS_SOURCE_TYPE_FILTER,
		.output_flags = OBS_SOURCE_CAP_DISABLED,
		.get_name = dummy_source_name,
		.create = dummy_source_create,
		.update = dummy_source_update,
		.destroy = dummy_source_destroy,
		.get_defaults = dummy_source_defaults,
		.get_properties = dummy_source_properties,
	};
	obs_register_source(&fightrecorder_source);

	obs_frontend_add_event_callback(source_defaults_frontend_event_cb,
					NULL);

	obs_log(LOG_INFO, "Plugin successfully loaded (version %s)",
		PLUGIN_VERSION);

	uint32_t version = avformat_version();
	obs_log(LOG_INFO, "libavformat version: %u.%u.%u",
	     (version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF);

	return true;
}

void concat_recording_tuple() {
	AVFormatContext *out_ctx = NULL;
	AVStream *out_streams[MAX_STREAMS] = {0};
	int64_t pts_offset[MAX_STREAMS] = {0};
	int64_t dts_offset[MAX_STREAMS] = {0};
	int stream_mapping[MAX_STREAMS] = {0};
	int64_t max_dts[MAX_STREAMS] = {0};
	int64_t max_pts[MAX_STREAMS] = {0};
	int num_streams = 0;
	char output_path[MAX_PATH];
	char folder[MAX_PATH];
	char filename[MAX_PATH];

	if (!recording->file_replaybuffer || !recording->file_recording) {
		obs_log(LOG_ERROR, "Replay buffer or recording is missing, can't concatenate.");
		return;
	}

	// Make the output filename 
	// /some/path/to/2025-07-12 21-16-41.mkv -> /some/path/to/Fight 2025-07-12 21-16-41.mkv
	strncpy(output_path, recording->file_recording, MAX_PATH - 1);
	output_path[MAX_PATH - 1] = '\0';

	char *last_slash = NULL;
	char *last_forwardslash = strrchr(output_path, '/');
	char *last_backslash = strrchr(output_path, '\\');
	if (!last_backslash && !last_forwardslash) {
		obs_log(LOG_ERROR, "Invalid path (no slashes): %s",
			output_path);
		return;
	}
	if (last_forwardslash)
		last_slash = last_forwardslash;
	if (last_backslash)
		last_slash = last_backslash;

	// Folder might come  from a config sometime later
	size_t folder_length = last_slash - output_path;
	if (folder_length >= MAX_PATH)
		folder_length = MAX_PATH - 1;
	strncpy(folder, output_path, folder_length);
	folder[folder_length] = '\0';

	strncpy(filename, last_slash + 1, MAX_PATH - 1);
	filename[MAX_PATH - 1] = '\0';

	snprintf(output_path, MAX_PATH, "%s%sFight %s", folder, FILE_SEPARATOR,
		 filename);

	// Open output context
	avformat_alloc_output_context2(&out_ctx, NULL, NULL, output_path);
	if (!out_ctx) {
		fprintf(stderr, "Could not create output context\n");
		return;
	}

	for (int i = 1; i <= 2; i++) {
		AVFormatContext *in_ctx = NULL;
		if (avformat_open_input(&in_ctx, i == 1 ? recording->file_replaybuffer : recording->file_recording, NULL, NULL) < 0 ||
		    avformat_find_stream_info(in_ctx, NULL) < 0) {
			obs_log(LOG_ERROR, "Failed to open input \n");
			return;
		}

		if (i == 1) {
			// We may have n output streams (video is 0, rest might be audio)
			for (unsigned int j = 0; j < in_ctx->nb_streams && num_streams < MAX_STREAMS; j++) {
				AVStream *in_stream = in_ctx->streams[j];
				AVStream *out_stream = avformat_new_stream(out_ctx, NULL);
				avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
				out_stream->time_base = in_stream->time_base;
				stream_mapping[j] = num_streams;
				out_streams[num_streams++] = out_stream;
			}

			if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
				if (avio_open(&out_ctx->pb, output_path,
					      AVIO_FLAG_WRITE) < 0) {
					obs_log(LOG_ERROR, "Failed to open output file");
					return;
				}
			}
			avformat_write_header(out_ctx, NULL);
		}

		AVPacket pkt;
		while (av_read_frame(in_ctx, &pkt) >= 0) {
			if (pkt.stream_index >= MAX_STREAMS || stream_mapping[pkt.stream_index] == -1) {
				av_packet_unref(&pkt);
				continue;
			}
			int out_idx = stream_mapping[pkt.stream_index];
			AVStream *in_stream = in_ctx->streams[pkt.stream_index];
			AVStream *out_stream = out_streams[out_idx];
			
			if (pkt.dts > max_dts[out_idx]) {
				max_dts[out_idx] = pkt.dts;
			}
			if (pkt.pts > max_pts[out_idx]) {
				max_pts[out_idx] = pkt.pts;
			}

			if (out_stream == NULL) {
				obs_log(LOG_ERROR,
					"Out stream is null, something went wrong");
				return;
			}

			pkt.pts = av_rescale_q(pkt.pts, in_stream->time_base,
					       out_stream->time_base);
			pkt.dts = av_rescale_q(pkt.dts, in_stream->time_base,
					       out_stream->time_base);
			pkt.duration = av_rescale_q(pkt.duration,
						    in_stream->time_base,
						    out_stream->time_base);

			pkt.pts += pts_offset[out_idx];
			pkt.dts += dts_offset[out_idx];

			pkt.pos = -1;
			pkt.stream_index = out_idx;

			av_interleaved_write_frame(out_ctx, &pkt);
			av_packet_unref(&pkt);
		}
		for (int j = 0; j < num_streams; j++) {
			pts_offset[j] = max_pts[j] + 1;
			dts_offset[j] = max_dts[j] + 1;
			//pts_offset[j] = max_pts[j] + 1;
			//dts_offset[j] = max_dts[j] + 1;
		}
		avformat_close_input(&in_ctx);
	}

	av_write_trailer(out_ctx);
	avio_closep(&out_ctx->pb);
	avformat_free_context(out_ctx);
}


void on_obs_frontend_event_finished_loading()
{
	char *plugin_path = obs_module_config_path(NULL);
	if (os_mkdir(plugin_path) == 0) {
		obs_log(LOG_INFO, "Directory %s created", plugin_path);
	}

	char *json_path = obs_module_config_path(config_file_string);
	obs_data_t *settings = obs_data_create_from_json_file(json_path);

	fightrecorder_source = obs_source_create(
		"fightrecorder-source", "Fightrecorder",
				  settings,
				  NULL);

	bfree(json_path);
	bfree(plugin_path);

	obs_data_release(settings);

	obs_frontend_add_tools_menu_item(obs_module_text("Fight Recorder"),
					 open_fightrecorder_settings, NULL);

	start_replaybuffer_if_active();
}