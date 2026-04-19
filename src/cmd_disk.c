#include "mops.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/statvfs.h>
#include <errno.h>

static int do_disk_status(void) {
    FILE *fp = fopen("/proc/diskstats", "r");
    if (!fp) {
        perror("Error opening /proc/diskstats");
        return 1;
    }

    char line[1024];
    printf("%-15s %-15s %-15s\n", "DEVICE", "READS", "WRITES");
    printf("-----------------------------------------------\n");
    
    while (fgets(line, sizeof(line), fp)) {
        int major, minor;
        char dev[256];
        unsigned long reads, merges_read, sectors_read, ms_read;
        unsigned long writes, merges_write, sectors_write, ms_write;

        /* /proc/diskstats usually has 14+ fields. We only care about the first few for a summary */
        if (sscanf(line, "%d %d %255s %lu %lu %lu %lu %lu %lu %lu %lu",
                   &major, &minor, dev,
                   &reads, &merges_read, &sectors_read, &ms_read,
                   &writes, &merges_write, &sectors_write, &ms_write) >= 11) {
            
            /* Filter out loopback devices and ramdisks for a cleaner output */
            if (strncmp(dev, "loop", 4) == 0 || strncmp(dev, "ram", 3) == 0) {
                continue;
            }
            
            printf("%-15s %-15lu %-15lu\n", dev, reads, writes);
        }
    }
    
    fclose(fp);
    return 0;
}

static int do_disk_usage(void) {
    struct statvfs stat;
    if (statvfs("/", &stat) != 0) {
        perror("Error executing statvfs on '/'");
        return 1;
    }

    double total = (double)stat.f_blocks * stat.f_frsize / (1024.0 * 1024.0 * 1024.0);
    double free = (double)stat.f_bfree * stat.f_frsize / (1024.0 * 1024.0 * 1024.0);
    double used = total - free;

    printf("Disk Usage (Root Filesystem '/'):\n");
    printf("  Total: %.2f GB\n", total);
    printf("  Used:  %.2f GB\n", used);
    printf("  Free:  %.2f GB\n", free);
    
    return 0;
}

static int do_disk_mounts(void) {
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) {
        perror("Error opening /proc/mounts");
        return 1;
    }

    char line[1024];
    printf("%-20s %-30s %-10s %s\n", "DEVICE", "MOUNTPOINT", "FSTYPE", "OPTIONS");
    printf("--------------------------------------------------------------------------------\n");
    
    while (fgets(line, sizeof(line), fp)) {
        char dev[256], mnt[256], type[256], opts[256];
        if (sscanf(line, "%255s %255s %255s %255s", dev, mnt, type, opts) == 4) {
            printf("%-20s %-30s %-10s %s\n", dev, mnt, type, opts);
        }
    }
    
    fclose(fp);
    return 0;
}

int cmd_disk(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops disk <status|usage|mounts>\n");
        return 1;
    }

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "status") == 0) {
        return do_disk_status();
    } else if (strcmp(subcmd, "usage") == 0) {
        return do_disk_usage();
    } else if (strcmp(subcmd, "mounts") == 0) {
        return do_disk_mounts();
    } else {
        fprintf(stderr, "Unknown disk command: %s\n", subcmd);
        fprintf(stderr, "Usage: mops disk <status|usage|mounts>\n");
        return 1;
    }
}