#ifndef DB_UTILS_H
#define DB_UTILS_H

#include <sqlite3.h>

void db_init(const char* filename);
void db_close();
int db_create_user(const char* first_name, const char* surname, const char* phone, char* session_token_out);
int db_save_message(const char* session_token, int sender_type, const char* content);
char* db_get_messages(const char* session_token, int after_msg_id);
char* db_get_users();
char* db_get_messages_admin(int user_id, int after_msg_id);
int db_save_message_admin(int user_id, const char* content);
int db_delete_user(int user_id);
int db_delete_user_by_token(const char* token);

#endif
