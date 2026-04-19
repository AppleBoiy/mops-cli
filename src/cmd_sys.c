#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include "mops.h"
#include "argtable3.h"

/*
 * System Metrics - CPU
 */

struct cpu_stat {
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
};

static int read_cpu_stat(struct cpu_stat *st) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) {
        return -1;
    }
    
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), fp)) {
        /*
         * /proc/stat CPU line format: cpu user nice system idle
         * iowait irq softirq steal guest guest_nice
         */
        sscanf(buffer, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &st->user, &st->nice, &st->system, &st->idle,
               &st->iowait, &st->irq, &st->softirq, &st->steal);
    }
    fclose(fp);
    return 0;
}

static unsigned long long get_idle_time(struct cpu_stat *st) {
    return st->idle + st->iowait;
}

static unsigned long long get_total_time(struct cpu_stat *st) {
    return st->user + st->nice + st->system + st->idle +
           st->iowait + st->irq + st->softirq + st->steal;
}

int cmd_sys_cpu(int argc, char **argv) {
    struct arg_lit *human = arg_lit0("h", NULL, "human readable output");
    struct arg_lit *longf = arg_lit0("l", NULL, "long format");
    struct arg_lit *jsonf = arg_lit0(NULL, "json", "output in JSON format");
    struct arg_lit *help  = arg_lit0(NULL, "help", "print this help and exit");
    struct arg_end *end   = arg_end(20);
    void *argtable[] = {human, longf, jsonf, help, end};

    int nerrors = arg_parse(argc, argv, argtable);

    if (help->count > 0) {
        printf("Usage: mops sys cpu");
        arg_print_syntax(stdout, argtable, "\n");
        printf("Show CPU utilization statistics.\n\n");
        arg_print_glossary(stdout, argtable, "  %-25s %s\n");
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 0;
    }

    if (nerrors > 0) {
        arg_print_errors(stdout, end, "mops sys cpu");
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 1;
    }

    int human_readable = (human->count > 0);
    int long_format = (longf->count > 0);
    int json = (jsonf->count > 0);

    struct cpu_stat stat1, stat2;
    
    if (read_cpu_stat(&stat1) != 0) {
        fprintf(stderr, "Error: Could not read /proc/stat. Are you on Linux?\n");
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 1;
    }
    
    /* Sample over 500 milliseconds for current utilization */
    usleep(500000); 
    
    if (read_cpu_stat(&stat2) != 0) {
        fprintf(stderr, "Error: Could not read /proc/stat.\n");
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 1;
    }

    unsigned long long idle1 = get_idle_time(&stat1);
    unsigned long long total1 = get_total_time(&stat1);
    unsigned long long idle2 = get_idle_time(&stat2);
    unsigned long long total2 = get_total_time(&stat2);

    unsigned long long total_diff = total2 - total1;
    unsigned long long idle_diff = idle2 - idle1;

    double usage = 0.0;
    if (total_diff > 0) {
        usage = (double)(total_diff - idle_diff) / total_diff * 100.0;
    }

    if (json) {
        printf("{\"cpu_utilization\":%.2f", usage);
        if (long_format) {
            printf(",\"user\":%llu,\"system\":%llu,\"idle\":%llu,\"iowait\":%llu", stat2.user, stat2.system, stat2.idle, stat2.iowait);
        }
        printf("}\n");
    } else {
        if (long_format) {
            if (human_readable) {
                printf("CPU User: %llu\n", stat2.user);
                printf("CPU System: %llu\n", stat2.system);
                printf("CPU Idle: %llu\n", stat2.idle);
                printf("CPU IOWait: %llu\n", stat2.iowait);
            } else {
                printf("user=%llu,system=%llu,idle=%llu,iowait=%llu\n", stat2.user, stat2.system, stat2.idle, stat2.iowait);
            }
        }

        if (human_readable) {
            printf("CPU Utilization: %.2f%%\n", usage);
        } else {
            printf("%.2f\n", usage);
        }
    }

    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return 0;
}

/*
 * System Metrics - Memory
 */

