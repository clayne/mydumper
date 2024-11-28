/*
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

        Authors:    Domas Mituzas, Facebook ( domas at fb dot com )
                    Mark Leith, Oracle Corporation (mark dot leith at oracle dot com)
                    Andrew Hutchings, MariaDB Foundation (andrew at mariadb dot org)
                    Max Bubenick, Percona RDBA (max dot bubenick at percona dot com)
                    David Ducos, Percona (david dot ducos at percona dot com)
*/

#include <gio/gio.h>

#include "mydumper.h"
#include "mydumper_start_dump.h"
#include "mydumper_database.h"
#include "mydumper_jobs.h"
#include "mydumper_global.h"
#include "mydumper_write.h"
#include "mydumper_common.h"
#include "mydumper_chunks.h"
#include "mydumper_integer_chunks.h"
#include "mydumper_char_chunks.h"
#include "mydumper_partition_chunks.h"
#include "mydumper_create_jobs.h"

GAsyncQueue *give_me_another_transactional_chunk_step_queue;
GAsyncQueue *give_me_another_non_transactional_chunk_step_queue;
GThread *chunk_builder=NULL;

void initialize_chunk(){
  give_me_another_transactional_chunk_step_queue=g_async_queue_new();
  give_me_another_non_transactional_chunk_step_queue=g_async_queue_new();
  initialize_char_chunk();
}

void start_chunk_builder(struct configuration *conf){
  if (!no_data){
    chunk_builder=g_thread_create((GThreadFunc)chunk_builder_thread, conf, TRUE, NULL);
  }
}

void finalize_chunk(){
  g_async_queue_unref(give_me_another_transactional_chunk_step_queue); 
  g_async_queue_unref(give_me_another_non_transactional_chunk_step_queue);
  if (!no_data){
    g_thread_join(chunk_builder);
  }
}

void process_none_chunk(struct table_job *tj, struct chunk_step_item * csi){
  (void)csi;
  write_table_job_into_file(tj);
}

void initialize_chunk_step_as_none(struct chunk_step_item * csi){
  csi->chunk_type=NONE;
  csi->chunk_functions.process=&process_none_chunk;
  csi->chunk_step = NULL;
}

struct chunk_step_item * new_none_chunk_step(){
  struct chunk_step_item * csi = g_new0(struct chunk_step_item, 1);
  initialize_chunk_step_as_none(csi);
  return csi;
}

struct chunk_step_item * initialize_chunk_step_item (MYSQL *conn, struct db_table *dbt, guint position, GString *prefix, guint64 rows) {
    struct chunk_step_item * csi=NULL;

    gchar *field=g_list_nth_data(dbt->primary_key, position);
    gchar *query = NULL;
    MYSQL_ROW row;
    MYSQL_RES *minmax = NULL;
    /* Get minimum/maximum */
    mysql_query(conn, query = g_strdup_printf(
                        "SELECT %s MIN(%s%s%s),MAX(%s%s%s),LEFT(MIN(%s%s%s),1),LEFT(MAX(%s%s%s),1) FROM %s%s%s.%s%s%s %s %s %s %s",
                        is_mysql_like()? "/*!40001 SQL_NO_CACHE */":"",
                        identifier_quote_character_str, field, identifier_quote_character_str, identifier_quote_character_str, field, identifier_quote_character_str,
                        identifier_quote_character_str, field, identifier_quote_character_str, identifier_quote_character_str, field, identifier_quote_character_str,
			identifier_quote_character_str, dbt->database->name, identifier_quote_character_str, identifier_quote_character_str, dbt->table, identifier_quote_character_str,
			where_option || (prefix && prefix->len>0) ? "WHERE" : "", where_option ? where_option : "", where_option && (prefix && prefix->len>0) ? "AND" : "", prefix && prefix->len>0 ? prefix->str : ""));
//    g_message("Query: %s", query);
    g_free(query);
    minmax = mysql_store_result(conn);

    if (!minmax){
      g_message("It is NONE with minmax == NULL");
      return new_none_chunk_step();
    }

    row = mysql_fetch_row(minmax);

