#ifndef REGISTRY_H
#define REGISTRY_H

#ifdef FEOS
#include <feos.h>
#else
#define FEOS_EXPORT
#endif
#include <sqlite3.h>
#include <stdint.h>

typedef enum {
  KEY_VOID,   /* NULL data */
  KEY_NUMBER, /* 64-bit int, signed or unsigned (user keeps track of signedness) */
  KEY_STRING, /* string data */
  KEY_RAW,    /* binary data */
} KeyType;

typedef struct {
  char *name;   /* key name (free() during cleanup) */
  KeyType type; /* key type */
  union {
    uint64_t number; /* if type == KEY_NUMBER */
    char *string;    /* if type == KEY_STRING (free() during cleanup) */
    struct {
      size_t length; /* length of data */
      void *data;    /* the data (free() during cleanup) */
    } raw;
  } value;
} KeyPair;

/* open, close, initialize (erase) registry. returns 0 for success, -1 for failure
   all failures will set errno
*/
FEOS_EXPORT int regOpen (void);
FEOS_EXPORT int regClose(void);
FEOS_EXPORT int regInit (void);

/* path: in format /path/to/key
   '/' is not a valid key (it is the special root key that cannot be accessed)
   value: the data to insert
   length: length of the data (for raw; string length is calculated via strlen())

   returns 0 for success, -1 for failure
   all failures will set errno

   To add a new key+value:
     regAddKey("/path/to/key");
     regSet[type]("/path/to/key", ...);

   default type is KEY_VOID
*/
FEOS_EXPORT int      regAddKey    (const char *path);
FEOS_EXPORT int      regDelKey    (const char *path);
FEOS_EXPORT int      regSetVoid   (const char *path);
FEOS_EXPORT int      regSetNumber (const char *path, uint64_t    value);
FEOS_EXPORT int      regSetString (const char *path, const char *value);
FEOS_EXPORT int      regSetRaw    (const char *path, const void *value, size_t length);

/* path: same as above
   returns KeyPair* on success, NULL on failure
   all failures will set errno
*/
FEOS_EXPORT KeyPair* regGetKeyPair(const char *path);

#endif /* REGISTRY_H */