int cmd_sys_mem(int argc, char **argv) {
    struct arg_lit *human = arg_lit0("h", NULL, "human readable output");
    struct arg_lit *jsonf = arg_lit0(NULL, "json", "output in JSON format");
    struct arg_lit *help  = arg_lit0(NULL, "help", "print this help and exit");
    struct arg_end *end   = arg_end(20);
    void *argtable[] = {human, jsonf, help, end};

    int nerrors = arg_parse(argc, argv, argtable);

    if (help->count > 0) {
        printf("Usage: mops sys mem");
        arg_print_syntax(stdout, argtable, "\n");
        printf("Show system memory and swap utilization.\n\n");
        arg_print_glossary(stdout, argtable, "  %-25s %s\n");
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 0;
    }

    if (nerrors > 0) {
        arg_print_errors(stdout, end, "mops sys mem");
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 1;
    }

    int human_readable = (human->count > 0);
    int json = (jsonf->count > 0);
    
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        if (json) printf("{\"error\":\"Could not open /proc/meminfo\"}\n");
        else fprintf(stderr, "Error: Could not open /proc/meminfo. Are you on Linux?\n");
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 1;
    }

    unsigned long long mem_total = 0, mem_free = 0, mem_available = 0;
    unsigned long long swap_total = 0, swap_free = 0;
    char line[256];
    
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0) sscanf(line, "MemTotal: %llu kB", &mem_total);
        else if (strncmp(line, "MemFree:", 8) == 0) sscanf(line, "MemFree: %llu kB", &mem_free);
        else if (strncmp(line, "MemAvailable:", 13) == 0) sscanf(line, "MemAvailable: %llu kB", &mem_available);
        else if (strncmp(line, "SwapTotal:", 10) == 0) sscanf(line, "SwapTotal: %llu kB", &swap_total);
        else if (strncmp(line, "SwapFree:", 9) == 0) sscanf(line, "SwapFree: %llu kB", &swap_free);
    }
    fclose(fp);

    unsigned long long mem_used = 0;
    if (mem_available > 0) {
        mem_used = mem_total - mem_available; // Best estimate of actual used memory
    } else {
        mem_used = mem_total - mem_free; // Fallback for older kernels
    }
    unsigned long long swap_used = swap_total - swap_free;

    if (json) {
        printf("{\"mem_total_kb\":%llu,\"mem_free_kb\":%llu,\"mem_available_kb\":%llu,\"mem_used_kb\":%llu,\"swap_total_kb\":%llu,\"swap_free_kb\":%llu,\"swap_used_kb\":%llu}\n",
               mem_total, mem_free, mem_available, mem_used, swap_total, swap_free, swap_used);
    } else if (human_readable) {
        printf("System Memory:\n");
        printf("----------------------------------------------------\n");
        printf("Total Memory:      %8.2f MB\n", (double)mem_total / 1024.0);
        printf("Used Memory:       %8.2f MB\n", (double)mem_used / 1024.0);
        printf("Free Memory:       %8.2f MB\n", (double)mem_free / 1024.0);
        if (mem_available > 0) {
            printf("Available Memory:  %8.2f MB\n", (double)mem_available / 1024.0);
        }
        if (swap_total > 0) {
            printf("----------------------------------------------------\n");
            printf("Total Swap:        %8.2f MB\n", (double)swap_total / 1024.0);
            printf("Used Swap:         %8.2f MB\n", (double)swap_used / 1024.0);
            printf("Free Swap:         %8.2f MB\n", (double)swap_free / 1024.0);
        }
    } else {
        printf("mem_total_kb=%llu,mem_used_kb=%llu,mem_available_kb=%llu,swap_total_kb=%llu,swap_used_kb=%llu\n",
               mem_total, mem_used, mem_available, swap_total, swap_used);
    }

    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return 0;
}

/*
 * System Metrics - GPU
 */

