#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <sqlite3.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include "mops.h"

#define PID_FILE "/tmp/mops_worker.pid"
#define POLL_INTERVAL 5 /* seconds */

static volatile int running = 1;

/*
 * Signal handler for graceful shutdown.
 */
static void handle_sigterm(int sig) {
    (void)sig;
    running = 0;
}

/*
 * Trigger a webhook notification upon task completion.
 */
static void notify_webhook_from_worker(int task_id, int exit_code, const char *url) {
    char curl_cmd[2048];
    snprintf(curl_cmd, sizeof(curl_cmd), 
             "curl -s -X POST -H 'Content-Type: application/json' "
             "-d '{\"task_id\":%d, \"exit_code\":%d}' %s > /dev/null 2>&1", 
             task_id, exit_code, url);
    if (system(curl_cmd) == -1) {
        /* This runs in a daemon, so we can't easily log failure. Fire and forget. */
    }
}

/*
 * The core loop that polls the database and executes queued tasks.
 */
static void run_worker_loop(void) {
    signal(SIGTERM, handle_sigterm);

    while (running) {
        sqlite3 *db = db_get_connection();
        if (!db) {
            sleep(POLL_INTERVAL);
            continue;
        }

        sqlite3_stmt *stmt;
        const char *sql = "SELECT id, command, notify_url FROM tasks WHERE status = 'QUEUED' ORDER BY id ASC LIMIT 1";

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                int task_id = sqlite3_column_int(stmt, 0);
                const char *cmd = (const char *)sqlite3_column_text(stmt, 1);
                const char *notify_url = (const char *)sqlite3_column_text(stmt, 2);

                char cmd_buf[2048];
                char notify_url_buf[2048] = {0};
                strncpy(cmd_buf, cmd, sizeof(cmd_buf) - 1);
                if (notify_url) {
                    strncpy(notify_url_buf, notify_url, sizeof(notify_url_buf) - 1);
                }
                
                sqlite3_finalize(stmt);

                sqlite3_stmt *update_stmt;
                sqlite3_prepare_v2(db, "UPDATE tasks SET status = 'RUNNING' WHERE id = ?", -1, &update_stmt, NULL);
                sqlite3_bind_int(update_stmt, 1, task_id);
                sqlite3_step(update_stmt);
                sqlite3_finalize(update_stmt);

                int ret = system(cmd_buf);
                int exit_code = WIFEXITED(ret) ? WEXITSTATUS(ret) : 1;

                const char *final_status = (exit_code == 0) ? "FINISHED" : "FAILED";
                sqlite3_prepare_v2(db, "UPDATE tasks SET status = ? WHERE id = ?", -1, &update_stmt, NULL);
                sqlite3_bind_text(update_stmt, 1, final_status, -1, SQLITE_STATIC);
                sqlite3_bind_int(update_stmt, 2, task_id);
                sqlite3_step(update_stmt);
                sqlite3_finalize(update_stmt);
                
                if (notify_url && strlen(notify_url_buf) > 0) {
                     notify_webhook_from_worker(task_id, exit_code, notify_url_buf);
                }

                continue; /* Check for another job immediately */
            } else {
                 sqlite3_finalize(stmt);
            }
        }
        
        sleep(POLL_INTERVAL);
    }
    remove(PID_FILE);
}

/*
 * Standard UNIX daemonization procedure.
 */
static int daemonize() {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) return -1;

    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    if (chdir("/") != 0) {
        return -1;
    }

    for (int x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
        close(x);
    }

    open("/dev/null", O_RDWR);
    open("/dev/null", O_RDWR);
    open("/dev/null", O_RDWR);
    
    return 0;
}

