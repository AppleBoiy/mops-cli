#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <dirent.h>
#include <ctype.h>
#include "mops.h"

/*
 * A minimal JSON string escaper.
 * It handles quotes, backslashes, and basic control characters.
 */
static void print_json_string(const char *str) {
    if (!str) {
        printf("\"\"");
        return;
    }
    printf("\"");
    for (int i = 0; str[i] != '\0'; i++) {
        unsigned char c = str[i];
        if (c == '"' || c == '\\') {
            putchar('\\');
            putchar(c);
        } else if (c < 32 || c == 127) {
            printf("\\u%04x", c);
        } else {
            putchar(c);
        }
    }
    printf("\"");
}

/* Declare external DB function from db.c */
extern sqlite3* db_get_connection(void);

/*
 * Database Helpers
 */

static int insert_task(int pid, const char *cmd, const char *status) {
    sqlite3 *db = db_get_connection();
    if (!db) return -1;

    sqlite3_stmt *stmt;
    int id = -1;
    const char *sql = "INSERT INTO tasks (pid, command, status) VALUES (?, ?, ?)";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, pid);
        sqlite3_bind_text(stmt, 2, cmd, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, status, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            id = (int)sqlite3_last_insert_rowid(db);
        }
        sqlite3_finalize(stmt);
    } else {
        fprintf(stderr, "Failed to prepare DB statement: %s\n", sqlite3_errmsg(db));
    }
    return id;
}

static void update_task_status(int id, const char *status) {
    sqlite3 *db = db_get_connection();
    if (!db) return;

    sqlite3_stmt *stmt;
    const char *sql = "UPDATE tasks SET status = ? WHERE id = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, status, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

static void update_task_pid(int id, int pid) {
    sqlite3 *db = db_get_connection();
    if (!db) return;

    sqlite3_stmt *stmt;
    const char *sql = "UPDATE tasks SET pid = ? WHERE id = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, pid);
        sqlite3_bind_int(stmt, 2, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

/*
 * Webhook Notification Helper
 */

static void notify_webhook(const char *url, int task_id, int exit_code) {
    char curl_cmd[1024];
    snprintf(curl_cmd, sizeof(curl_cmd),
             "curl -s -X POST -H 'Content-Type: application/json' "
             "-d '{\"task_id\":%d, \"exit_code\":%d}' %s > /dev/null",
             task_id, exit_code, url);
    if (system(curl_cmd) == -1) {
        /* Ignore execution failures for background webhooks */
    }
}

/*
 * Subcommand Implementations
 */

int cmd_task_exec(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops task exec \"<command>\" [--notify <url>]\n");
        return 1;
    }

    const char *cmd = argv[1];
    const char *notify_url = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--notify") == 0 && i + 1 < argc) {
            notify_url = argv[i+1];
            break;
        }
    }

    int task_id = insert_task(getpid(), cmd, "RUNNING");

    printf("Executing synchronously (Task ID %d): %s\n", task_id, cmd);
    int ret = system(cmd);
    int exit_code = 1;

    if (ret != -1) {
        exit_code = WEXITSTATUS(ret);
    } else {
        perror("system");
    }

    update_task_status(task_id, exit_code == 0 ? "FINISHED" : "FAILED");

    if (notify_url) {
        notify_webhook(notify_url, task_id, exit_code);
    }

    return exit_code;
}

int cmd_task_bg(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops task bg \"<command>\" [--notify <url>]\n");
        return 1;
    }

    const char *cmd = argv[1];
    const char *notify_url = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--notify") == 0 && i + 1 < argc) {
            notify_url = argv[i+1];
            break;
        }
    }

    int task_id = insert_task(0, cmd, "STARTING");
    if (task_id < 0) {
        fprintf(stderr, "Failed to register background task in database.\n");
        return 1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        return 1;
    }

    if (pid > 0) {
        /* Parent process */
        update_task_pid(task_id, pid);
        update_task_status(task_id, "RUNNING");
        printf("Started background task (Task ID %d, PID %d): %s\n", task_id, pid, cmd);
        return 0;
    } else {
        /* Child process */

        /* 1. Create a new session and detach from controlling terminal */
        if (setsid() < 0) {
            exit(EXIT_FAILURE);
        }

        /* 2. Redirect standard I/O to task-specific log files */
        char log_path[256];
        snprintf(log_path, sizeof(log_path), "/tmp/mops_task_%d.log", task_id);

        int fd_in = open("/dev/null", O_RDONLY);
        int fd_out = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);

        if (fd_in >= 0) {
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }
        if (fd_out >= 0) {
            dup2(fd_out, STDOUT_FILENO);
            dup2(fd_out, STDERR_FILENO);
            close(fd_out);
        }

        /* 3. Execute the command via shell */
        int ret = system(cmd);
        int exit_code = 1;
        if (ret != -1 && WIFEXITED(ret)) {
            exit_code = WEXITSTATUS(ret);
        }

        /* 4. Update status dynamically from the background process */
        update_task_status(task_id, exit_code == 0 ? "FINISHED" : "FAILED");

        /* 5. Trigger webhook if requested */
        if (notify_url) {
            notify_webhook(notify_url, task_id, exit_code);
        }

        exit(exit_code);
    }
}

