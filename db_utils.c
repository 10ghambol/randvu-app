#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>
#include "db_utils.h"

sqlite3 *db;

void db_init(const char* filename) {
    if (sqlite3_open(filename, &db)) {
        fprintf(stderr, "Can't open DB: %s\n", sqlite3_errmsg(db));
        exit(1);
    }
    const char *sql1 = "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY AUTOINCREMENT, token TEXT, first_name TEXT, surname TEXT, phone TEXT);";
    const char *sql2 = "CREATE TABLE IF NOT EXISTS messages (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER, sender_type INTEGER, content TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";
    sqlite3_exec(db, sql1, 0, 0, 0);
    sqlite3_exec(db, sql2, 0, 0, 0);
}

void db_close() {
    sqlite3_close(db);
}

void escape_json(const char *input, char *output) {
    while (*input) {
        if (*input == '"' || *input == '\\') {
            *output++ = '\\';
        }
        *output++ = *input++;
    }
    *output = '\0';
}

void generate_token(char* out) {
    sprintf(out, "%ld%d", (long)time(NULL), rand() % 10000);
}

int db_create_user(const char* first_name, const char* surname, const char* phone, char* session_token_out) {
    generate_token(session_token_out);
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO users (token, first_name, surname, phone) VALUES (?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    
    sqlite3_bind_text(stmt, 1, session_token_out, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, first_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, surname, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, phone, -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int get_user_id_by_token(const char* token) {
    sqlite3_stmt *stmt;
    int id = -1;
    if (sqlite3_prepare_v2(db, "SELECT id FROM users WHERE token = ?", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, token, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            id = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return id;
}

int db_save_message_internal(int user_id, int sender_type, const char* content) {
    sqlite3_stmt *stmt;
    int rc = 0;
    if (sqlite3_prepare_v2(db, "INSERT INTO messages (user_id, sender_type, content) VALUES (?, ?, ?)", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        sqlite3_bind_int(stmt, 2, sender_type);
        sqlite3_bind_text(stmt, 3, content, -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
    }
    return rc;
}

int db_save_message(const char* session_token, int sender_type, const char* content) {
    int uid = get_user_id_by_token(session_token);
    if (uid == -1) return 0;
    return db_save_message_internal(uid, sender_type, content);
}

int db_save_message_admin(int user_id, const char* content) {
    return db_save_message_internal(user_id, 1, content);
}

char* build_json_messages(sqlite3_stmt *stmt) {
    char *res = malloc(8192);
    strcpy(res, "[");
    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) strcat(res, ",");
        first = 0;
        int id = sqlite3_column_int(stmt, 0);
        int sender = sqlite3_column_int(stmt, 1);
        const unsigned char* content = sqlite3_column_text(stmt, 2);
        
        char escaped[1024] = {0};
        if(content) escape_json((const char*)content, escaped);
        
        char buf[2048];
        snprintf(buf, sizeof(buf), "{\"id\":%d,\"sender\":%d,\"text\":\"%s\"}", id, sender, escaped);
        if (strlen(res) + strlen(buf) < 8000) strcat(res, buf);
    }
    strcat(res, "]");
    return res;
}

char* db_get_messages(const char* session_token, int after_msg_id) {
    int uid = get_user_id_by_token(session_token);
    if (uid == -1) {
        char* empty = malloc(3);
        strcpy(empty, "[]");
        return empty;
    }
    return db_get_messages_admin(uid, after_msg_id);
}

char* db_get_messages_admin(int user_id, int after_msg_id) {
    sqlite3_stmt *stmt;
    char *res = NULL;
    if (sqlite3_prepare_v2(db, "SELECT id, sender_type, content FROM messages WHERE user_id = ? AND id > ? ORDER BY id ASC", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        sqlite3_bind_int(stmt, 2, after_msg_id);
        res = build_json_messages(stmt);
        sqlite3_finalize(stmt);
    } else {
        res = malloc(3);
        strcpy(res, "[]");
    }
    return res;
}

char* db_get_users() {
    sqlite3_stmt *stmt;
    char *res = malloc(8192);
    strcpy(res, "[");
    if (sqlite3_prepare_v2(db, "SELECT id, first_name, surname, phone FROM users", -1, &stmt, NULL) == SQLITE_OK) {
        int first = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!first) strcat(res, ",");
            first = 0;
            int id = sqlite3_column_int(stmt, 0);
            const unsigned char* fn = sqlite3_column_text(stmt, 1);
            const unsigned char* sn = sqlite3_column_text(stmt, 2);
            const unsigned char* ph = sqlite3_column_text(stmt, 3);
            
            char e_fn[256]={0}, e_sn[256]={0}, e_ph[256]={0};
            if(fn) escape_json((const char*)fn, e_fn);
            if(sn) escape_json((const char*)sn, e_sn);
            if(ph) escape_json((const char*)ph, e_ph);
            
            char buf[1024];
            snprintf(buf, sizeof(buf), "{\"id\":%d,\"name\":\"%s %s\",\"phone\":\"%s\"}", id, e_fn, e_sn, e_ph);
            if (strlen(res) + strlen(buf) < 8000) strcat(res, buf);
        }
        sqlite3_finalize(stmt);
    }
    strcat(res, "]");
    return res;
}
