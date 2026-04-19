#include "mops.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/statvfs.h>
#include <errno.h>

static int do_disk_status(int human, int long_fmt) {
    FILE *fp = fopen("/proc/diskstats", "r");
    if (!fp) {
        perror("Error opening /proc/diskstats");
        return 1;
    }

    char line[1024];
    if (long_fmt) {
        if (human) {
            printf("%-15s %-15s %-15s %-15s %-15s\n", "DEVICE", "READS", "WRITES", "READ (MB)", "WRITTEN (MB)");
        } else {
            printf("%-15s %-15s %-15s %-15s %-15s\n", "DEVICE", "READS", "WRITES", "SECTORS_READ", "SECTORS_WRITTEN");
        }
        printf("-----------------------------------------------------------------------------\n");
    } else {
        printf("%-15s %-15s %-15s\n", "DEVICE", "READS", "WRITES");
        printf("-----------------------------------------------\n");
    }
    
    while (fgets(line, sizeof(line), fp)) {
        int major, minor;
        char dev[256];
        unsigned long reads, merges_read, sectors_read, ms_read;
        unsigned long writes, merges_write, sectors_write, ms_write;

        /*
         * /proc/diskstats usually has 14+ fields.
         * We only care about the first few for a summary.
         */
        if (sscanf(line, "%d %d %255s %lu %lu %lu %lu %lu %lu %lu %lu",
                   &major, &minor, dev,
                   &reads, &merges_read, &sectors_read, &ms_read,
                   &writes, &merges_write, &sectors_write, &ms_write) >= 11) {
            
            /*
             * Filter out loopback devices and ramdisks
             * for a cleaner output.
             */
            if (strncmp(dev, "loop", 4) == 0 || strncmp(dev, "ram", 3) == 0) {
                continue;
            }
            
            if (long_fmt) {
                if (human) {
                    printf("%-15s %-15lu %-15lu %-15.2f %-15.2f\n", dev, reads, writes,
                           (double)sectors_read * 512.0 / 1048576.0,
                           (double)sectors_write * 512.0 / 1048576.0);
                } else {
                    printf("%-15s %-15lu %-15lu %-15lu %-15lu\n", dev, reads, writes, sectors_read, sectors_write);
                }
            } else {
                printf("%-15s %-15lu %-15lu\n", dev, reads, writes);
            }
        }
    }
    
    fclose(fp);
    return 0;
}

static int do_disk_usage(int human, int long_fmt) {
    struct statvfs stat;
    if (statvfs("/", &stat) != 0) {
        perror("Error executing statvfs on '/'");
        return 1;
    }

    printf("Disk Usage (Root Filesystem '/'):\n");

    if (long_fmt) {
        printf("  Block size:    %lu bytes\n", (unsigned long)stat.f_bsize);
        printf("  Fragment size: %lu bytes\n", (unsigned long)stat.f_frsize);
        printf("  Blocks total:  %lu\n", (unsigned long)stat.f_blocks);
        printf("  Blocks free:   %lu\n", (unsigned long)stat.f_bfree);
        printf("  Inodes total:  %lu\n", (unsigned long)stat.f_files);
        printf("  Inodes free:   %lu\n", (unsigned long)stat.f_ffree);
    }

    if (human) {
        double total = (double)stat.f_blocks * stat.f_frsize / (1024.0 * 1024.0 * 1024.0);
        double free = (double)stat.f_bfree * stat.f_frsize / (1024.0 * 1024.0 * 1024.0);
        double used = total - free;

        printf("  Total: %.2f GB\n", total);
        printf("  Used:  %.2f GB\n", used);
        printf("  Free:  %.2f GB\n", free);
    } else {
        unsigned long long total = (unsigned long long)stat.f_blocks * stat.f_frsize;
        unsigned long long free = (unsigned long long)stat.f_bfree * stat.f_frsize;
        unsigned long long used = total - free;

        printf("  Total: %llu bytes\n", total);
        printf("  Used:  %llu bytes\n", used);
        printf("  Free:  %llu bytes\n", free);
    }
    
    return 0;
}

static int do_disk_mounts(int human, int long_fmt) {
    (void)human;
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) {
        perror("Error opening /proc/mounts");
        return 1;
    }

    char line[1024];
    if (long_fmt) {
        printf("%-20s %-30s %-10s %s\n", "DEVICE", "MOUNTPOINT", "FSTYPE", "OPTIONS");
        printf("--------------------------------------------------------------------------------\n");
    } else {
        printf("%-20s %-30s %-10s\n", "DEVICE", "MOUNTPOINT", "FSTYPE");
        printf("----------------------------------------------------------------\n");
    }
    
    while (fgets(line, sizeof(line), fp)) {
        char dev[256], mnt[256], type[256], opts[256];
        if (sscanf(line, "%255s %255s %255s %255s", dev, mnt, type, opts) == 4) {
            if (long_fmt) {
                printf("%-20s %-30s %-10s %s\n", dev, mnt, type, opts);
            } else {
                printf("%-20s %-30s %-10s\n", dev, mnt, type);
            }
        }
    }
    
    fclose(fp);
    return 0;
}

int cmd_disk(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops disk <status|usage|mounts> [-h] [-l]\n");
        return 1;
    }

    const char *subcmd = NULL;
    int human = 0;
    int long_fmt = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            human = 1;
        } else if (strcmp(argv[i], "-l") == 0) {
            long_fmt = 1;
        } else if (argv[i][0] != '-' && subcmd == NULL) {
            subcmd = argv[i];
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    if (!subcmd) {
        fprintf(stderr, "Usage: mops disk <status|usage|mounts> [-h] [-l]\n");
        return 1;
    }

    if (strcmp(subcmd, "status") == 0) {
        return do_disk_status(human, long_fmt);
    } else if (strcmp(subcmd, "usage") == 0) {
        return do_disk_usage(human, long_fmt);
    } else if (strcmp(subcmd, "mounts") == 0) {
        return do_disk_mounts(human, long_fmt);
    } else {
        fprintf(stderr, "Unknown disk command: %s\n", subcmd);
        fprintf(stderr, "Usage: mops disk <status|usage|mounts> [-h] [-l]\n");
        return 1;
    }
}