int cmd_worker_start(int argc, char **argv) {
    (void)argc; (void)argv;

    FILE *fp = fopen(PID_FILE, "r");
    if (fp) {
        pid_t pid;
        if (fscanf(fp, "%d", &pid) == 1) {
            if (kill(pid, 0) == 0) {
                fprintf(stderr, "Error: mops worker is already running (PID: %d).\n", pid);
                fclose(fp);
                return 1;
            } else {
                /* Stale PID file; remove it before starting */
                fclose(fp);
                remove(PID_FILE);
            }
        } else {
            fclose(fp);
            /* Corrupt PID file; remove it */
            remove(PID_FILE);
        }
    }
    
    printf("Starting mops worker daemon...\n");
    if (daemonize() != 0) {
        perror("Failed to daemonize");
        return 1;
    }

    fp = fopen(PID_FILE, "w");
    if (!fp) {
        /* Could not write PID file; abort */
        return 1;
    }
    fprintf(fp, "%d\n", getpid());
    fflush(fp);
    int fd = fileno(fp);
    if (fd >= 0) {
        fsync(fd);
    }
    fclose(fp);
    
    /* Initialize database with retries to avoid transient failures */
    int init_rc = -1;
    for (int attempt = 0; attempt < 5; attempt++) {
        init_rc = db_init();
        if (init_rc == 0) break;
        sleep(1);
    }
    if (init_rc != 0) {
        /* Clean up PID file on failure to avoid stale state */
        remove(PID_FILE);
        return 1;
    }

    run_worker_loop();
    return 0;
}

int cmd_worker_stop(int argc, char **argv) {
    (void)argc; (void)argv;

    FILE *fp = fopen(PID_FILE, "r");
    if (!fp) {
        fprintf(stderr, "mops worker is not running (PID file not found).\n");
        return 1;
    }

    pid_t pid;
    if (fscanf(fp, "%d", &pid) != 1) {
        fclose(fp);
        fprintf(stderr, "Error: Could not read PID from file.\n");
        return 1;
    }
    fclose(fp);

    if (kill(pid, SIGTERM) == 0) {
        printf("Sent SIGTERM to mops worker (PID %d).\n", pid);
        /* Wait up to 10 seconds for graceful shutdown */
        int exited = 0;
        for (int i = 0; i < 100; i++) {
            if (kill(pid, 0) != 0) {
                exited = 1;
                break;
            }
            usleep(100000); /* 100ms */
        }
        if (exited) {
            printf("Worker has shut down.\n");
            remove(PID_FILE);
        } else {
            fprintf(stderr, "Worker did not exit after SIGTERM; sending SIGKILL.\n");
            if (kill(pid, SIGKILL) == 0) {
                /* Wait briefly for SIGKILL to take effect */
                for (int i = 0; i < 50; i++) {
                    if (kill(pid, 0) != 0) break;
                    usleep(100000);
                }
                if (kill(pid, 0) != 0) {
                    printf("Worker was forcefully terminated.\n");
                    remove(PID_FILE);
                } else {
                    fprintf(stderr, "Failed to terminate worker PID %d.\n", pid);
                    return 1;
                }
            } else {
                if (errno == ESRCH) {
                    /* Already gone */
                    remove(PID_FILE);
                } else {
                    perror("Failed to send SIGKILL to worker");
                    return 1;
                }
            }
        }
    } else {
        if (errno == ESRCH) {
            fprintf(stderr, "Worker with PID %d not found. Removing stale PID file.\n", pid);
            remove(PID_FILE);
        } else {
            perror("Failed to send SIGTERM to worker");
        }
        return 1;
    }
    return 0;
}

int cmd_worker_status(int argc, char **argv) {
    (void)argc; (void)argv;
    
    FILE *fp = fopen(PID_FILE, "r");
    if (!fp) {
        printf("mops worker: stopped\n");
        return 1;
    }
    
    pid_t pid;
    if (fscanf(fp, "%d", &pid) != 1) {
        printf("mops worker: unknown (invalid PID file)\n");
        fclose(fp);
        return 1;
    }
    fclose(fp);

    if (kill(pid, 0) == 0) {
        printf("mops worker: running (PID %d)\n", pid);
    } else {
        printf("mops worker: stopped (stale PID file found)\n");
        remove(PID_FILE);
        return 1;
    }
    return 0;
}

/*
 * Main dispatcher for `worker` subcommand.
 */
int cmd_worker(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops worker <start|stop|status>\n");
        return 1;
    }

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "start") == 0) {
        return cmd_worker_start(argc - 2, argv + 2);
    } else if (strcmp(subcmd, "stop") == 0) {
        return cmd_worker_stop(argc - 2, argv + 2);
    } else if (strcmp(subcmd, "status") == 0) {
        return cmd_worker_status(argc - 2, argv + 2);
    } else {
        fprintf(stderr, "Unknown worker command: %s\n", subcmd);
        fprintf(stderr, "Usage: mops worker <start|stop|status>\n");
        return 1;
    }
}