int cmd_sys_gpu(int argc, char **argv) {
    int human_readable = 0;
    int long_format = 0;
    int pids = 0;
    int json = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) human_readable = 1;
        if (strcmp(argv[i], "-l") == 0) long_format = 1;
        if (strcmp(argv[i], "--pids") == 0) pids = 1;
        if (strcmp(argv[i], "--json") == 0) json = 1;
    }

    if (pids) {
        const char *cmd = "nvidia-smi --query-compute-apps=pid,used_memory,process_name --format=csv,noheader,nounits 2>/dev/null";
        FILE *fp = popen(cmd, "r");
        if (!fp) {
            fprintf(stderr, "Error: Failed to execute nvidia-smi.\n");
            return 1;
        }

        char line[256];
        if (json) {
            printf("[");
        } else if (human_readable) {
            printf("GPU Compute Processes:\n");
            printf("%-10s | %-20s | %s\n", "PID", "VRAM Used", "Process Name");
            printf("------------------------------------------------------------\n");
        }

        int first = 1;
        while (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\n")] = 0;
            char *pid_str = strtok(line, ",");
            char *mem = strtok(NULL, ",");
            char *name = strtok(NULL, ",");
            
            if (pid_str && mem && name) {
                if (json) {
                    if (!first) printf(",");
                    printf("{\"pid\":%s,\"vram_used_mib\":%s,\"process_name\":\"%s\"}", pid_str, mem, name);
                    first = 0;
                } else if (human_readable) {
                    printf("%-10s | %-20s | %s\n", pid_str, mem, name);
                } else {
                    printf("%s,%s,%s\n", pid_str, mem, name);
                }
            }
        }
        if (json) printf("]\n");
        pclose(fp);

        if (first && !json && human_readable) {
            printf("No active compute processes found on GPU.\n");
        }
        return 0;
    }
    
    /*
     * Execute nvidia-smi. We ask for CSV format to avoid
     * messy parsing.
     */
    const char *cmd = long_format ?
        "nvidia-smi --query-gpu=index,name,memory.used,memory.total,utilization.gpu,temperature.gpu,power.draw --format=csv,noheader,nounits 2>/dev/null" :
        "nvidia-smi --query-gpu=index,name,memory.used,memory.total,utilization.gpu --format=csv,noheader,nounits 2>/dev/null";
    FILE *fp = popen(cmd, "r");
    
    if (!fp) {
        fprintf(stderr, "Error: Failed to execute nvidia-smi.\n");
        return 1;
    }

    char line[256];
    int first = 1;

    if (json) printf("[");
    
    while (fgets(line, sizeof(line), fp)) {
        if (first) {
            if (!json && human_readable) {
                printf("NVIDIA GPUs Found:\n");
                if (long_format) {
                    printf("%-5s | %-20s | %-20s | %-10s | %-10s | %-10s\n", "IDX", "Name", "Memory (Used/Total)", "Util (%)", "Temp", "Power");
                    printf("------------------------------------------------------------------------------------------------\n");
                } else {
                    printf("%-5s | %-20s | %-20s | %-10s\n", "IDX", "Name", "Memory (Used/Total)", "Util (%)");
                    printf("----------------------------------------------------------------------\n");
                }
            }
        }
        
        line[strcspn(line, "\n")] = 0; /* Strip newline */
        
        /*
         * Simple manual split based on commas to format output nicely.
         */
        char *idx = strtok(line, ",");
        char *name = strtok(NULL, ",");
        char *mem_used = strtok(NULL, ",");
        char *mem_total = strtok(NULL, ",");
        char *util = strtok(NULL, ",");
        char *temp = long_format ? strtok(NULL, ",") : NULL;
        char *power = long_format ? strtok(NULL, ",") : NULL;

        if (idx && name && mem_used && mem_total && util) {
            if (json) {
                if (!first) printf(",");
                printf("{\"index\":%s,\"name\":\"%s\",\"memory_used_mib\":%s,\"memory_total_mib\":%s,\"utilization_gpu_pct\":%s",
                       idx, name, mem_used, mem_total, util);
                if (long_format && temp && power) {
                    printf(",\"temperature_c\":%s,\"power_draw_w\":%s", temp, power);
                }
                printf("}");
            } else if (human_readable) {
                char mem_str[64];
                snprintf(mem_str, sizeof(mem_str), "%s / %s", mem_used, mem_total);
                if (long_format && temp && power) {
                    printf("%-5s | %-20s | %-20s | %-10s | %-10s | %-10s\n", idx, name, mem_str, util, temp, power);
                } else {
                    printf("%-5s | %-20s | %-20s | %-10s\n", idx, name, mem_str, util);
                }
            } else {
                if (long_format && temp && power) {
                    printf("%s,%s,%s,%s,%s,%s,%s\n", idx, name, mem_used, mem_total, util, temp, power);
                } else {
                    printf("%s,%s,%s,%s,%s\n", idx, name, mem_used, mem_total, util);
                }
            }
            first = 0;
        } else {
            /* Fallback if formatting is unexpected */
            printf("%s\n", line);
        }
    }
    
    if (json) printf("]\n");
    int status = pclose(fp);

    if ((first && !json) || WEXITSTATUS(status) != 0) {
        if (human_readable) {
            printf("No NVIDIA GPU found, or nvidia-smi is not in PATH.\n");
        }
    }
    
    return 0;
}

