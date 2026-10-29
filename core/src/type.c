#include "type.h"
#include "stark.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// System table prefix for storing types
#define TYPE_KEY_PREFIX "type:"
#define TYPE_MANIFEST_PREFIX "__type_manifest__:"

// Provided by database_api.c: attach an internal warning to the handle.
extern void stark_set_error_msg(stark_db_t* db, const char* msg);

// ==================== HELPER FUNCTIONS ====================

// Deterministic type id derived from the name (djb2), so identical type
// definitions produce byte-identical on-disk records across processes/bindings.
static uint32_t type_name_hash(const char* s) {
    uint32_t h = 5381;
    int c;
    while ((c = (unsigned char)*s++) != 0) {
        h = ((h << 5) + h) + (uint32_t)c;
    }
    return h;
}

// ==================== TYPE CREATION ====================

stark_result_t type_create(stark_db_t* db, const char* name, 
                           FieldDef* fields, uint32_t field_count) {
    if (!db || !name || !fields || field_count == 0) 
        return STARK_INVALID_ARG;
    
    // Check if type already exists
    char type_key[256];
    snprintf(type_key, sizeof(type_key), "type:%s", name);
    
    char dummy[1];
    size_t size = 0;
    if (stark_get_str(db, type_key, dummy, &size) == STARK_OK || size > 0) {
        return STARK_ERROR;  // Type already exists
    }

    // Allocate memory for type definition
    size_t type_size = sizeof(TypeDef) + (field_count * sizeof(FieldDef));
    TypeDef* type = (TypeDef*)malloc(type_size);
    if (!type) return STARK_MEMORY_ERROR;

    // Fill type definition. Offsets and sizes are recomputed here so every
    // binding (CLI, C, C++, Python) produces an identical layout.
    type->id = type_name_hash(name);
    strncpy(type->name, name, 63);
    type->name[63] = '\0';
    type->field_count = field_count;

    uint32_t offset = 0;
    for (uint32_t i = 0; i < field_count; i++) {
        FieldDef* dst = &type->fields[i];
        memset(dst, 0, sizeof(FieldDef));  // zero padding for byte-identical output
        memcpy(dst->name, fields[i].name, sizeof(dst->name));
        dst->type = fields[i].type;
        dst->size = (dst->type == TYPE_INT) ? 4 : fields[i].size;
        dst->offset = offset;
        offset += dst->size;
    }
    type->size = offset;

    // Store in database
    stark_result_t result = stark_put_str(db, type_key, type, type_size);
    free(type);
    if (result != STARK_OK) return result;

    // Create an empty record manifest for this type (a single newline; empty
    // lines are ignored when parsing). Records are appended on add_typed.
    char manifest_key[256];
    snprintf(manifest_key, sizeof(manifest_key), "%s%s", TYPE_MANIFEST_PREFIX, name);
    size_t existing = 0;
    stark_get_str(db, manifest_key, NULL, &existing);
    if (existing == 0) {
        stark_put_str(db, manifest_key, "\n", 1);
    }

    return STARK_OK;
}

// ==================== RECORD MANIFEST ====================

// Append "<type>:<key>" to the type's manifest if not already present.
stark_result_t type_manifest_update(stark_db_t* db, const char* type_name,
                                    const char* record_key) {
    if (!db || !type_name || !record_key) return STARK_INVALID_ARG;

    char manifest_key[256];
    snprintf(manifest_key, sizeof(manifest_key), "%s%s", TYPE_MANIFEST_PREFIX, type_name);

    size_t size = 0;
    stark_get_str(db, manifest_key, NULL, &size);

    char* buf = NULL;
    size_t len = 0;
    if (size > 0) {
        buf = malloc(size + 1);
        if (!buf) return STARK_MEMORY_ERROR;
        len = size;
        if (stark_get_str(db, manifest_key, buf, &len) != STARK_OK) {
            free(buf);
            buf = NULL;
            len = 0;
        }
    }

    size_t rklen = strlen(record_key);
    int found = 0;
    if (buf) {
        size_t start = 0;
        for (size_t i = 0; i <= len; i++) {
            if (i == len || buf[i] == '\n') {
                size_t line_len = i - start;
                if (line_len == rklen && memcmp(buf + start, record_key, line_len) == 0) {
                    found = 1;
                    break;
                }
                start = i + 1;
            }
        }
    }

    if (found) {
        free(buf);
        return STARK_OK;
    }

    char* nb = malloc(len + rklen + 2);
    if (!nb) {
        free(buf);
        return STARK_MEMORY_ERROR;
    }
    if (len > 0) memcpy(nb, buf, len);
    memcpy(nb + len, record_key, rklen);
    nb[len + rklen] = '\n';
    size_t nlen = len + rklen + 1;

    stark_result_t result = stark_put_str(db, manifest_key, nb, nlen);
    free(nb);
    free(buf);
    return result;
}

