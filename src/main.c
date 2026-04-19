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
#ifdef DEV_MODE
extern int cmd_task(int argc, char **argv);
#endif
extern int db_init(void);

void print_usage(const char *prog_name) {
    printf("mops - Multipurpose Operations CLI\n\n");
    printf("Usage: %s <command> [options]\n\n", prog_name);
    printf("Commands:\n");
    printf("  disk      Disk operations (status, usage, mounts)\n");
    printf("  sys       System & Hardware metrics (cpu, gpu, tpu, oom, cgroup)\n");
    printf("  net       Network operations (port)\n");
    printf("  gcp       Google Cloud Platform operations (whoami, spot-watch, tunnel, run-with-secrets)\n");
#ifdef DEV_MODE
    printf("  task      Task management (exec, bg, queue, list, logs, clean, kill)\n");
#endif
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
     * Initialize the SQLite database for state tracking
     */
    if (db_init() != 0) {
        fprintf(stderr, "Error: Failed to initialize SQLite database.\n");
        return EXIT_FAILURE;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "disk") == 0) {
        /* Pass remaining args to subcommand */
        return cmd_disk(argc - 1, argv + 1);
    } else if (strcmp(cmd, "sys") == 0) {
        return cmd_sys(argc - 1, argv + 1);
    } else if (strcmp(cmd, "net") == 0) {
        return cmd_net(argc - 1, argv + 1);
    } else if (strcmp(cmd, "gcp") == 0) {
        return cmd_gcp(argc - 1, argv + 1);
#ifdef DEV_MODE
    } else if (strcmp(cmd, "task") == 0) {
        return cmd_task(argc - 1, argv + 1);
#endif
    } else if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
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