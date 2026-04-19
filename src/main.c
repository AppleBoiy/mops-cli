#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations for module entry points */
extern int cmd_disk(int argc, char **argv);
extern int cmd_sys(int argc, char **argv);
extern int cmd_task(int argc, char **argv);
extern int db_init(void);

void print_usage(const char *prog_name) {
    printf("Usage: %s <command> [options]\n\n", prog_name);
    printf("Commands:\n");
    printf("  disk      Disk operations (status, usage, mounts)\n");
    printf("  sys       System & Hardware metrics (cpu, gpu, tpu)\n");
#ifdef DEV_MODE
    printf("  task      Task management (exec, bg, queue, list, kill)\n");
#else
    printf("  task      Task management (exec, bg, list, kill)\n");
#endif
    printf("\nRun '%s <command> --help' for more information on a command.\n", prog_name);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* Initialize the SQLite database for state tracking */
    if (db_init() != 0) {
        fprintf(stderr, "Error: Failed to initialize SQLite database.\n");
        return EXIT_FAILURE;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "disk") == 0) {
        return cmd_disk(argc - 1, argv + 1); /* Pass remaining args to subcommand */
    } else if (strcmp(cmd, "sys") == 0) {
        return cmd_sys(argc - 1, argv + 1);
    } else if (strcmp(cmd, "task") == 0) {
        return cmd_task(argc - 1, argv + 1);
    } else if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    } else {
        fprintf(stderr, "Unknown command: %s\n\n", cmd);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}