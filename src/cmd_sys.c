#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include "mops.h"

/* --- System Metrics - CPU --- */

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
        /* /proc/stat CPU line format: cpu user nice system idle iowait irq softirq steal guest guest_nice */
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
    (void)argc;
    (void)argv;
    
    struct cpu_stat stat1, stat2;
    
    if (read_cpu_stat(&stat1) != 0) {
        fprintf(stderr, "Error: Could not read /proc/stat. Are you on Linux?\n");
        return 1;
    }
    
    /* Sample over 500 milliseconds for current utilization */
    usleep(500000); 
    
    if (read_cpu_stat(&stat2) != 0) {
        fprintf(stderr, "Error: Could not read /proc/stat.\n");
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

    printf("CPU Utilization: %.2f%%\n", usage);
    return 0;
}

/* --- System Metrics - GPU --- */

int cmd_sys_gpu(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    /* Execute nvidia-smi. We ask for CSV format to avoid messy parsing. */
    const char *cmd = "nvidia-smi --query-gpu=index,name,memory.used,memory.total,utilization.gpu --format=csv,noheader 2>/dev/null";
    FILE *fp = popen(cmd, "r");
    
    if (!fp) {
        fprintf(stderr, "Error: Failed to execute nvidia-smi.\n");
        return 1;
    }

    char line[256];
    int gpu_found = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (!gpu_found) {
            printf("NVIDIA GPUs Found:\n");
            printf("%-5s | %-20s | %-20s | %-10s\n", "IDX", "Name", "Memory (Used/Total)", "Util (%)");
            printf("----------------------------------------------------------------------\n");
            gpu_found = 1;
        }
        
        line[strcspn(line, "\n")] = 0; // Strip newline
        
        // Simple manual split based on commas to format output nicely
        char *idx = strtok(line, ",");
        char *name = strtok(NULL, ",");
        char *mem_used = strtok(NULL, ",");
        char *mem_total = strtok(NULL, ",");
        char *util = strtok(NULL, ",");

        if (idx && name && mem_used && mem_total && util) {
            char mem_str[64];
            snprintf(mem_str, sizeof(mem_str), "%s / %s", mem_used, mem_total);
            printf("%-5s | %-20s | %-20s | %-10s\n", idx, name, mem_str, util);
        } else {
            // Fallback if formatting is unexpected
            printf("%s\n", line);
        }
    }
    
    int status = pclose(fp);

    if (!gpu_found || status != 0) {
        printf("No NVIDIA GPU found, or nvidia-smi is not in PATH.\n");
    }
    
    return 0;
}

/* --- System Metrics - TPU --- */

int cmd_sys_tpu(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    DIR *dir = opendir("/dev");
    if (!dir) {
        fprintf(stderr, "Error: Cannot open /dev to search for TPUs.\n");
        return 1;
    }
    
    struct dirent *ent;
    int tpu_count = 0;
    
    printf("Scanning for TPU devices...\n");
    
    while ((ent = readdir(dir)) != NULL) {
        /* Standard Google Cloud TPU device names are typically /dev/accelX */
        if (strncmp(ent->d_name, "accel", 5) == 0) {
            printf("- Found TPU device: /dev/%s\n", ent->d_name);
            tpu_count++;
        }
    }
    closedir(dir);
    
    if (tpu_count == 0) {
        printf("No TPU devices found (checked /dev/accel*).\n");
    } else {
        printf("Total TPUs found: %d\n", tpu_count);
    }
    
    return 0;
}

/* --- Main Dispatcher --- */

int cmd_sys(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops sys <cpu|gpu|tpu>\n");
        return 1;
    }

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "cpu") == 0) {
        return cmd_sys_cpu(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "gpu") == 0) {
        return cmd_sys_gpu(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "tpu") == 0) {
        return cmd_sys_tpu(argc - 1, argv + 1);
    } else {
        fprintf(stderr, "Unknown sys command: %s\n", subcmd);
        fprintf(stderr, "Available commands: cpu, gpu, tpu\n");
        return 1;
    }
}