    MYSQL_FIELD *fields = mysql_fetch_fields(minmax);
    gulong *lengths = mysql_fetch_lengths(minmax);
    /* Check if all values are NULL */
    if (row[0] == NULL){
      if (minmax)
        mysql_free_result(minmax);
      g_message("It is NONE with row == NULL");
      return new_none_chunk_step();
    }
  /* Support just bigger INTs for now, very dumb, no verify approach */
    guint64 diff_btwn_max_min;
    guint64 unmin, unmax;
    gint64 nmin, nmax;
//    union chunk_step *cs = NULL;
    switch (fields[0].type) {
      case MYSQL_TYPE_TINY:
      case MYSQL_TYPE_SHORT:
      case MYSQL_TYPE_LONG:
      case MYSQL_TYPE_LONGLONG:
      case MYSQL_TYPE_INT24:
        trace("Integer PK found on `%s`.`%s`",dbt->database->name, dbt->table);
        unmin = strtoull(row[0], NULL, 10);
        unmax = strtoull(row[1], NULL, 10);
        nmin  = strtoll (row[0], NULL, 10);
        nmax  = strtoll (row[1], NULL, 10);

        if (fields[0].flags & UNSIGNED_FLAG){
          diff_btwn_max_min=gint64_abs(unmax-unmin);
        }else{
          diff_btwn_max_min=gint64_abs(nmax-nmin);
        }

        gboolean unsign = fields[0].flags & UNSIGNED_FLAG;
        mysql_free_result(minmax);

        // If !(diff_btwn_max_min > min_chunk_step_size), then there is no need to split the table.
        if ( diff_btwn_max_min > dbt->min_chunk_step_size){
          union type type;

          if (unsign){
            type.unsign.min=unmin;
            type.unsign.max=unmax;
          }else{
            type.sign.min=nmin;
            type.sign.max=nmax;
          }

          gboolean is_step_fixed_length = dbt->min_chunk_step_size!=0 && dbt->min_chunk_step_size == dbt->starting_chunk_step_size && dbt->max_chunk_step_size == dbt->starting_chunk_step_size;
          csi = new_integer_step_item( TRUE, prefix, field, unsign, type, 0, is_step_fixed_length, dbt->starting_chunk_step_size, dbt->min_chunk_step_size, dbt->max_chunk_step_size, 0, FALSE, FALSE, NULL, position);

          if (dbt->multicolumn && csi->position == 0){
            if ((csi->chunk_step->integer_step.is_unsigned && (rows / (csi->chunk_step->integer_step.type.unsign.max - csi->chunk_step->integer_step.type.unsign.min) > (dbt->min_chunk_step_size==0?MIN_CHUNK_STEP_SIZE:dbt->min_chunk_step_size))
                )||(
               (!csi->chunk_step->integer_step.is_unsigned && (rows / gint64_abs(csi->chunk_step->integer_step.type.sign.max   - csi->chunk_step->integer_step.type.sign.min)   > (dbt->min_chunk_step_size==0?MIN_CHUNK_STEP_SIZE:dbt->min_chunk_step_size))
              )
              )){
              csi->chunk_step->integer_step.min_chunk_step_size=1;
              csi->chunk_step->integer_step.is_step_fixed_length=TRUE;
              csi->chunk_step->integer_step.max_chunk_step_size=1;
              csi->chunk_step->integer_step.step=1;
            }else
              dbt->multicolumn=FALSE;
          }


          if (csi->chunk_step->integer_step.is_step_fixed_length){
            if (csi->chunk_step->integer_step.is_unsigned){
              csi->chunk_step->integer_step.type.unsign.min=(csi->chunk_step->integer_step.type.unsign.min/csi->chunk_step->integer_step.step)*csi->chunk_step->integer_step.step;
            }else{
              csi->chunk_step->integer_step.type.sign.min=(csi->chunk_step->integer_step.type.sign.min/csi->chunk_step->integer_step.step)*csi->chunk_step->integer_step.step;
            }
          }

          if (dbt->min_chunk_step_size==dbt->starting_chunk_step_size && dbt->max_chunk_step_size==dbt->starting_chunk_step_size && dbt->min_chunk_step_size != 0)
            dbt->chunk_filesize=0;
          return csi;



        }else{
          trace("Integer PK on `%s`.`%s` performing full table scan",dbt->database->name, dbt->table);
          return new_none_chunk_step();
        }
        break;
      case MYSQL_TYPE_STRING:
      case MYSQL_TYPE_VAR_STRING:

        if (minmax)
          mysql_free_result(minmax);
        return new_none_chunk_step();

        csi=new_char_step_item(conn, TRUE, prefix, dbt->primary_key->data, 0, 0, row, lengths, NULL);
        if (minmax)
          mysql_free_result(minmax);
        return csi;
        break;
      default:
        if (minmax)
          mysql_free_result(minmax);
        g_message("It is NONE: default");
        return new_none_chunk_step();
        break;
      }

  return NULL;
}


