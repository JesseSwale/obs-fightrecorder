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

#include "plugin-main.h"

obs_source_t *fightrecorder_source = NULL;
fightrecorder_data_t *fightrecorder = NULL;

obs_properties_t *dummy_source_properties(void *fightrecorder_data)
{
	struct fightrecorder_data *data = fightrecorder_data;

	obs_properties_t *props = obs_properties_create();

	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	obs_properties_t *group_main = obs_properties_create();

	obs_properties_add_path(group_main, "fightrecorder_logs_dir",
				"Game Logs Dir", OBS_PATH_DIRECTORY,
				NULL, NULL);

	obs_properties_add_int(group_main, "fightrecorder_grace_period",
			       "Grace period (seconds)", 10, 99999, 1);

	obs_properties_add_bool(group_main, "fightrecorder_active", "Active");


	obs_properties_add_group(props, "Main", "Main settings",
				 OBS_GROUP_NORMAL, group_main);

	//obs_properties_t *group_adv = obs_properties_create();
	//obs_properties_add_text(group_adv, "fight_recorder_logs_regex",
	//			"Log Regex", OBS_TEXT_DEFAULT);

	/*
	obs_property_t *advanced_plugin_property = obs_properties_add_text(group_adv, "fight_recorder_advanced_options",
				"Plugin options", OBS_TEXT_DEFAULT);
	*/
	/*
	obs_property_set_long_description(
		advanced_plugin_property,
		"Plugin options:\n\n   -i [n]\t Track game log files of the last n hours (Default n=24)\n");
	*/
	//obs_properties_add_group(props, "Advanced", "Advanced settings",
	//			 OBS_GROUP_NORMAL, group_adv);
	
	return props;
}

void save_replay_buffer_start_recording()
{
	if (!fightrecorder->active)
		return;
	if (fightrecorder->started_recording)
		return;

	if (obs_frontend_replay_buffer_active()) {
		obs_frontend_replay_buffer_save();
		blog(LOG_DEBUG, "obs-fightrecorder stopped replay buffer");
	}

	obs_frontend_recording_start();
	blog(LOG_DEBUG, "obs-fightrecorder started recording");
	fightrecorder->started_recording = true;
}

void stop_recording()
{
	if (fightrecorder->started_recording) {
		obs_frontend_recording_stop();
		fightrecorder->started_recording = false;
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
	logfile_t *tmp;

	while (head != NULL) {
		tmp = head;
		head = head->next;
		bfree(tmp->file_path);
		bfree(tmp);
	}
}

logfile_t *create_logfile_node(const char *file_path)
{
	logfile_t *node = bzalloc(sizeof(logfile_t));
	if (node == NULL) {
		obs_log(LOG_ERROR, "error bzalloc");
		return NULL;
	}

	node->file_path = strdup(file_path);
	node->fp = fopen(node->file_path, "r");
	fseek(node->fp, 0, SEEK_END);
	obs_log(LOG_DEBUG, "%s ftell to %d", file_path, "end");


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

void *monitor_file_and_control_recording(fightrecorder_data_t *arg)
{
	int sleep = 1000;
	int update_files_sleep = 30000;
	int update_files_iter = 30000;
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
		if (!fightrecorder->active) {
			break;
		}

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

	obs_log(LOG_INFO, "Observer thread stopped gracefully");
	return NULL;
}

void start_observer_thread()
{
	pthread_create(&fightrecorder->thread_id, NULL,
		       monitor_file_and_control_recording, fightrecorder);
	obs_log(LOG_INFO, "Observer thread started");
}

void start_replaybuffer_if_active()
{
	if (fightrecorder->active) {
		if (!obs_frontend_replay_buffer_active()) {
			obs_frontend_replay_buffer_start();
		}
	}
}

void obs_module_unload(void)
{
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
	bfree(fightrecorder);
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

	config_t* profile_config = obs_frontend_get_profile_config();

	// Load useful default values depending on the current profile
	if (strcmp(config_get_string(profile_config, "Output", "Mode"),
		   "Advanced") == 0)
	{
		obs_data_set_default_string(
			settings, "fightrecorder_output_dir",
			config_get_string(profile_config, "AdvOut",
					  "RecFilePath"));
	}
	else
	{
		obs_data_set_default_string(
			settings, "fightrecorder_output_dir",
			config_get_string(profile_config, "SimpleOutput",
					  "FilePath"));
	}

	obs_data_set_default_bool(settings, "fightrecorder_active", true);
	//obs_data_set_default_bool(settings, "fightrecorder_delete", true);
	obs_data_set_default_int(settings, "fightrecorder_grace_period", 120);
	//obs_data_set_default_bool(settings, "fightrecorder_concat", true);
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
	data->logs_dir = obs_data_get_string(settings, "fightrecorder_logs_dir");
	data->logs_regex = obs_data_get_string(settings, "fight_recorder_logs_regex");
	data->output_dir = obs_data_get_string(settings, "fightrecorder_output_dir");
	data->grace_period = obs_data_get_int(settings, "fightrecorder_grace_period");
	data->adv_options = obs_data_get_string(settings, "fight_recorder_advanced_options");

	if (!active_past && data->active) {
		obs_log(LOG_DEBUG, "fightrecorder_active [false -> true]");
		start_observer_thread();
		start_replaybuffer_if_active();
	}
	else if (active_past && !data->active) {
		obs_log(LOG_DEBUG, "fightrecorder_active [true -> false]");
		obs_log(LOG_DEBUG, "Observer thread should stop anytime now...");
	}
	obs_log(LOG_DEBUG, "fightrecorder_logs_dir: %s", data->logs_dir);
	// obs_log(LOG_DEBUG, "fight_recorder_logs_regex: %s", data->logs_regex);
	obs_log(LOG_DEBUG, "fightrecorder_grace_period: %d", data->grace_period);
}


void *dummy_source_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(source);
	fightrecorder_data_t *fightrecorder_args = bzalloc(sizeof(struct fightrecorder_data));
	fightrecorder = fightrecorder_args;

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
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
		//replay_buffer_in_progress = true;
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
		//replay_buffer_in_progress = false;
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		//if (!fightrecorder_started_recording)
		//	obs_log(LOG_INFO, "User pressed record, stop Fight Recorder until user stopped their recording");
		//	fightrecorder_active = false; /* User starts recording, not fight recorder*/
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		fightrecorder->started_recording = false;
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

	return true;
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

	obs_frontend_add_tools_menu_item(obs_module_text("Fight Recorder"),
					 open_fightrecorder_settings, NULL);

	start_replaybuffer_if_active();
}
