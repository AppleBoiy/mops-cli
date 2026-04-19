#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Forward declarations for module entry points
 */
extern int cmd_disk(int argc, char **argv);
extern int cmd_sys(int argc, char **argv);
extern int cmd_net(int argc, char **argv);
extern int cmd_gcp(int argc, char **argv);
extern int cmd_task(int argc, char **argv);
extern int cmd_worker(int argc, char **argv);
extern int db_init(void);

void print_usage(const char *prog_name) {
    printf("mops - Multipurpose Operations CLI & Batch System\n\n");
    printf("Usage: %s <command> [options]\n\n", prog_name);
    printf("Commands:\n");
    printf("  disk      Disk operations (status, usage, mounts)\n");
    printf("  sys       System & Hardware metrics (cpu, gpu, tpu, oom, cgroup)\n");
    printf("  net       Network operations (port)\n");
    printf("  gcp       Google Cloud Platform operations (whoami, spot-watch, tunnel, run-with-secrets)\n");
    printf("  task      Task management (submit, list, kill, logs, clean)\n");
    printf("  worker    Manage the background task scheduler daemon (start, stop, status)\n\n");
    printf("Aliases:\n");
    printf("  mem       Alias for 'mops sys mem'\n");
    printf("  qsub      Alias for 'mops task submit'\n");
    printf("  qstat     Alias for 'mops task list'\n");
    printf("  qdel      Alias for 'mops task kill'\n");
    printf("\nRun '%s <command> --help' for more information on a command.\n", prog_name);
}

void print_author(void) {
    printf("Personal Details\n");
    printf("----------------\n");
    printf("Name:   Chaipat J.\n");
    printf("GitHub: AppleBoiy\n");
    printf("Email:  contact@chaipat.cc\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /*
     * Initialize the SQLite database for state tracking.
     * Skip this for worker status checks to avoid creating the DB unnecessarily.
     */
    int is_worker_status = (argc > 2 && strcmp(argv[1], "worker") == 0 && strcmp(argv[2], "status") == 0);
    if (!is_worker_status) {
        if (db_init() != 0) {
            fprintf(stderr, "Error: Failed to initialize SQLite database.\n");
            return EXIT_FAILURE;
        }
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "disk") == 0) {
        return cmd_disk(argc - 1, argv + 1);
    } else if (strcmp(cmd, "sys") == 0) {
        return cmd_sys(argc - 1, argv + 1);
    } else if (strcmp(cmd, "net") == 0) {
        return cmd_net(argc - 1, argv + 1);
    } else if (strcmp(cmd, "gcp") == 0) {
        return cmd_gcp(argc - 1, argv + 1);
    } else if (strcmp(cmd, "task") == 0) {
        return cmd_task(argc - 1, argv + 1);
    } else if (strcmp(cmd, "worker") == 0) {
        return cmd_worker(argc - 1, argv + 1);
    } else if (strcmp(cmd, "mem") == 0) {
        char *new_argv[] = { "sys", "mem" };
        return cmd_sys(2, new_argv);
    }
    /* PBS-style aliases */
    else if (strcmp(cmd, "qsub") == 0 || strcmp(cmd, "qdel") == 0) {
        // Reconstruct argv to pass to the task command dispatcher.
        // e.g., "mops qsub <args>" becomes "mops task submit <args>"
        int new_argc = argc;
        char **new_argv = malloc(sizeof(char*) * new_argc);
        if (!new_argv) return EXIT_FAILURE;
        
        new_argv[0] = "task";
        new_argv[1] = (strcmp(cmd, "qsub") == 0) ? "submit" : "kill";
        for (int i = 2; i < argc; i++) {
            new_argv[i] = argv[i];
        }
        
        int ret = cmd_task(new_argc, new_argv);
        free(new_argv);
        return ret;
    } else if (strcmp(cmd, "qstat") == 0) {
        char *new_argv[] = { "task", "list" };
        return cmd_task(2, new_argv);
    }
    else if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    } else if (strcmp(cmd, "--author") == 0) {
        print_author();
        return EXIT_SUCCESS;
    } else {
        fprintf(stderr, "Unknown command: %s\n\n", cmd);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}