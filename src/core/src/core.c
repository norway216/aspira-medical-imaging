/**
 * @file core.c
 * @brief Core library utility functions
 */

#include "aspira/core/core.h"

const char* aspira_strerror(aspira_error_t err) {
    switch (err) {
    case ASPIRA_OK:               return "Success";
    case ASPIRA_ERROR_NULL_POINTER: return "Null pointer";
    case ASPIRA_ERROR_INVALID_ARG:  return "Invalid argument";
    case ASPIRA_ERROR_NO_MEMORY:    return "Out of memory";
    case ASPIRA_ERROR_FULL:         return "Buffer full";
    case ASPIRA_ERROR_EMPTY:        return "Buffer empty";
    case ASPIRA_ERROR_TIMEOUT:      return "Timeout";
    case ASPIRA_ERROR_BUSY:         return "Resource busy";
    case ASPIRA_ERROR_NOT_FOUND:    return "Not found";
    case ASPIRA_ERROR_OVERFLOW:     return "Overflow";
    case ASPIRA_ERROR_INTERNAL:     return "Internal error";
    default:                        return "Unknown error";
    }
}

const char* aspira_version(void) {
    return "Aspira "
           "0.1.0";
}
