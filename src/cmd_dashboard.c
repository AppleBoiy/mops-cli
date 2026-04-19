#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>
#include <time.h>
#include "mops.h"

/*
 * Dashboard Metric Helpers
 */

struct dashboard_stats {
    double cpu_usage;
    unsigned long long mem_total;
    unsigned long long mem_used;
    int task_count;
};

static void get_mem_stats(unsigned long long *total, unsigned long long *used) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;
    unsigned long long free = 0, avail = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0) sscanf(line, "MemTotal: %llu kB", total);
        else if (strncmp(line, "MemFree:", 8) == 0) sscanf(line, "MemFree: %llu kB", &free);
        else if (strncmp(line, "MemAvailable:", 13) == 0) sscanf(line, "MemAvailable: %llu kB", &avail);
    }
    fclose(fp);
    if (avail > 0) *used = *total - avail;
    else *used = *total - free;
}

static int get_task_count() {
    sqlite3 *db = db_get_connection();
    if (!db) return 0;
    sqlite3_stmt *stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM tasks", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

void draw_bar(int y, int x, int width, double pct, const char *label) {
    mvprintw(y, x, "%s: [", label);
    int bar_width = width - strlen(label) - 5;
    int filled = (int)(bar_width * (pct / 100.0));
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) addch('#');
        else addch(' ');
    }
    printw("] %.1f%%", pct);
}

int cmd_dashboard(int argc, char **argv) {
    (void)argc; (void)argv;

    initscr();
    noecho();
    curs_set(0);
    timeout(1000); /* 1s refresh */
    start_color();
    init_pair(1, COLOR_CYAN, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);

    int row, col;
    getmaxyx(stdscr, row, col);

    while (1) {
        unsigned long long total_mem = 0, used_mem = 0;
        get_mem_stats(&total_mem, &used_mem);
        double mem_pct = total_mem > 0 ? (double)used_mem / total_mem * 100.0 : 0.0;
        int tasks = get_task_count();

        clear();
        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(1, (col - 16) / 2, "MOPS DASHBOARD");
        attroff(COLOR_PAIR(1) | A_BOLD);

        mvprintw(3, 2, "System Resources:");
        draw_bar(4, 4, col - 8, mem_pct, "Memory");
        
        mvprintw(6, 2, "Task Queue:");
        attron(COLOR_PAIR(2));
        mvprintw(7, 4, "Total Tasks tracked: %d", tasks);
        attroff(COLOR_PAIR(2));

        mvprintw(row - 2, 2, "Press 'q' to exit | Refresh: 1s");

        /* Task table header */
        mvprintw(9, 2, "%-5s | %-10s | %s", "ID", "Status", "Command");
        mvprintw(10, 2, "--------------------------------------------------");

        sqlite3 *db = db_get_connection();
        if (db) {
            sqlite3_stmt *stmt;
            const char *sql = "SELECT id, status, command FROM tasks ORDER BY id DESC LIMIT 5";
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
                int r = 11;
                while (sqlite3_step(stmt) == SQLITE_ROW && r < row - 4) {
                    int id = sqlite3_column_int(stmt, 0);
                    const char *status = (const char *)sqlite3_column_text(stmt, 1);
                    const char *cmd = (const char *)sqlite3_column_text(stmt, 2);
                    mvprintw(r++, 2, "%-5d | %-10s | %s", id, status, cmd);
                }
                sqlite3_finalize(stmt);
            }
        }

        refresh();

        int ch = getch();
        if (ch == 'q') break;
    }

    endwin();
    return 0;
}