guint64 get_rows_from_explain(MYSQL * conn, struct db_table *dbt, GString *where, gchar *field){
  gchar *query = NULL;
  MYSQL_ROW row = NULL;
  MYSQL_RES *res= NULL;
  /* Get minimum/maximum */

  mysql_query(conn, query = g_strdup_printf(
                        "EXPLAIN SELECT %s %s%s%s FROM %s%s%s.%s%s%s%s%s",
                        is_mysql_like() ? "/*!40001 SQL_NO_CACHE */": "",
                        field?identifier_quote_character_str:"", field?field:"*", field?identifier_quote_character_str:"",
                        identifier_quote_character_str, dbt->database->name, identifier_quote_character_str, identifier_quote_character_str, dbt->table, identifier_quote_character_str,
                        where?" WHERE ":"",where?where->str:""));

  g_free(query);
  res = mysql_store_result(conn);

  guint row_col=-1;

  if (!res){
    return 0;
  }
  determine_explain_columns(res, &row_col);
  row = mysql_fetch_row(res);

  if (row==NULL || row[row_col]==NULL){
    mysql_free_result(res);
    return 0;
  }
  guint64 rows_in_explain = strtoull(row[row_col], NULL, 10);
  mysql_free_result(res);
  return rows_in_explain;
}

static
guint64 get_rows_from_count(MYSQL * conn, struct db_table *dbt)
{
  char *query= g_strdup_printf("SELECT %s COUNT(*) FROM %s%s%s.%s%s%s",
                               is_mysql_like() ? "/*!40001 SQL_NO_CACHE */": "",
                               identifier_quote_character_str, dbt->database->name, identifier_quote_character_str, identifier_quote_character_str, dbt->table, identifier_quote_character_str);
  mysql_query(conn, query);

  g_free(query);
  MYSQL_RES *res= mysql_store_result(conn);
  MYSQL_ROW row= mysql_fetch_row(res);

  if (!row || !row[0]) {
    mysql_free_result(res);
    return 0;
  }
  guint64 rows= strtoull(row[0], NULL, 10);
  mysql_free_result(res);
  return rows;
}


void set_chunk_strategy_for_dbt(MYSQL *conn, struct db_table *dbt){
  g_mutex_lock(dbt->chunks_mutex);
  struct chunk_step_item * csi = NULL;
  guint64 rows;
  if (check_row_count) {
    rows= get_rows_from_count(conn, dbt);
  } else
    rows= get_rows_from_explain(conn, dbt, NULL ,NULL);
  g_message("%s.%s has %s%lu rows", dbt->database->name, dbt->table,
            (check_row_count ? "": "~"), rows);
  dbt->rows_total= rows;
  if (rows > (dbt->min_chunk_step_size!=0?dbt->min_chunk_step_size:MIN_CHUNK_STEP_SIZE)){
    GList *partitions=NULL;
    if (split_partitions || dbt->partition_regex){
      partitions = get_partitions_for_table(conn, dbt);
    }
    if (partitions){
      csi=new_real_partition_step_item(partitions,0,0);
    }else{
      if (dbt->split_integer_tables) {
        csi = initialize_chunk_step_item(conn, dbt, 0, NULL, rows);
      }else{
        csi = new_none_chunk_step();
      }
    }
  }else{
    csi = new_none_chunk_step();
  }
//  dbt->initial_chunk_step=csi;
  dbt->chunks=g_list_prepend(dbt->chunks,csi);
  g_async_queue_push(dbt->chunks_queue, csi);
  dbt->status=READY;
  g_mutex_unlock(dbt->chunks_mutex);
}