// ==================== TYPE RETRIEVAL ====================

TypeDef* type_get(stark_db_t* db, const char* name) {
    if (!db || !name) return NULL;
    
    char type_key[256];
    snprintf(type_key, sizeof(type_key), "type:%s", name);
    
    // First, get the size needed
    size_t size = 0;
    stark_result_t result = stark_get_str(db, type_key, NULL, &size);
    
    // stark_get_str returns STARK_ERROR but sets size when data exists
    if (size == 0) {
        return NULL;
    }

    // Allocate buffer for type
    TypeDef* type = (TypeDef*)malloc(size);
    if (!type) return NULL;

    // Get the actual data
    result = stark_get_str(db, type_key, type, &size);
    if (result != STARK_OK) {
        free(type);
        return NULL;
    }

    return type;
}

// ==================== TYPE DELETION ====================

stark_result_t type_delete(stark_db_t* db, const char* name) {
    if (!db || !name) return STARK_INVALID_ARG;

    char type_key[256];
    snprintf(type_key, sizeof(type_key), "%s%s", TYPE_KEY_PREFIX, name);

    char manifest_key[256];
    snprintf(manifest_key, sizeof(manifest_key), "%s%s", TYPE_MANIFEST_PREFIX, name);

    // Determine whether this type has a record manifest.
    size_t msize = 0;
    stark_get_str(db, manifest_key, NULL, &msize);

    if (msize > 0) {
        char* buf = malloc(msize + 1);
        if (!buf) return STARK_MEMORY_ERROR;

        size_t len = msize;
        if (stark_get_str(db, manifest_key, buf, &len) == STARK_OK) {
            buf[len] = '\0';
            size_t start = 0;
            for (size_t i = 0; i <= len; i++) {
                if (i == len || buf[i] == '\n') {
                    size_t line_len = i - start;
                    if (line_len > 0) {
                        char line[512];
                        size_t copy = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
                        memcpy(line, buf + start, copy);
                        line[copy] = '\0';
                        // Ignore STARK_NOT_FOUND for missing records.
                        stark_del_str(db, line);
                    }
                    start = i + 1;
                }
            }
        }
        free(buf);
        stark_del_str(db, manifest_key);
    } else {
        // Backward compatibility: types defined before manifests existed.
        stark_set_error_msg(db, "warning: type has no record manifest; "
                                "orphaned records were not deleted");
    }

    // Delete the type definition itself.
    return stark_del_str(db, type_key);
}

// ==================== TYPE LISTING ====================

stark_result_t type_list(stark_db_t* db, char*** names, uint32_t* count) {
    if (!db || !names || !count) return STARK_INVALID_ARG;
    
    // For now, we need a way to iterate over all keys
    // This is a simplified approach - in a real DB you'd have a cursor
    
    // We'll use a temporary approach: try common type IDs
    // In a real implementation, you'd maintain a separate index of type names
    
    *names = NULL;
    *count = 0;
    
    // Since we don't have iteration yet, we'll return a message
    // For now, users can use 'desc <typename>' to check specific types
    
    printf("ℹ️  Type listing requires cursor iteration (to be implemented)\n");
    printf("   For now, use 'desc <typename>' to check specific types\n");
    
    return STARK_OK;
}

// ==================== FIELD PARSING ====================