int cmd_task_submit(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops task submit \"<command>\"\n");
        return 1;
    }

    const char *cmd = argv[1];
    int id = insert_task(0, cmd, "QUEUED");
    if (id > 0) {
        printf("Task %d submitted to queue: %s\n", id, cmd);
    } else {
        fprintf(stderr, "Failed to submit task to queue.\n");
        return 1;
    }
    return 0;
}


int cmd_task_list(int argc, char **argv) {
    int json = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) json = 1;
    }

    sqlite3 *db = db_get_connection();
    if (!db) {
        if (json) printf("{\"error\":\"Database connection not available\"}\n");
        else fprintf(stderr, "Database connection not available.\n");
        return 1;
    }

    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, pid, command, status FROM tasks";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 1;
    }

    if (json) {
        printf("[");
    } else {
        printf("%-5s | %-10s | %-12s | %s\n", "ID", "PID", "STATUS", "COMMAND");
        printf("--------------------------------------------------------------\n");
    }

    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        int pid = sqlite3_column_int(stmt, 1);
        const unsigned char *cmd = sqlite3_column_text(stmt, 2);
        const unsigned char *status = sqlite3_column_text(stmt, 3);

        char current_status[32];
        snprintf(current_status, sizeof(current_status), "%s", status);

        /*
         * Check if a running task is actually still alive.
         */
        if (strcmp(current_status, "RUNNING") == 0 && pid > 0) {
            if (kill(pid, 0) != 0) {
                /*
                 * Process doesn't exist or we have no permission;
                 * assume dead.
                 */
                snprintf(current_status, sizeof(current_status), "FINISHED");
                update_task_status(id, "FINISHED");
            }
        }

        if (json) {
            if (!first) printf(",");
            printf("{\"id\":%d,\"pid\":%d,\"status\":\"%s\",\"command\":", id, pid, current_status);
            print_json_string((const char*)cmd);
            printf("}");
            first = 0;
        } else {
            printf("%-5d | %-10d | %-12s | %s\n", id, pid, current_status, cmd);
        }
    }

    if (json) {
        printf("]\n");
    }
    sqlite3_finalize(stmt);
    return 0;
}


int cmd_task_kill(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops task kill <task_id>\n");
        return 1;
    }

    int task_id = atoi(argv[1]);
    if (task_id <= 0) {
        fprintf(stderr, "Invalid task ID.\n");
        return 1;
    }

    sqlite3 *db = db_get_connection();
    if (!db) return 1;

    sqlite3_stmt *stmt;
    const char *sql = "SELECT pid, status FROM tasks WHERE id = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare query.\n");
        return 1;
    }

    sqlite3_bind_int(stmt, 1, task_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int pid = sqlite3_column_int(stmt, 0);
        const unsigned char *status = sqlite3_column_text(stmt, 1);

        if (strcmp((const char *)status, "RUNNING") == 0) {
            if (kill(pid, SIGTERM) == 0) {
                printf("Sent SIGTERM to task %d (PID %d)\n", task_id, pid);
                update_task_status(task_id, "KILLED");
            } else {
                perror("Failed to send SIGTERM");
            }
        } else {
            printf("Task %d is not RUNNING (Current status: %s)\n", task_id, status);
        }
    } else {
        printf("Task ID %d not found.\n", task_id);
    }

    sqlite3_finalize(stmt);
    return 0;
}


int cmd_task_logs(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops task logs <task_id> [--tail]\n");
        return 1;
    }

    int task_id = atoi(argv[1]);
    int tail = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--tail") == 0) tail = 1;
    }

    char log_path[256];
    snprintf(log_path, sizeof(log_path), "/tmp/mops_task_%d.log", task_id);

    if (access(log_path, F_OK) != 0) {
        fprintf(stderr, "Log file not found for Task %d: %s\n", task_id, log_path);
        return 1;
    }

    if (tail) {
        execlp("tail", "tail", "-f", log_path, NULL);
    } else {
        execlp("cat", "cat", log_path, NULL);
    }

    perror("execlp");
    return 1;
}

