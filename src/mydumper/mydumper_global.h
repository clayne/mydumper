
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <mysql.h>
#include "mydumper_start_dump.h"
extern gchar *defaults_file;
extern char *defaults_extra_file;
extern GHashTable *all_dbts;
extern GOptionEntry common_filter_entries[];
extern GOptionEntry common_connection_entries[];
extern GOptionEntry common_entries[];
extern gboolean program_version;
extern guint verbose;
extern gboolean debug;
extern gchar *tables_list;
extern const gchar *fields_enclosed_by;
extern const gchar *fields_enclosed_by_ld;
extern gchar *lines_starting_by_ld;
extern gchar *statement_terminated_by_ld;
extern gboolean insert_ignore;
extern gboolean hex_blob;
extern gchar *lines_terminated_by_ld;
extern gchar *fields_terminated_by_ld;
extern gboolean csv;
extern gboolean clickhouse;
extern gboolean include_header;
extern gboolean replace;
extern guint chunk_filesize;
extern gchar *ignore_engines_str;
extern int build_empty_files;
extern gboolean exit_if_broken_table_found;
extern gboolean data_checksums;
extern gboolean no_dump_views;
extern gboolean views_as_tables;
extern gboolean dump_checksums;
extern gboolean split_partitions;
extern guint char_deep;
extern const gchar *exec_per_thread_extension;
extern gchar *exec_per_thread;
extern gboolean order_by_primary_key;
extern guint num_exec_threads;
extern guint snapshot_interval;
extern int killqueries;
extern int longquery;
extern int longquery_retry_interval;
extern int longquery_retries;
extern gboolean skip_ddl_locks;
extern gboolean no_backup_locks;
extern gchar *logfile;
extern gboolean help;
extern GKeyFile * key_file;
extern char **tables;
extern int (*m_open)(char **filename, const char *);
extern char * (*identifier_quote_character_protect)(char *r);
struct db_table;
extern int (*m_close)(guint thread_id, int file, gchar *filename, guint64 size, struct db_table * dbt);
extern GAsyncQueue *start_scheduled_dump;
extern gboolean daemon_mode;
extern gboolean dump_events;
extern gboolean dump_routines;
extern gboolean dump_tablespaces;
extern gboolean dump_triggers;
extern gboolean ignore_generated_fields;
extern gboolean load_data;
extern gboolean no_data;
extern gboolean no_delete;
extern gboolean no_schemas;
extern gboolean no_stream;
extern gboolean routine_checksums;
extern gboolean schema_checksums;
extern gboolean shutdown_triggered;
extern gboolean skip_definer;
extern gboolean skip_constraints;
extern gboolean skip_indexes;
extern gboolean stream;
extern gboolean use_fifo;
extern gboolean use_savepoints;
extern gboolean clear_dumpdir;
extern gboolean dirty_dumpdir;
extern gboolean merge_dumpdir;
extern gboolean use_defer;
extern gboolean check_row_count;
extern gchar *db;
extern gchar *disk_limits;
extern gchar *dump_directory;
extern gchar *exec_command;
extern gchar *fields_escaped_by;
extern gchar *output_directory;
extern gchar *output_directory_str;
extern gchar *pmm_path;
extern gchar *pmm_resolution ;
extern gchar *set_names_str;
extern gchar *tables_skiplist_file;
extern gchar *tidb_snapshot;
extern gchar *where_option;
extern GHashTable *all_where_per_table;
extern gint database_counter;
extern struct MList *transactional_table, *non_transactional_table;
extern GMutex *transactional_table_mutex;
extern GMutex *non_transactional_table_mutex;
extern GList *no_updated_tables;
extern GList *schema_post;
extern GList *table_schemas;
extern GList *trigger_schemas;
extern GList *view_schemas;
extern GRecMutex *ready_database_dump_mutex;
extern GString *set_global;
extern GString *set_global_back;
extern GString *set_session;
extern gchar *sql_mode;
extern guint char_chunk;
extern guint complete_insert;
extern guint dump_number;
extern guint errors;
extern guint num_threads;
extern guint64 starting_chunk_step_size;
extern guint snapshot_count;
extern guint statement_size;
extern guint updated_since;
extern int errno;
extern int need_dummy_read;
extern int need_dummy_toku_read;
extern int skip_tz;
extern MYSQL *main_connection;
extern struct configuration_per_table conf_per_table;
extern gboolean schema_sequence_fix;
extern gboolean it_is_a_consistent_backup;
extern gchar *partition_regex;
extern gchar **exec_per_thread_cmd;
extern const gchar *compress_method;
extern guint64 min_chunk_step_size;
extern guint64 max_chunk_step_size;
extern gboolean compact;
extern gboolean split_integer_tables;
extern const gchar *rows_file_extension;
extern gint source_data;
extern guint output_format;
extern struct function_pointer identity_function_pointer;
extern GAsyncQueue *stream_queue;
extern gboolean masquerade_filename;
extern enum sync_thread_lock_mode sync_thread_lock_mode;
extern guint trx_tables;
extern gboolean replica_stopped;
extern guint isms;
