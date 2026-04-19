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
#include "mops.h"

/* Declare external DB function from db.c */
extern sqlite3* db_get_connection(void);

/* --- Database Helpers --- */

static void insert_task(int pid, const char *cmd, const char *status) {
    sqlite3 *db = db_get_connection();
    if (!db) return;

    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO tasks (pid, command, status) VALUES (?, ?, ?)";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, pid);
        sqlite3_bind_text(stmt, 2, cmd, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, status, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    } else {
        fprintf(stderr, "Failed to prepare DB statement: %s\n", sqlite3_errmsg(db));
    }
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

/* --- Subcommand Implementations --- */

int cmd_task_exec(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops task exec \"<command>\"\n");
        return 1;
    }
    
    const char *cmd = argv[1];
    printf("Executing synchronously: %s\n", cmd);
    
    int ret = system(cmd);
    if (ret == -1) {
        perror("system");
        return 1;
    }
    
    return WEXITSTATUS(ret);
}

int cmd_task_bg(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops task bg \"<command>\"\n");
        return 1;
    }
    
    const char *cmd = argv[1];
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("fork failed");
        return 1;
    }
    
    if (pid > 0) {
        /* Parent process */
        insert_task(pid, cmd, "RUNNING");
        printf("Started background task (PID %d): %s\n", pid, cmd);
        return 0;
    } else {
        /* Child process */
        
        /* 1. Create a new session and detach from controlling terminal */
        if (setsid() < 0) {
            exit(EXIT_FAILURE);
        }
        
        /* 2. Redirect standard I/O */
        int fd_in = open("/dev/null", O_RDONLY);
        int fd_out = open("/tmp/mops_bg.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        
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
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        
        /* 4. Exit if execl fails */
        exit(EXIT_FAILURE);
    }
}

#ifdef DEV_MODE
int cmd_task_queue(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops task queue \"<command>\" | --exec\n");
        return 1;
    }
    
    if (strcmp(argv[1], "--exec") == 0) {
        sqlite3 *db = db_get_connection();
        if (!db) return 1;
        
        sqlite3_stmt *stmt;
        const char *sql = "SELECT id, command FROM tasks WHERE status = 'QUEUED' ORDER BY id ASC";
        
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
            fprintf(stderr, "Failed to query queued tasks.\n");
            return 1;
        }
        
        int found = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            found = 1;
            int id = sqlite3_column_int(stmt, 0);
            const char *cmd_text = (const char *)sqlite3_column_text(stmt, 1);
            
            printf("Executing queued task %d: %s\n", id, cmd_text);
            update_task_status(id, "RUNNING");
            
            int ret = system(cmd_text);
            if (ret == 0) {
                update_task_status(id, "FINISHED");
            } else {
                update_task_status(id, "FAILED");
            }
        }
        
        sqlite3_finalize(stmt);
        
        if (!found) {
            printf("No queued tasks to execute.\n");
        }
        return 0;
    }
    
    const char *cmd = argv[1];
    insert_task(0, cmd, "QUEUED");
    printf("Task added to queue: %s\n", cmd);
    return 0;
}
#endif

int cmd_task_list(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    sqlite3 *db = db_get_connection();
    if (!db) {
        fprintf(stderr, "Database connection not available.\n");
        return 1;
    }

    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, pid, command, status FROM tasks";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Failed to query tasks.\n");
        return 1;
    }
    
    printf("%-5s | %-10s | %-12s | %s\n", "ID", "PID", "STATUS", "COMMAND");
    printf("--------------------------------------------------------------\n");
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        int pid = sqlite3_column_int(stmt, 1);
        const unsigned char *cmd = sqlite3_column_text(stmt, 2);
        const unsigned char *status = sqlite3_column_text(stmt, 3);
        
        char current_status[32];
        snprintf(current_status, sizeof(current_status), "%s", status);
        
        /* Check if a running task is actually still alive */
        if (strcmp(current_status, "RUNNING") == 0) {
            if (kill(pid, 0) != 0) {
                /* Process doesn't exist or we have no permission; assume dead */
                snprintf(current_status, sizeof(current_status), "FINISHED");
                update_task_status(id, "FINISHED");
            }
        }
        
        printf("%-5d | %-10d | %-12s | %s\n", id, pid, current_status, cmd);
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

/* --- Main Dispatcher --- */

int cmd_task(int argc, char **argv) {
    if (argc < 2) {
#ifdef DEV_MODE
        fprintf(stderr, "Usage: mops task <exec|bg|queue|list|kill> [args...]\n");
#else
        fprintf(stderr, "Usage: mops task <exec|bg|list|kill> [args...]\n");
#endif
        return 1;
    }

    const char *subcmd = argv[1];
    
    if (strcmp(subcmd, "exec") == 0) {
        return cmd_task_exec(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "bg") == 0) {
        return cmd_task_bg(argc - 1, argv + 1);
#ifdef DEV_MODE
    } else if (strcmp(subcmd, "queue") == 0) {
        return cmd_task_queue(argc - 1, argv + 1);
#endif
    } else if (strcmp(subcmd, "list") == 0) {
        return cmd_task_list(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "kill") == 0) {
        return cmd_task_kill(argc - 1, argv + 1);
    } else {
        fprintf(stderr, "Unknown task subcommand: %s\n", subcmd);
#ifdef DEV_MODE
        fprintf(stderr, "Usage: mops task <exec|bg|queue|list|kill> [args...]\n");
#else
        fprintf(stderr, "Usage: mops task <exec|bg|list|kill> [args...]\n");
#endif
        return 1;
    }
}