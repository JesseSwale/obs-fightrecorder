#pragma once

#if defined(_WIN32) || defined(_WIN64)
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#define FILE_SEPARATOR "\\"
	#define HOME_DIR "UserProfile"
	#include <windows.h>
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
	bool replaybuffer_alwayson;
	char *logs_dir;
	char *logs_regex;
	char *output_dir;
	__int64 grace_period;
	char *adv_options;

	// state
	bool started_recording;
	bool active_eve_clients; // only used when replaybuffer_alwayson = false
	pthread_mutex_t pthread_shutdown_lock; // mutex for bool pthread_shutdown
	bool pthread_shutdown;
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
bool observer_thread_needs_shutdown();
void start_observer_thread();
void stop_observer_thread();
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
bool concat_property_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);

#ifdef _WIN32
	BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);
#endif