/*
 * System Metrics - TPU
 */

int cmd_sys_tpu(int argc, char **argv) {
    int human_readable = 0;
    int long_format = 0;
    int json = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) human_readable = 1;
        if (strcmp(argv[i], "-l") == 0) long_format = 1;
        if (strcmp(argv[i], "--json") == 0) json = 1;
    }
    
    DIR *dir = opendir("/dev");
    if (!dir) {
        fprintf(stderr, "Error: Cannot open /dev to search for TPUs.\n");
        return 1;
    }
    
    struct dirent *ent;
    int tpu_count = 0;
    
    if (json) {
        printf("{\"devices\":[");
    } else if (human_readable) {
        printf("Scanning for TPU devices...\n");
    }
    
    int first = 1;
    while ((ent = readdir(dir)) != NULL) {
        /*
         * Standard Google Cloud TPU device names are typically
         * /dev/accelX
         */
        if (strncmp(ent->d_name, "accel", 5) == 0) {
            if (json) {
                if (!first) printf(",");
                printf("\"/dev/%s\"", ent->d_name);
                first = 0;
            } else if (human_readable) {
                printf("- Found TPU device: /dev/%s\n", ent->d_name);
            } else if (long_format) {
                printf("/dev/%s\n", ent->d_name);
            }
            tpu_count++;
        }
    }
    closedir(dir);

    if (json) {
        printf("],\"tpu_count\":%d}\n", tpu_count);
    } else if (human_readable) {
        if (tpu_count == 0) {
            printf("No TPU devices found (checked /dev/accel*).\n");
        } else {
            if (long_format) {
                printf("Total TPUs found: %d (Paths listed above)\n", tpu_count);
            } else {
                printf("Total TPUs found: %d\n", tpu_count);
            }
        }
    } else {
        if (!long_format) {
            printf("%d\n", tpu_count);
        }
    }
    
    return 0;
}

/*
 * System Metrics - OOM
 */

int cmd_sys_oom(int argc, char **argv) {
    int json = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) json = 1;
    }
    
    FILE *fp = popen("dmesg | grep -i 'killed process' 2>/dev/null", "r");
    if (!fp) {
        if (json) {
            printf("[]\n");
        } else {
            fprintf(stderr, "Error: Could not read dmesg. Make sure you have privileges.\n");
        }
        return 1;
    }
    
    char line[512];
    int first = 1;
    
    if (json) {
        printf("[");
    } else {
        printf("OOM Killed Processes:\n");
        printf("----------------------------------------------------\n");
    }
    
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0; // Strip newline
        if (json) {
            if (!first) printf(",");
            // Basic escaping for quotes
            char escaped_line[1024] = {0};
            int j = 0;
            for (int i = 0; line[i] != '\0' && j < 1022; i++) {
                if (line[i] == '"' || line[i] == '\\') {
                    escaped_line[j++] = '\\';
                }
                escaped_line[j++] = line[i];
            }
            printf("{\"log_entry\":\"%s\"}", escaped_line);
            first = 0;
        } else {
            printf("%s\n", line);
        }
    }
    
    pclose(fp);

    if (json) {
        printf("]\n");
    } else if (first) {
        printf("No OOM kills found in the current dmesg ring buffer.\n");
    }
    
    return 0;
}

/*
 * System Metrics - CGroup Usage
 */