int cmd_task_clean(int argc, char **argv) {
    int force = 0;
    int json = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--force") == 0) force = 1;
        if (strcmp(argv[i], "--json") == 0) json = 1;
    }

    DIR *dir = opendir("/proc");
    if (!dir) {
        if (json) printf("{\"error\":\"Could not open /proc\"}\n");
        else perror("opendir /proc");
        return 1;
    }

    struct dirent *ent;
    int found = 0;
    int first = 1;

    if (json) {
        printf("[");
    } else {
        printf("Scanning for zombie processes and orphaned workers...\n");
    }

    while ((ent = readdir(dir)) != NULL) {
        if (!isdigit((unsigned char)ent->d_name[0])) continue;

        char path[512];
        snprintf(path, sizeof(path), "/proc/%s/stat", ent->d_name);

        FILE *fp = fopen(path, "r");
        if (!fp) continue;

        int pid, ppid;
        char comm[256];
        char state;

        /*
         * Parse /proc/[pid]/stat
         * Format: pid (comm) state ppid ...
         */
        if (fscanf(fp, "%d (%255[^)]) %c %d", &pid, comm, &state, &ppid) == 4) {
            int is_zombie = (state == 'Z');
            int is_orphan = (ppid == 1 && (strstr(comm, "python") != NULL || strstr(comm, "worker") != NULL));

            if (is_zombie || is_orphan) {
                if (json) {
                    if (!first) printf(",");
                    printf("{");
                    if (is_zombie) {
                        printf("\"type\":\"zombie\",\"pid\":%d,\"comm\":", pid);
                        print_json_string(comm);
                        printf(",\"ppid\":%d", ppid);
                    } else { // is_orphan
                        printf("\"type\":\"orphan\",\"pid\":%d,\"comm\":", pid);
                        print_json_string(comm);
                        printf(",\"killed\":%s", force ? "true" : "false");
                    }
                    printf("}");
                    first = 0;
                } else {
                    if (is_zombie) {
                        printf("- Zombie process found: PID %d (%s) PPID %d\n", pid, comm, ppid);
                    } else { // is_orphan
                        printf("- Orphaned worker found: PID %d (%s)\n", pid, comm);
                        if (force) {
                            printf("  -> Sending SIGKILL to PID %d\n", pid);
                        }
                    }
                }
                if (is_orphan && force) {
                    kill(pid, SIGKILL);
                }
                found++;
            }
        }
        fclose(fp);
    }
    closedir(dir);

    if (json) {
        printf("]\n");
    } else {
        if (!found) {
            printf("System is clean. No zombies or orphaned workers found.\n");
        } else if (!force) {
            printf("\nRun 'mops task clean --force' to aggressively terminate orphaned workers.\n");
        }
    }

    return 0;
}

/*
 * Main Dispatcher
 */

int cmd_task(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops task <command> [options]\n");
        fprintf(stderr, "Run 'mops task --help' for more information.\n");
        return 1;
    }

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "--help") == 0 || strcmp(subcmd, "-h") == 0) {
        printf("Usage: mops task <command> [options]\n\n");
        printf("Commands:\n");
        printf("  submit (qsub, queue)   Submit a command to the worker queue\n");
        printf("  list (qstat)           List all tracked tasks and their status\n");
        printf("  kill (qdel)            Send SIGTERM to a running task\n");
        printf("  exec                   Execute a command synchronously\n");
        printf("  bg                     Start a background task and track it\n");
        printf("  logs                   Read or tail stdout/stderr for a task\n");
        printf("  clean                  Sweep zombie processes and orphaned workers\n\n");
        printf("Options:\n");
        printf("  --notify <url>         Send a webhook upon task completion ('exec', 'bg')\n");
        printf("  --tail                 Tail log output ('logs')\n");
        printf("  --force                Aggressively kill orphaned workers ('clean')\n");
        printf("  --json                 Output in JSON format ('list', 'clean')\n");
        return 0;
    }

    if (strcmp(subcmd, "exec") == 0) {
        return cmd_task_exec(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "bg") == 0) {
        return cmd_task_bg(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "submit") == 0 || strcmp(subcmd, "queue") == 0 || strcmp(subcmd, "qsub") == 0) {
        return cmd_task_submit(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "list") == 0 || strcmp(subcmd, "qstat") == 0) {
        return cmd_task_list(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "kill") == 0 || strcmp(subcmd, "qdel") == 0) {
        return cmd_task_kill(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "logs") == 0) {
        return cmd_task_logs(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "clean") == 0) {
        return cmd_task_clean(argc - 1, argv + 1);
    } else {
        fprintf(stderr, "Unknown task subcommand: %s\n", subcmd);
        fprintf(stderr, "Run 'mops task --help' for more information.\n");
        return 1;
    }
}