#include "mops.h"
#include "argtable3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <errno.h>

/*
 * cmd_doctor: Run diagnostic checks on the environment.
 */
int cmd_doctor(int argc, char **argv) {
    struct arg_lit *help = arg_lit0(NULL, "help", "print this help and exit");
    struct arg_end *end = arg_end(20);
    void *argtable[] = {help, end};

    int nerrors = arg_parse(argc, argv, argtable);

    if (help->count > 0) {
        printf("Usage: mops doctor\n");
        printf("Run diagnostic checks to ensure mops is correctly configured.\n\n");
        arg_print_glossary(stdout, argtable, "  %-25s %s\n");
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 0;
    }

    if (nerrors > 0) {
        arg_print_errors(stdout, end, "mops doctor");
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 1;
    }

    printf("Mops Diagnostics\n");
    printf("================\n\n");

    int issues = 0;

    /* 1. Check Database */
    printf("[ ] Checking database (mops.db)... ");
    if (access("mops.db", F_OK) == 0) {
        if (access("mops.db", W_OK) == 0) {
            printf("OK (Writable)\n");
        } else {
            printf("FAIL (Found but not writable)\n");
            issues++;
        }
    } else {
        printf("WARN (Not found, will be created on first use)\n");
    }

    /* 2. Check Worker */
    printf("[ ] Checking worker status... ");
    FILE *pf = fopen("/tmp/mops_worker.pid", "r");
    if (pf) {
        int pid;
        if (fscanf(pf, "%d", &pid) == 1) {
            if (kill(pid, 0) == 0) {
                printf("OK (Running, PID %d)\n", pid);
            } else {
                printf("FAIL (PID file exists but process %d not found)\n", pid);
                issues++;
            }
        } else {
            printf("FAIL (PID file is empty or corrupt)\n");
            issues++;
        }
        fclose(pf);
    } else {
        printf("INFO (Not running)\n");
    }

    /* 3. Check Dependencies */
    printf("[ ] Checking dependencies:\n");
    
    printf("    - sqlite3: ");
    if (system("sqlite3 --version >/dev/null 2>&1") == 0) {
        printf("Found\n");
    } else {
        printf("NOT FOUND (Optional CLI)\n");
    }

    printf("    - nvidia-smi: ");
    if (system("nvidia-smi --version >/dev/null 2>&1") == 0) {
        printf("Found (GPU metrics enabled)\n");
    } else {
        printf("NOT FOUND (GPU metrics will be unavailable)\n");
    }

    printf("\n");
    if (issues == 0) {
        printf("All systems operational!\n");
    } else {
        printf("Found %d issue(s). Please check the failures above.\n", issues);
    }

    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return (issues == 0) ? 0 : 1;
}

/*
 * cmd_completion: Generate shell completion scripts.
 */
int cmd_completion(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops completion <shell>\n");
        fprintf(stderr, "Supported shells: bash\n");
        return 1;
    }

    const char *shell = argv[1];

    if (strcmp(shell, "bash") == 0) {
        printf("# mops bash completion\n"
               "_mops_completions()\n"
               "{\n"
               "    local cur prev opts\n"
               "    COMPREPLY=()\n"
               "    cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
               "    prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n"
               "    opts=\"disk sys dashboard net gcp task worker doctor completion version help\"\n"
               "\n"
               "    case \"${prev}\" in\n"
               "        disk)\n"
               "            COMPREPLY=( $(compgen -W \"status usage mounts\" -- ${cur}) )\n"
               "            return 0\n"
               "            ;;\n"
               "        sys)\n"
               "            COMPREPLY=( $(compgen -W \"cpu mem gpu tpu oom cgroup\" -- ${cur}) )\n"
               "            return 0\n"
               "            ;;\n"
               "        task)\n"
               "            COMPREPLY=( $(compgen -W \"submit list kill logs clean rm purge\" -- ${cur}) )\n"
               "            return 0\n"
               "            ;;\n"
               "        worker)\n"
               "            COMPREPLY=( $(compgen -W \"start stop status\" -- ${cur}) )\n"
               "            return 0\n"
               "            ;;\n"
               "        completion)\n"
               "            COMPREPLY=( $(compgen -W \"bash\" -- ${cur}) )\n"
               "            return 0\n"
               "            ;;\n"
               "    esac\n"
               "\n"
               "    COMPREPLY=( $(compgen -W \"${opts}\" -- ${cur}) )\n"
               "}\n"
               "complete -F _mops_completions mops\n");
        return 0;
    } else {
        fprintf(stderr, "Unsupported shell: %s\n", shell);
        return 1;
    }
}