stark_result_t type_parse_fields(const char* input, FieldDef** fields, 
                                  uint32_t* field_count) {
    if (!input || !fields || !field_count) return STARK_INVALID_ARG;
    
    // Make a copy of the input
    char* input_copy = strdup(input);
    if (!input_copy) return STARK_MEMORY_ERROR;
    
    // First, count the fields (pairs of name type)
    uint32_t count = 0;
    char* token = strtok(input_copy, " \t");
    while (token) {
        // Each field has a name AND a type (2 tokens)
        if (strlen(token) > 0) {
            count++;
        }
        token = strtok(NULL, " \t");
    }
    free(input_copy);
    
    // Must have even number of tokens (name, type pairs)
    if (count == 0 || count % 2 != 0) {
        return STARK_INVALID_ARG;
    }
    
    *field_count = count / 2;
    *fields = (FieldDef*)calloc(*field_count, sizeof(FieldDef));
    if (!*fields) return STARK_MEMORY_ERROR;
    
    // Now parse each field
    input_copy = strdup(input);
    if (!input_copy) {
        free(*fields);
        return STARK_MEMORY_ERROR;
    }
    
    uint32_t field_idx = 0;
    char* name_token = strtok(input_copy, " \t");
    
    while (name_token && field_idx < *field_count) {
        // Get field name
        strncpy((*fields)[field_idx].name, name_token, 31);
        (*fields)[field_idx].name[31] = '\0';
        
        // Get field type (next token)
        char* type_token = strtok(NULL, " \t");
        if (!type_token) {
            free(input_copy);
            free(*fields);
            return STARK_INVALID_ARG;
        }
        
        // Parse type (may include size like string(64))
        char type_str[32] = {0};
        char size_str[32] = {0};
        
        int parsed = sscanf(type_token, "%31[^(](%31[^)])", type_str, size_str);
        if (parsed < 1) {
            // Just copy the whole token as type
            strncpy(type_str, type_token, 31);
            type_str[31] = '\0';
        }
        
        if (strcmp(type_str, "int") == 0) {
            (*fields)[field_idx].type = TYPE_INT;
            (*fields)[field_idx].size = 4;
        } else if (strcmp(type_str, "string") == 0) {
            (*fields)[field_idx].type = TYPE_STRING;
            if (strlen(size_str) > 0) {
                (*fields)[field_idx].size = atoi(size_str);
            } else {
                (*fields)[field_idx].size = 64;  // default
            }
        } else {
            // Unknown type
            free(input_copy);
            free(*fields);
            return STARK_INVALID_ARG;
        }
        
        field_idx++;
        name_token = strtok(NULL, " \t");
    }
    
    free(input_copy);
    
    // Calculate offsets
    uint32_t offset = 0;
    for (uint32_t i = 0; i < *field_count; i++) {
        (*fields)[i].offset = offset;
        offset += (*fields)[i].size;
    }
    
    return STARK_OK;
}

// ==================== DATA SERIALIZATION ====================

static int parse_value(const char* value_str, uint8_t type, void* output) {
    if (type == TYPE_INT) {
        *((uint32_t*)output) = (uint32_t)atoi(value_str);
        return 1;
    } else if (type == TYPE_STRING) {
        // Remove quotes if present
        char temp[256];
        strncpy(temp, value_str, 255);
        temp[255] = '\0';
        
        if (temp[0] == '"') {
            char* end = strrchr(temp, '"');
            if (end) *end = '\0';
            strcpy((char*)output, temp + 1);
        } else {
            strcpy((char*)output, temp);
        }
        return 1;
    }
    return 0;
}

