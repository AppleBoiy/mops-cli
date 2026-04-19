#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>

/*
 * Find the inode associated with a given local port.
 * Parses /proc/net/tcp or /proc/net/tcp6.
 */
static unsigned long find_inode_for_port(int search_port, const char *proto_file) {
    FILE *fp = fopen(proto_file, "r");
    if (!fp) return 0;

    char line[512];
    /* Skip header line */
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return 0;
    }

    while (fgets(line, sizeof(line), fp)) {
        unsigned int local_ip, local_port;
        unsigned long inode = 0;
        
        /*
         * /proc/net/tcp format:
         * sl local_address rem_address st tx_queue rx_queue tr tm->when retrnsmt uid timeout inode
         */
        if (sscanf(line, "%*s %x:%x %*s %*s %*s %*s %*s %*s %*s %lu", 
                   &local_ip, &local_port, &inode) >= 3) {
            if ((int)local_port == search_port) {
                fclose(fp);
                return inode;
            }
        }
    }
    
    fclose(fp);
    return 0;
}

/*
 * Scan /proc/[pid]/fd/ to find the process holding the socket inode.
 */
static void find_process_by_inode(unsigned long target_inode) {
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) {
        fprintf(stderr, "Error: Cannot open /proc. Are you on Linux?\n");
        return;
    }

    struct dirent *proc_ent;
    while ((proc_ent = readdir(proc_dir)) != NULL) {
        /* Check if directory name is numeric (indicates a PID) */
        if (!isdigit(proc_ent->d_name[0])) continue;

        char fd_path[512];
        snprintf(fd_path, sizeof(fd_path), "/proc/%s/fd", proc_ent->d_name);

        DIR *fd_dir = opendir(fd_path);
        if (!fd_dir) continue;

        struct dirent *fd_ent;
        while ((fd_ent = readdir(fd_dir)) != NULL) {
            if (fd_ent->d_name[0] == '.') continue;

            char link_path[512];
            snprintf(link_path, sizeof(link_path), "%s/%s", fd_path, fd_ent->d_name);

            char link_dest[256];
            ssize_t len = readlink(link_path, link_dest, sizeof(link_dest) - 1);
            if (len != -1) {
                link_dest[len] = '\0';
                unsigned long ino = 0;
                
                /* Match socket:[inode] format */
                if (sscanf(link_dest, "socket:[%lu]", &ino) == 1) {
                    if (ino == target_inode) {
                        /* Found the process! Read its command line */
                        char cmd_path[512];
                        snprintf(cmd_path, sizeof(cmd_path), "/proc/%s/cmdline", proc_ent->d_name);
                        FILE *cmd_fp = fopen(cmd_path, "r");
                        char cmdline[1024] = {0};
                        
                        if (cmd_fp) {
                            size_t n = fread(cmdline, 1, sizeof(cmdline) - 1, cmd_fp);
                            for (size_t i = 0; i < n; i++) {
                                if (cmdline[i] == '\0') cmdline[i] = ' ';
                            }
                            fclose(cmd_fp);
                        }
                        
                        printf("PID: %s | Command: %s\n", proc_ent->d_name, cmdline);
                        
                        closedir(fd_dir);
                        closedir(proc_dir);
                        return;
                    }
                }
            }
        }
        closedir(fd_dir);
    }
    closedir(proc_dir);
    
    printf("Could not map inode %lu to an active PID (You might need root privileges).\n", target_inode);
}

/*
 * Handle `mops net port <number>`
 */
int cmd_net_port(int argc, char **argv) {
    if (argc > 0 && (strcmp(argv[0], "--help") == 0 || strcmp(argv[0], "-h") == 0)) {
        printf("Usage: mops net port <number>\n\n");
        printf("Finds the exact process ID (PID) and command line associated with a local TCP port.\n");
        return 0;
    }

    if (argc < 1) {
        fprintf(stderr, "Usage: mops net port <number>\n");
        fprintf(stderr, "Run 'mops net --help' for more information.\n");
        return 1;
    }

    int port = atoi(argv[0]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port number: %s\n", argv[0]);
        return 1;
    }

    printf("Investigating port %d...\n", port);

    /* Check TCP IPv4 */
    unsigned long inode = find_inode_for_port(port, "/proc/net/tcp");
    
    /* Check TCP IPv6 if not found */
    if (!inode) {
        inode = find_inode_for_port(port, "/proc/net/tcp6");
    }

    if (!inode) {
        printf("No active TCP socket found listening on port %d.\n", port);
        return 0;
    }

    printf("Found socket inode: %lu. Scanning process tree...\n", inode);
    find_process_by_inode(inode);

    return 0;
}

/*
 * Main Dispatcher for `net` subcommand
 */
int cmd_net(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mops net <subcommand> [options]\n");
        fprintf(stderr, "Run 'mops net --help' for more information.\n");
        return 1;
    }

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "--help") == 0 || strcmp(subcmd, "-h") == 0) {
        printf("Usage: mops net <subcommand> [options]\n\n");
        printf("Commands:\n");
        printf("  port      Investigate which process is listening on a specific local port\n");
        return 0;
    } else if (strcmp(subcmd, "port") == 0) {
        return cmd_net_port(argc - 2, argv + 2);
    } else {
        fprintf(stderr, "Unknown net subcommand: %s\n", subcmd);
        fprintf(stderr, "Usage: mops net <subcommand> [options]\n");
        fprintf(stderr, "Run 'mops net --help' for more information.\n");
        return 1;
    }
}