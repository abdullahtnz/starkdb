#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "urage.h"

void print_help() {
    printf("\nURAGE Database CLI\n");
    printf("===================\n");
    printf("Commands:\n");
    printf("  add <key> <value>     - Insert/update key with value\n");
    printf("  get <key>             - Retrieve value by key\n");
    printf("  del <key>             - Delete key\n");
    printf("  exists <key>          - Check if key exists\n");
    printf("  stats                 - Show database stats\n");
    printf("  sync                  - Flush to disk\n");
    printf("  help                  - Show this help\n");
    printf("  exit                  - Exit program\n");
}

int main(int argc, char* argv[]) {
    const char* db_path = argc > 1 ? argv[1] : "mydb";
    
    printf("Opening database: %s\n", db_path);
    
    urage_db_t* db = urage_open(db_path, 0);
    if (!db) {
        printf("Failed to open database!\n");
        return 1;
    }
    
    printf("URAGE Database ready. Type 'help' for commands.\n");
    
    char line[512];
    char cmd[32];
    uint32_t key;
    char value[256];
    
    while (1) {
        printf("\nurage> ");
        fflush(stdout);
        
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = 0;
        
        if (strlen(line) == 0) continue;
        
        // Parse command
        if (sscanf(line, "%s", cmd) != 1) continue;
        
        if (strcmp(cmd, "add") == 0) {
            if (sscanf(line, "%*s %u %255s", &key, value) == 2) {
                urage_result_t r = urage_add(db, key, value, strlen(value) + 1);
                if (r == URAGE_OK)
                    printf("OK: %u -> %s\n", key, value);
                else
                    printf("Error: %d\n", r);
            } else {
                printf("Usage: add <key> <value>\n");
            }
        }
        else if (strcmp(cmd, "get") == 0) {
            if (sscanf(line, "%*s %u", &key) == 1) {
                char buffer[256];
                size_t size = sizeof(buffer);
                urage_result_t r = urage_get(db, key, buffer, &size);
                if (r == URAGE_OK) {
                    buffer[size] = '\0';
                    printf("%u -> %s\n", key, buffer);
                } else if (r == URAGE_NOT_FOUND) {
                    printf("Key %u not found\n", key);
                } else {
                    printf("Error: %d\n", r);
                }
            } else {
                printf("Usage: get <key>\n");
            }
        }
        else if (strcmp(cmd, "del") == 0) {
            if (sscanf(line, "%*s %u", &key) == 1) {
                urage_result_t r = urage_delete(db, key);
                if (r == URAGE_OK)
                    printf("Key %u deleted\n", key);
                else if (r == URAGE_NOT_FOUND)
                    printf("Key %u not found\n", key);
                else
                    printf("Error: %d\n", r);
            } else {
                printf("Usage: del <key>\n");
            }
        }
        else if (strcmp(cmd, "exists") == 0) {
            if (sscanf(line, "%*s %u", &key) == 1) {
                int exists = urage_exists(db, key);
                printf("Key %u %s\n", key, exists ? "exists" : "does not exist");
            } else {
                printf("Usage: exists <key>\n");
            }
        }
        else if (strcmp(cmd, "stats") == 0) {
            urage_stats_t stats;
            if (urage_stats(db, &stats) == URAGE_OK) {
                printf("Database Statistics:\n");
                printf("  Keys: %llu\n", (unsigned long long)stats.keys_count);
                printf("  B-tree height: %u\n", stats.btree_height);
                printf("  Data size: %llu bytes\n", (unsigned long long)stats.data_size);
                printf("  Pages: %u\n", stats.page_count);
            } else {
                printf("Failed to get stats\n");
            }
        }
        else if (strcmp(cmd, "sync") == 0) {
            if (urage_sync(db) == URAGE_OK)
                printf("Synced to disk\n");
            else
                printf("Sync failed\n");
        }
        else if (strcmp(cmd, "help") == 0) {
            print_help();
        }
        else if (strcmp(cmd, "exit") == 0) {
            break;
        }
        else {
            printf("Unknown command. Type 'help'\n");
        }
    }
    
    urage_close(db);
    printf("Database closed.\n");
    return 0;
}