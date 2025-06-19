#pragma once

#if defined(_WIN32) || defined(_WIN64)
#define FILE_SEPARATOR "\\"
#define HOME_DIR "UserProfile"
#else
#define FILE_SEPARATOR "/"
#define HOME_DIR "HOME"
#endif

#if _MSC_VER
#define __popen _popen
#define __pclose _pclose
#else
#define __popen popen
#define __pclose pclose
#endif

#include <pthread.h>

#define MAX_LINE_LENGTH 1024
#define MAX_PATH 260
#define MAX_STREAMS 8

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

const char *config_file_string = "obs-fightrecorder.json";

typedef struct fightrecorder_data {
	bool active;
	bool concatdelete;
	bool concat;
	const char *logs_dir;
	const char *logs_regex;
	const char *output_dir;
	__int64 grace_period;
	const char *adv_options;

	// state
	bool started_recording;
	pthread_t thread_id;
} fightrecorder_data_t;

// The recording tuple
typedef struct fightrecorder_rec {
	const char *file_replaybuffer;
	const char *file_recording;
} fightrecorder_rec_t;

typedef struct logfile {
	char *file_path;
	FILE *fp;
	int64_t file_position;
	struct logfile *next;
} logfile_t;

obs_properties_t *dummy_source_properties(void *fightrecorder_data);
void save_replay_buffer_start_recording();
void stop_recording();
void open_fightrecorder_settings(void *data);
bool check_file(FILE *file, const char *word, long *position);
void free_logfiles(logfile_t *head);
logfile_t *create_logfile_node(const char *file_path);
void add_logfile_if_not_exists(logfile_t **head, const char *file_path);
void *monitor_file_and_control_recording(fightrecorder_data_t *arg);
void start_observer_thread();
void start_replaybuffer_if_active();
void concat_recording_tuple();

void obs_module_unload(void);
const char *dummy_source_name(void *data);
void dummy_source_destroy(void *data);
void dummy_source_defaults(obs_data_t *settings);
void dummy_source_update(fightrecorder_data_t *data, obs_data_t *settings);
void *dummy_source_create(obs_data_t *settings, obs_source_t *source);
void on_obs_frontend_event_exit();
bool obs_module_load(void);
void on_obs_frontend_event_finished_loading();
bool replay_buffer_settings_modified_cb(obs_properties_t *props, obs_property_t *prop, obs_data_t *settings);