stark_result_t type_serialize(FieldDef* fields, uint32_t field_count,
                              const char* field_values, void* buffer) {
    if (!fields || !buffer || !field_values) return STARK_INVALID_ARG;
    
    char* values_copy = strdup(field_values);
    if (!values_copy) return STARK_ERROR;
    
    char* token = strtok(values_copy, " \t");
    while (token) {
        // Parse field=value
        char* equals = strchr(token, '=');
        if (equals) {
            *equals = '\0';
            char* field_name = token;
            char* value_str = equals + 1;
            
            // Find matching field
            int found = 0;
            for (uint32_t i = 0; i < field_count; i++) {
                if (strcmp(fields[i].name, field_name) == 0) {
                    // Write value at field offset
                    void* dest = (char*)buffer + fields[i].offset;
                    parse_value(value_str, fields[i].type, dest);
                    found = 1;
                    break;
                }
            }
            if (!found) {
                printf("Warning: Unknown field '%s' ignored\n", field_name);
            }
        }
        token = strtok(NULL, " \t");
    }
    
    free(values_copy);
    return STARK_OK;
}

stark_result_t type_deserialize_ex(FieldDef* fields, uint32_t field_count,
                                   const void* buffer, char* output,
                                   size_t output_size, size_t* required) {
    if (!fields || !buffer || !output) return STARK_INVALID_ARG;

    // First pass: measure the exact output length without writing anything.
    size_t need = 1;  // NUL terminator
    for (uint32_t i = 0; i < field_count; i++) {
        const void* src = (const char*)buffer + fields[i].offset;
        int n;
        if (fields[i].type == TYPE_INT) {
            uint32_t value = *((uint32_t*)src);
            n = snprintf(NULL, 0, "%s=%u", fields[i].name, value);
        } else if (fields[i].type == TYPE_STRING) {
            char value[256];
            size_t cap = fields[i].size < sizeof(value) ? fields[i].size : sizeof(value);
            if (cap == 0) cap = 1;
            strncpy(value, (const char*)src, cap);
            value[cap - 1] = '\0';
            n = snprintf(NULL, 0, "%s=\"%s\"", fields[i].name, value);
        } else {
            n = 0;
        }
        if (n < 0) return STARK_ERROR;
        need += (size_t)n;
        if (i < field_count - 1) need += 1;
    }

    if (required) *required = need;
    if (output_size < need) {
        if (output_size > 0) output[0] = '\0';
        return STARK_BUFFER_TOO_SMALL;
    }

    // Second pass: write, knowing it fits.
    size_t pos = 0;
    for (uint32_t i = 0; i < field_count; i++) {
        const void* src = (const char*)buffer + fields[i].offset;
        if (fields[i].type == TYPE_INT) {
            uint32_t value = *((uint32_t*)src);
            pos += (size_t)snprintf(output + pos, output_size - pos,
                                    "%s=%u", fields[i].name, value);
        } else if (fields[i].type == TYPE_STRING) {
            char value[256];
            size_t cap = fields[i].size < sizeof(value) ? fields[i].size : sizeof(value);
            if (cap == 0) cap = 1;
            strncpy(value, (const char*)src, cap);
            value[cap - 1] = '\0';
            pos += (size_t)snprintf(output + pos, output_size - pos,
                                    "%s=\"%s\"", fields[i].name, value);
        }
        if (i < field_count - 1) {
            output[pos++] = ' ';
        }
    }

    return STARK_OK;
}

stark_result_t type_deserialize(FieldDef* fields, uint32_t field_count,
                                const void* buffer, char* output, size_t output_size) {
    return type_deserialize_ex(fields, field_count, buffer, output, output_size, NULL);
}

// This would require implementing cursor functionality in your B-tree
// For now, here's a placeholder:

typedef struct {
    stark_db_t* db;
    void* internal_cursor;
    int valid;
} TypeIterator;

TypeIterator* type_iterator_create(stark_db_t* db) {
    if (!db) return NULL;
    
    TypeIterator* it = malloc(sizeof(TypeIterator));
    if (!it) return NULL;
    
    it->db = db;
    it->internal_cursor = NULL;
    it->valid = 0;
    
    // TODO: Initialize B-tree cursor
    // This would require adding cursor support to your btree.c
    
    return it;
}

int type_iterator_next(TypeIterator* it, char** name) {
    if (!it || !name) return 0;
    
    // TODO: Get next key with "type:" prefix
    // For now, return 0 (no more items)
    
    return 0;
}

void type_iterator_destroy(TypeIterator* it) {
    free(it);
}