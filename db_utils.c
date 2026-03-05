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
        // Only escape double quotes, backslashes and control characters.
        // Let UTF-8 multi-byte characters pass through untouched.
        if (*input == '"' || *input == '\\') {
            *output++ = '\\';
            *output++ = *input;
        } else if (*input == '\n') {
            *output++ = '\\';
            *output++ = 'n';
        } else if (*input == '\r') {
            *output++ = '\\';
            *output++ = 'r';
        } else if (*input == '\t') {
            *output++ = '\\';
            *output++ = 't';
        } else if ((unsigned char)*input < 0x20) {
            // Drop other control characters to be safe
        } else {
            *output++ = *input;
        }
        input++;
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

int db_delete_user(int user_id) {
    sqlite3_stmt *stmt;
    // Delete messages first
    if (sqlite3_prepare_v2(db, "DELETE FROM messages WHERE user_id = ?", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    // Delete user
    if (sqlite3_prepare_v2(db, "DELETE FROM users WHERE id = ?", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }
    return 0;
}

int db_delete_user_by_token(const char* token) {
    int uid = get_user_id_by_token(token);
    if (uid == -1) return 0;
    return db_delete_user(uid);
}

char* build_json_messages(sqlite3_stmt *stmt) {
    size_t cap = 8192;
    char *res = malloc(cap);
    strcpy(res, "[");
    size_t len = 1;
    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        int sender = sqlite3_column_int(stmt, 1);
        const unsigned char* content = sqlite3_column_text(stmt, 2);
        const unsigned char* ts = sqlite3_column_text(stmt, 3);
        
        size_t content_len = content ? strlen((const char*)content) : 0;
        size_t escaped_len = content_len * 2 + 1;
        char* escaped = malloc(escaped_len);
        if(content) escape_json((const char*)content, escaped);
        else escaped[0] = '\0';
        
        char ts_buf[64] = "null";
        if (ts && strlen((const char*)ts) > 0) {
            snprintf(ts_buf, sizeof(ts_buf), "\"%s\"", (const char*)ts);
        }

        size_t item_size = strlen(escaped) + 200;
        char* buf = malloc(item_size);
        snprintf(buf, item_size, "{\"id\":%d,\"sender\":%d,\"text\":\"%s\",\"timestamp\":%s}", id, sender, escaped, ts_buf);
        
        size_t buf_len = strlen(buf);
        if (len + buf_len + 2 > cap) {
            cap *= 2;
            if (cap < len + buf_len + 2) cap = len + buf_len + 2;
            res = realloc(res, cap);
        }
        
        if (!first) { strcat(res, ","); len++; }
        first = 0;
        strcat(res, buf);
        len += buf_len;
        
        free(buf);
        free(escaped);
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
    if (sqlite3_prepare_v2(db, "SELECT id, sender_type, content, timestamp FROM messages WHERE user_id = ? AND id > ? ORDER BY id ASC", -1, &stmt, NULL) == SQLITE_OK) {
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
    size_t cap = 8192;
    char *res = malloc(cap);
    strcpy(res, "[");
    size_t len = 1;
    if (sqlite3_prepare_v2(db,
        "SELECT u.id, u.first_name, u.surname, u.phone, "
        "COALESCE(m.id, 0) as last_msg_id, COALESCE(m.sender_type, -1) as last_sender "
        "FROM users u "
        "LEFT JOIN messages m ON m.id = ("
        "  SELECT id FROM messages WHERE user_id = u.id ORDER BY id DESC LIMIT 1"
        ") ORDER BY last_msg_id DESC",
        -1, &stmt, NULL) == SQLITE_OK) {
        int first = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            const unsigned char* fn = sqlite3_column_text(stmt, 1);
            const unsigned char* sn = sqlite3_column_text(stmt, 2);
            const unsigned char* ph = sqlite3_column_text(stmt, 3);
            int last_msg_id = sqlite3_column_int(stmt, 4);
            int last_sender  = sqlite3_column_int(stmt, 5);
            
            char e_fn[256]={0}, e_sn[256]={0}, e_ph[256]={0};
            if(fn) escape_json((const char*)fn, e_fn);
            if(sn) escape_json((const char*)sn, e_sn);
            if(ph) escape_json((const char*)ph, e_ph);
            
            char buf[1024];
            snprintf(buf, sizeof(buf), "{\"id\":%d,\"name\":\"%s %s\",\"phone\":\"%s\",\"last_msg_id\":%d,\"last_sender\":%d}",
                     id, e_fn, e_sn, e_ph, last_msg_id, last_sender);
            
            size_t buf_len = strlen(buf);
            if (len + buf_len + 2 > cap) {
                cap *= 2;
                if (cap < len + buf_len + 2) cap = len + buf_len + 2;
                res = realloc(res, cap);
            }
            
            if (!first) { strcat(res, ","); len++; }
            first = 0;
            strcat(res, buf);
            len += buf_len;
        }
        sqlite3_finalize(stmt);
    }
    strcat(res, "]");
    return res;
}
