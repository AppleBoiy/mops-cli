#ifndef MOPS_H
#define MOPS_H

#include <sqlite3.h>

/* --- Database Operations --- */
int db_init(void);
sqlite3 *db_get_connection(void);

/* --- Subcommand Dispatchers --- */
int cmd_disk(int argc, char **argv);
int cmd_sys(int argc, char **argv);
int cmd_task(int argc, char **argv);

/* --- System Operations --- */
int cmd_sys_cpu(int argc, char **argv);
int cmd_sys_gpu(int argc, char **argv);
int cmd_sys_tpu(int argc, char **argv);

/* --- Task Operations --- */
int cmd_task_exec(int argc, char **argv);
int cmd_task_bg(int argc, char **argv);
int cmd_task_queue(int argc, char **argv);
int cmd_task_list(int argc, char **argv);
int cmd_task_kill(int argc, char **argv);

#endif /* MOPS_H */