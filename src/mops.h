#ifndef MOPS_H
#define MOPS_H

#include <sqlite3.h>

/*
 * Database Operations
 */
int db_init(void);
void db_close(void);
sqlite3 *db_get_connection(void);

/*
 * Subcommand Dispatchers
 */
int cmd_disk(int argc, char **argv);
int cmd_sys(int argc, char **argv);
int cmd_net(int argc, char **argv);
int cmd_gcp(int argc, char **argv);
int cmd_task(int argc, char **argv);
int cmd_worker(int argc, char **argv);

/*
 * System Operations
 */
int cmd_sys_cpu(int argc, char **argv);
int cmd_sys_gpu(int argc, char **argv);
int cmd_sys_tpu(int argc, char **argv);

/*
 * Net Operations
 */
int cmd_net_port(int argc, char **argv);

/*
 * GCP Operations
 */
int cmd_gcp_whoami(int argc, char **argv);
int cmd_gcp_spot_watch(int argc, char **argv);
int cmd_gcp_tunnel(int argc, char **argv);
int cmd_gcp_run_with_secrets(int argc, char **argv);


/*
 * Task Operations
 */
int cmd_task_exec(int argc, char **argv);
int cmd_task_bg(int argc, char **argv);
int cmd_task_submit(int argc, char **argv);
int cmd_task_list(int argc, char **argv);
int cmd_task_kill(int argc, char **argv);
int cmd_task_logs(int argc, char **argv);
int cmd_task_clean(int argc, char **argv);

/*
 * Worker Operations
 */
int cmd_worker_start(int argc, char **argv);
int cmd_worker_stop(int argc, char **argv);
int cmd_worker_status(int argc, char **argv);


#endif /* MOPS_H */