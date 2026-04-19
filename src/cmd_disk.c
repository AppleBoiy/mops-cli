#include "mops.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/statvfs.h>
#include <errno.h>

/*
 * Helper to format byte counts into human-readable strings (K, M, G).
 */
static void format_bytes_human(unsigned long long bytes, char *buf, size_t len) {
    const char *suffixes[] = {"B", "K", "M", "G", "T", "P"};
    int i = 0;
    double d_bytes = bytes;

    while (d_bytes >= 1024.0 && i < 5) {
        d_bytes /= 1024.0;
        i++;
    }

    if (i == 0) {
        snprintf(buf, len, "%llu%s", bytes, suffixes[i]);
    } else {
        snprintf(buf, len, "%.1f%s", d_bytes, suffixes[i]);
    }
}

static int do_disk_status(int human, int long_fmt, int json) {
    FILE *fp = fopen("/proc/diskstats", "r");
    if (!fp) {
        perror("Error opening /proc/diskstats");
        return 1;
    }

    char line[1024];
    int first = 1;

    if (json) {
        printf("[");
    } else if (long_fmt) {
        if (human) {
            printf("%-15s %-15s %-15s %-15s %-15s\n", "DEVICE", "READS", "WRITES", "READ (MB)", "WRITTEN (MB)");
        } else {
            printf("%-15s %-15s %-15s %-15s %-15s\n", "DEVICE", "READS", "WRITES", "SECTORS_READ", "SECTORS_WRITTEN");
        }
        printf("-----------------------------------------------------------------------------\n");
    } else {
        if (human) {
            printf("%-15s %-15s %-15s\n", "DEVICE", "READ (MB)", "WRITTEN (MB)");
        } else {
            printf("%-15s %-15s %-15s\n", "DEVICE", "READS", "WRITES");
        }
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
            
            if (json) {
                if (!first) printf(",");
                printf("{\"device\":\"%s\",\"reads\":%lu,\"writes\":%lu", dev, reads, writes);
                if (long_fmt) {
                    printf(",\"sectors_read\":%lu,\"sectors_written\":%lu", sectors_read, sectors_write);
                }
                printf("}");
                first = 0;
            } else if (long_fmt) {
                if (human) {
                    printf("%-15s %-15lu %-15lu %-15.2f %-15.2f\n", dev, reads, writes,
                           (double)sectors_read * 512.0 / 1048576.0,
                           (double)sectors_write * 512.0 / 1048576.0);
                } else {
                    printf("%-15s %-15lu %-15lu %-15lu %-15lu\n", dev, reads, writes, sectors_read, sectors_write);
                }
            } else {
                if (human) {
                    printf("%-15s %-15.2f %-15.2f\n", dev,
                           (double)sectors_read * 512.0 / 1048576.0,
                           (double)sectors_write * 512.0 / 1048576.0);
                } else {
                    printf("%-15s %-15lu %-15lu\n", dev, reads, writes);
                }
            }
        }
    }

    if (json) {
        printf("]\n");
    }
    fclose(fp);
    return 0;
}

static int do_disk_usage(int human, int long_fmt, int json) {
    struct statvfs stat;
    if (statvfs("/", &stat) != 0) {
        perror("Error executing statvfs on '/'");
        return 1;
    }

    unsigned long long total_b = (unsigned long long)stat.f_blocks * stat.f_frsize;
    unsigned long long free_b = (unsigned long long)stat.f_bfree * stat.f_frsize;
    unsigned long long used_b = total_b - free_b;

    if (json) {
        printf("{\"total_bytes\":%llu,\"used_bytes\":%llu,\"free_bytes\":%llu", total_b, used_b, free_b);
        if (long_fmt) {
            printf(",\"block_size\":%lu", (unsigned long)stat.f_bsize);
            printf(",\"fragment_size\":%lu", (unsigned long)stat.f_frsize);
            printf(",\"blocks_total\":%lu", (unsigned long)stat.f_blocks);
            printf(",\"blocks_free\":%lu", (unsigned long)stat.f_bfree);
            printf(",\"inodes_total\":%lu", (unsigned long)stat.f_files);
            printf(",\"inodes_free\":%lu", (unsigned long)stat.f_ffree);
        }
        printf("}\n");
    } else {
        printf("Disk Usage (Root Filesystem '/'):\n");

        if (long_fmt) {
            if (human) {
                char bsize_buf[32], frsize_buf[32];
                format_bytes_human((unsigned long long)stat.f_bsize, bsize_buf, sizeof(bsize_buf));
                format_bytes_human((unsigned long long)stat.f_frsize, frsize_buf, sizeof(frsize_buf));
                printf("  Block size:    %s\n", bsize_buf);
                printf("  Fragment size: %s\n", frsize_buf);
            } else {
                printf("  Block size:    %lu bytes\n", (unsigned long)stat.f_bsize);
                printf("  Fragment size: %lu bytes\n", (unsigned long)stat.f_frsize);
            }
            printf("  Blocks total:  %lu\n", (unsigned long)stat.f_blocks);
            printf("  Blocks free:   %lu\n", (unsigned long)stat.f_bfree);
            printf("  Inodes total:  %lu\n", (unsigned long)stat.f_files);
            printf("  Inodes free:   %lu\n", (unsigned long)stat.f_ffree);
        }

        if (human) {
            char total_buf[32], used_buf[32], free_buf[32];
            format_bytes_human(total_b, total_buf, sizeof(total_buf));
            format_bytes_human(used_b, used_buf, sizeof(used_buf));
            format_bytes_human(free_b, free_buf, sizeof(free_buf));
            printf("  Total: %s\n", total_buf);
            printf("  Used:  %s\n", used_buf);
            printf("  Free:  %s\n", free_buf);
        } else {
            printf("  Total: %llu bytes\n", total_b);
            printf("  Used:  %llu bytes\n", used_b);
            printf("  Free:  %llu bytes\n", free_b);
        }
    }

    return 0;
}

