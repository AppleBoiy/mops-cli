#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include "mops.h"

#define DEFAULT_DB_PATH "mops.db"

static sqlite3 *db = NULL;

int db_init(void) {
    const char *db_path = getenv("MOPS_DB_PATH");
    if (!db_path || db_path[0] == '\0') {
        db_path = DEFAULT_DB_PATH;
    }
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    const char *sql = "CREATE TABLE IF NOT EXISTS tasks ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                      "pid INTEGER, "
                      "command TEXT NOT NULL, "
                      "status TEXT NOT NULL, "
                      "exit_code INTEGER, "
                      "notify_url TEXT, "
                      "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";

    char *err_msg = NULL;
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc == SQLITE_OK) {
        sqlite3_exec(db, "ALTER TABLE tasks ADD COLUMN notify_url TEXT;", 0, 0, NULL);
        sqlite3_exec(db, "ALTER TABLE tasks ADD COLUMN exit_code INTEGER;", 0, 0, NULL);
    }
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to create table: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }

    return 0;
}

void db_close(void) {
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}

sqlite3* db_get_connection(void) {
    return db;
}