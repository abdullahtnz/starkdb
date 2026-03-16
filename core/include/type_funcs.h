#ifndef TYPE_FUNCS_H
#define TYPE_FUNCS_H

#include "stark.h"
#include "type.h"

// Type management functions
stark_result_t type_create(stark_db_t* db, const char* name, 
                           FieldDef* fields, uint32_t field_count);
TypeDef* type_get(stark_db_t* db, const char* name);
stark_result_t type_delete(stark_db_t* db, const char* name);

// Serialization functions
stark_result_t type_serialize(FieldDef* fields, uint32_t field_count,
                              const char* field_values, void* buffer);
stark_result_t type_deserialize(FieldDef* fields, uint32_t field_count,
                                const void* buffer, char* output, size_t output_size);

#endif