static int do_disk_mounts(int human, int long_fmt, int json) {
    (void)human;
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) {
        perror("Error opening /proc/mounts");
        return 1;
    }

    char line[1024];
    int first = 1;

    if (json) {
        printf("[");
    } else if (long_fmt) {
        printf("%-20s %-30s %-10s %s\n", "DEVICE", "MOUNTPOINT", "FSTYPE", "OPTIONS");
        printf("--------------------------------------------------------------------------------\n");
    } else {
        printf("%-20s %-30s %-10s\n", "DEVICE", "MOUNTPOINT", "FSTYPE");
        printf("----------------------------------------------------------------\n");
    }
    
    while (fgets(line, sizeof(line), fp)) {
        char dev[256], mnt[256], type[256], opts[256];
        if (sscanf(line, "%255s %255s %255s %255s", dev, mnt, type, opts) == 4) {
            if (json) {
                if (!first) printf(",");
                printf("{\"device\":\"%s\",\"mountpoint\":\"%s\",\"fstype\":\"%s\"", dev, mnt, type);
                if (long_fmt) {
                    printf(",\"options\":\"%s\"", opts);
                }
                printf("}");
                first = 0;
            } else if (long_fmt) {
                printf("%-20s %-30s %-10s %s\n", dev, mnt, type, opts);
            } else {
                printf("%-20s %-30s %-10s\n", dev, mnt, type);
            }
        }
    }
    
    if (json) {
        printf("]\n");
    }
    fclose(fp);
    return 0;
}

int cmd_disk(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops disk <command> [options]\n");
        return 1;
    }

    const char *first_arg = argv[1];

    if (strcmp(first_arg, "--help") == 0) {
        printf("Usage: mops disk <command> [options]\n\n");
        printf("Commands:\n");
        printf("  status    Show disk read/write statistics\n");
        printf("  usage     Show total, used, and free space on root filesystem\n");
        printf("  mounts    Display currently mounted drives from /proc/mounts\n\n");
        printf("Options:\n");
        printf("  -h        Human-readable output\n");
        printf("  -l        Long format (include extra information columns)\n");
        printf("  --json    Output in JSON format\n");
        return 0;
    }

    const char *subcmd = NULL;
    int human = 0;
    int long_fmt = 0;
    int json = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            human = 1;
        } else if (strcmp(argv[i], "-l") == 0) {
            long_fmt = 1;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (argv[i][0] != '-' && subcmd == NULL) {
            subcmd = argv[i];
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    if (!subcmd) {
        fprintf(stderr, "Usage: mops disk <command> [options]\n");
        fprintf(stderr, "Run 'mops disk --help' for more information.\n");
        return 1;
    }

    if (strcmp(subcmd, "status") == 0) {
        return do_disk_status(human, long_fmt, json);
    } else if (strcmp(subcmd, "usage") == 0) {
        return do_disk_usage(human, long_fmt, json);
    } else if (strcmp(subcmd, "mounts") == 0) {
        return do_disk_mounts(human, long_fmt, json);
    } else {
        fprintf(stderr, "Unknown disk command: %s\n", subcmd);
        fprintf(stderr, "Usage: mops disk <command> [options]\n");
        fprintf(stderr, "Run 'mops disk --help' for more information.\n");
        return 1;
    }
}