gboolean get_next_dbt_and_chunk_step_item(struct db_table **dbt_pointer,struct chunk_step_item **csi, struct MList *dbt_list){
  g_mutex_lock(dbt_list->mutex);
  GList *iter=dbt_list->list;
  struct db_table *dbt;
  gboolean are_there_jobs_defining=FALSE;
  struct chunk_step_item *lcs;
//  struct chunk_step_item *(*get_next)(struct db_table *dbt);
  while (iter){
    dbt=iter->data;
    g_mutex_lock(dbt->chunks_mutex);
//    g_message("Checking table: %s.%s", d->database->name, d->table);
    if (dbt->status != DEFINING){

      if (dbt->status == UNDEFINED){
//        g_message("Checking table: %s.%s DEFINING NOW", d->database->name, d->table);
        *dbt_pointer=iter->data;
        dbt->status = DEFINING;
        are_there_jobs_defining=TRUE;
        g_mutex_unlock(dbt->chunks_mutex);
        break;
      }

      // Set by set_chunk_strategy_for_dbt() in working_thread()
      g_assert(dbt->status == READY);

      // Initially chunks are set by set_chunk_strategy_for_dbt() and then by
      // chunk_functions.get_next(d) (see below)
      if (dbt->chunks == NULL){
        g_mutex_unlock(dbt->chunks_mutex);
        goto next;
      }

      lcs = (struct chunk_step_item *)g_list_first(dbt->chunks)->data;
      if (lcs->chunk_type == NONE){
        *dbt_pointer=iter->data;
        *csi = lcs;
        dbt_list->list=g_list_remove(dbt_list->list,dbt);
        g_mutex_unlock(dbt->chunks_mutex);
        break;
      }

      if (dbt->max_threads_per_table <= dbt->current_threads_running){
        g_mutex_unlock(dbt->chunks_mutex);
        goto next;
      }
      dbt->current_threads_running++;
      lcs=lcs->chunk_functions.get_next(dbt);

      if (lcs!=NULL){
        *dbt_pointer=iter->data;
        *csi = lcs;
        g_mutex_unlock(dbt->chunks_mutex);
        break;
      }else{
        iter=iter->next;
        // Assign iter previous removing dbt from list is important as we might break the list
        dbt_list->list=g_list_remove(dbt_list->list,dbt);
        g_mutex_unlock(dbt->chunks_mutex);
        continue;
      }
    }else{
      g_mutex_unlock(dbt->chunks_mutex);
      are_there_jobs_defining=TRUE;
    }
next:
    iter=iter->next;
  }
  g_mutex_unlock(dbt_list->mutex);
  return are_there_jobs_defining;
}

static
void enqueue_shutdown_jobs(GAsyncQueue * queue){
  struct job *j=NULL;
  guint n;
  for (n = 0; n < num_threads; n++) {
    j = g_new0(struct job, 1);
    j->type = JOB_SHUTDOWN;
    g_async_queue_push(queue, j);
  }
}

static inline
void enqueue_shutdown(struct table_queuing *q)
{
  enqueue_shutdown_jobs(q->queue);
  enqueue_shutdown_jobs(q->defer);
}

static
void table_job_enqueue(struct table_queuing *q)
{
  struct db_table *dbt;
  struct chunk_step_item *csi;
  gboolean are_there_jobs_defining=FALSE;
  g_message("Starting to enqueue %s tables", q->descr);
  for (;;) {
    g_async_queue_pop(q->request_chunk);
    if (shutdown_triggered) {
      break;
    }
    dbt=NULL;
    csi=NULL;
    are_there_jobs_defining=FALSE;
    are_there_jobs_defining= get_next_dbt_and_chunk_step_item(&dbt, &csi, q->table_list);

    if (dbt!=NULL){

      if (dbt->status == DEFINING){
        create_job_to_determine_chunk_type(dbt, g_async_queue_push, q->queue);
        continue;
      }

      if (csi!=NULL){
        switch (csi->chunk_type) {
        case INTEGER:
          if (use_defer) {
            create_job_to_dump_chunk(dbt, NULL, csi->number,
                                     csi, g_async_queue_push, q->defer);
            create_job_defer(dbt, q->queue);
          } else {
            create_job_to_dump_chunk(dbt, NULL, csi->number,
                                     csi, g_async_queue_push, q->queue);
          }
          break;
        case CHAR:
          create_job_to_dump_chunk(dbt, NULL, csi->number, csi, g_async_queue_push, q->queue);
          break;
        case PARTITION:
          create_job_to_dump_chunk(dbt, NULL, csi->number, csi, g_async_queue_push, q->queue);
          break;
        case NONE:
          create_job_to_dump_chunk(dbt, NULL, 0, csi, g_async_queue_push, q->queue);
          break;
        default:
          m_error("This should not happen %s", csi->chunk_type);
          break;
        }
      }
    }else{
      if (are_there_jobs_defining){
//        g_debug("chunk_builder_thread: Are jobs defining... should we wait and try again later?");
        g_async_queue_push(q->request_chunk, GINT_TO_POINTER(1));
        usleep(1);
        continue;
      }
//      g_debug("chunk_builder_thread: There were not job defined");
      break;
    }
  }
  g_message("Enqueuing of %s tables completed", q->descr);
  enqueue_shutdown(q);
}

void *chunk_builder_thread(struct configuration *conf)
{
  table_job_enqueue(&conf->non_transactional);
  table_job_enqueue(&conf->transactional);
  return NULL;
}