int cmd_sys_cgroup(int argc, char **argv) {
    int json = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) json = 1;
    }
    
    FILE *fp;
    unsigned long long mem_usage = 0, mem_limit = 0;
    int found_mem = 0;
    
    /* Try cgroup v2 first */
    fp = fopen("/sys/fs/cgroup/memory.current", "r");
    if (fp) {
        if (fscanf(fp, "%llu", &mem_usage) == 1) found_mem = 1;
        fclose(fp);
        
        fp = fopen("/sys/fs/cgroup/memory.max", "r");
        if (fp) {
            char buf[64];
            if (fgets(buf, sizeof(buf), fp)) {
                if (strncmp(buf, "max", 3) != 0) {
                    mem_limit = strtoull(buf, NULL, 10);
                }
            }
            fclose(fp);
        }
    } else {
        /* Try cgroup v1 fallback */
        fp = fopen("/sys/fs/cgroup/memory/memory.usage_in_bytes", "r");
        if (fp) {
            if (fscanf(fp, "%llu", &mem_usage) == 1) found_mem = 1;
            fclose(fp);
            
            fp = fopen("/sys/fs/cgroup/memory/memory.limit_in_bytes", "r");
            if (fp) {
                if (fscanf(fp, "%llu", &mem_limit) != 1) mem_limit = 0;
                fclose(fp);
            }
        }
    }

    if (!found_mem) {
        if (json) {
            printf("{\"error\":\"Could not find cgroup memory metrics\"}\n");
        } else {
            fprintf(stderr, "Error: Could not find cgroup memory metrics.\n");
        }
        return 1;
    }

    if (json) {
        printf("{\"memory_usage_bytes\":%llu", mem_usage);
        if (mem_limit > 0 && mem_limit < 9000000000000000000ULL) {
            printf(",\"memory_limit_bytes\":%llu}\n", mem_limit);
        } else {
            printf(",\"memory_limit_bytes\":null}\n");
        }
    } else {
        printf("Container / Cgroup Resources:\n");
        printf("----------------------------------------------------\n");
        printf("Memory Usage: %.2f MB\n", (double)mem_usage / (1024 * 1024));
        
        /* Display limits carefully to ignore max defaults (e.g. 9223372036854771712) */
        if (mem_limit > 0 && mem_limit < 9000000000000000000ULL) {
            printf("Memory Limit: %.2f MB\n", (double)mem_limit / (1024 * 1024));
        } else {
            printf("Memory Limit: Unlimited\n");
        }
    }
    
    return 0;
}


/*
 * Main Dispatcher
 */

int cmd_sys(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops sys <command> [options]\n");
        fprintf(stderr, "Run 'mops sys --help' for more information.\n");
        return 1;
    }

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "--help") == 0 || strcmp(subcmd, "-h") == 0) {
        printf("mops sys - System and Hardware Monitoring\n\n");
        printf("Usage: mops sys <command> [options]\n\n");
        printf("Commands:\n");
        printf("  cpu       Show CPU utilization\n");
        printf("  mem       Show Memory utilization\n");
        printf("  gpu       Show GPU utilization\n");
        printf("  tpu       Show TPU availability\n");
        printf("  oom       List Out-Of-Memory killed processes from dmesg\n");
        printf("  cgroup    Show Cgroup / Docker container resource limits and usage\n\n");
        printf("Generic Options:\n");
        printf("  --json    Output in JSON format\n");
        printf("  -h        Human-readable output\n\n");
        printf("Run 'mops sys <command> --help' for specific command options.\n");
        return 0;
    }

    if (strcmp(subcmd, "cpu") == 0) {
        return cmd_sys_cpu(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "mem") == 0) {
        return cmd_sys_mem(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "gpu") == 0) {
        return cmd_sys_gpu(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "tpu") == 0) {
        return cmd_sys_tpu(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "oom") == 0) {
        return cmd_sys_oom(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "cgroup") == 0) {
        return cmd_sys_cgroup(argc - 1, argv + 1);
    } else {
        fprintf(stderr, "Unknown sys command: %s\n", subcmd);
        fprintf(stderr, "Usage: mops sys <command> [options]\n");
        fprintf(stderr, "Run 'mops sys --help' for more information.\n");
        return 1;
    }
}