#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "registry.h"

static char query[1024];
sqlite3 *db = NULL;

typedef uint64_t KeyId;

typedef enum {
  Q_GETKEY,
  Q_ADDKEY,
  Q_ADDKEY2,
  Q_ADDKEY3,
  Q_GETKEYTYPE,
  Q_SETNUMBER,
  Q_SETSTRING,
  Q_SETRAW,
  Q_SETRAW2,
  Q_SETRAW3,
  Q_DELKEY,
  Q_GETKPNUM,
  Q_GETKPSTR,
  Q_GETKPRAW,
} Query;

static struct {
  sqlite3_stmt *stmt;
  const char * const query;
} queries[] = {
  [Q_GETKEY]     = { NULL, "select rowid from key where name = ? and parent = ?;", },
  [Q_ADDKEY]     = { NULL, "select * from key where parent = ? and name = ?;", },
  [Q_ADDKEY2]    = { NULL, "insert into key (parent, name, type) values(?, ?, ?);", },
  [Q_ADDKEY3]    = { NULL, "update key set parent = ? where rowid = ?;", },
  [Q_GETKEYTYPE] = { NULL, "select type from key where rowid = ?;", },
  [Q_SETNUMBER]  = { NULL, "update number set value = ? where parent = ?;", },
  [Q_SETSTRING]  = { NULL, "update string set value = ? where parent = ?;", },
  [Q_SETRAW]     = { NULL, "update raw set value = ? where parent = ?;", },
  [Q_SETRAW2]    = { NULL, "insert into raw (parent, value) values(?, ?);", },
  [Q_SETRAW3]    = { NULL, "update key set type = ? where rowid = ?;", },
  [Q_DELKEY]     = { NULL, "delete from key where rowid = ?;", },
  [Q_GETKPNUM]   = { NULL, "select value from number where parent = ?;", },
  [Q_GETKPSTR]   = { NULL, "select value from string where parent = ?;", },
  [Q_GETKPRAW]   = { NULL, "select value from raw    where parent = ?;", },
};

static char errs[] = {
  [SQLITE_OK]         = 0,
  [SQLITE_ERROR]      = EIO,
  [SQLITE_INTERNAL]   = EIO,
  [SQLITE_PERM]       = EPERM,
  [SQLITE_ABORT]      = EIO,
  [SQLITE_BUSY]       = EBUSY,
  [SQLITE_LOCKED]     = EIO,
  [SQLITE_NOMEM]      = ENOMEM,
  [SQLITE_READONLY]   = EBADF,
  [SQLITE_INTERRUPT]  = EINTR,
  [SQLITE_IOERR]      = EIO,
  [SQLITE_CORRUPT]    = EILSEQ,
  [SQLITE_NOTFOUND]   = EIO,
  [SQLITE_FULL]       = ENOSPC,
  [SQLITE_CANTOPEN]   = EIO,
  [SQLITE_PROTOCOL]   = EIO,
  [SQLITE_EMPTY]      = ENODATA,
  [SQLITE_SCHEMA]     = EIO,
  [SQLITE_TOOBIG]     = EOVERFLOW,
  [SQLITE_CONSTRAINT] = EIO,
  [SQLITE_MISMATCH]   = EIO,
  [SQLITE_MISUSE]     = EIO,
  [SQLITE_NOLFS]      = ENOSYS,
  [SQLITE_AUTH]       = EPERM,
  [SQLITE_FORMAT]     = EIO,
  [SQLITE_RANGE]      = ERANGE,
  [SQLITE_NOTADB]     = ENODEV,
};

static inline KeyId   regGetKey(const char *path);
static inline KeyType regGetKeyType(KeyId id);
static inline int     regInit(void);

static inline int errmap(int sqlite_err) {
  if(sqlite_err >= SQLITE_OK && sqlite_err <= SQLITE_NOTADB)
    return errs[sqlite_err];
  else
    return 0;
}      

static inline sqlite3_stmt* LOAD(int x) {
  int rc;
  if(queries[x].stmt == NULL) {
    rc = sqlite3_prepare_v2(db, queries[x].query, strlen(queries[x].query)+1, &queries[x].stmt, NULL);
    if(rc != SQLITE_OK) {
      errno = errmap(sqlite3_errcode(db));
      return NULL;
    }
  }

  return queries[x].stmt;
}

int regOpen(void) {
  int rc;
  int flags = SQLITE_OPEN_READWRITE;
  if(db == NULL) {
    rc = sqlite3_open_v2("/data/FeOS/registry.bin", &db, flags, NULL);
    if(rc == SQLITE_CANTOPEN) {
      flags |= SQLITE_OPEN_CREATE;
      rc = sqlite3_open_v2("/data/FeOS/registry.bin", &db, flags, NULL);
    }
    if(rc != SQLITE_OK) {
      errno = errmap(sqlite3_errcode(db));
      return -1;
    }
    rc = sqlite3_exec(db, "pragma journal_mode = memory;", NULL, NULL, NULL);
    assert(rc == SQLITE_OK);
    rc = sqlite3_exec(db, "pragma foreign_keys = on;", NULL, NULL, NULL);
    assert(rc == SQLITE_OK);

    if((flags & SQLITE_OPEN_CREATE) && regInit()) {
      errno = errmap(sqlite3_errcode(db));
      return -1;
    }
  }
  else {
    errno = EBUSY;
    return -1;
  }

  return 0;
}

int regClose(void) {
  int rc;
  int i;

  for(i = 0; i < sizeof(queries)/sizeof(queries[0]); i++) {
    rc = sqlite3_finalize(queries[i].stmt);
    assert(rc == SQLITE_OK);
    queries[i].stmt = NULL;
  }

  rc = sqlite3_close(db);
  assert(rc == SQLITE_OK);
  (void)rc;
  db = NULL;

  return 0;
}

static inline int regInit(void) {
  int rc;

  rc = sqlite3_exec(db, "drop table if exists key; "
                        "drop table if exists number; "
                        "drop table if exists string; "
                        "drop table if exists raw; "
    "create table key   (id integer primary key autoincrement, parent int references key(id) on delete cascade, name text, type int); "
    "create table number(id integer primary key autoincrement, parent int references key(id) on delete cascade, value int); "
    "create table string(id integer primary key autoincrement, parent int references key(id) on delete cascade, value text); "
    "create table raw   (id integer primary key autoincrement, parent int references key(id) on delete cascade, value blob); "
    "insert into  key   (id, parent, name, type) values (0, 0, '/', 0)",
       NULL, NULL, NULL);
  if(rc != SQLITE_OK) {
    errno = errmap(sqlite3_errcode(db));
    return -1;
  }

  return 0;
}

KeyId regGetKey(const char *path) {
  sqlite3_stmt *stmt;
  int rc;
  char *_path;
  char *part;
  sqlite3_int64 parent = 0;
  sqlite3_int64 id;

  stmt = LOAD(Q_GETKEY); /* "select rowid from key where name = ? and parent = ?;" */
  if(stmt == NULL)
    return 0;

  _path = strdup(path);
  if(_path == NULL) {
    errno = ENOMEM;
    return 0;
  }

  part = strtok(_path, "/");
  if(part == NULL) {
    free(_path);
    errno = EINVAL;
    return 0;
  }

  while(part) {
    rc = sqlite3_reset(stmt);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_text(stmt, 1, part, strlen(part), SQLITE_STATIC);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int64(stmt, 2, parent);
    assert(rc == SQLITE_OK);

    rc = sqlite3_step(stmt);
    if(rc == SQLITE_DONE) { /* empty result */
      errno = ENOENT;
      free(_path);
      return 0;
    }
    assert(rc == SQLITE_ROW);

    part = strtok(NULL, "/");
    if(part)
      parent = sqlite3_column_int64(stmt, 0);
  }

  id = sqlite3_column_int64(stmt, 0);
  free(_path);
  return id;
}

int regAddKey(const char *path) {
  sqlite3_stmt *stmt;
  int rc;
  int len;
  const char *name;
  char *base;
  sqlite3_int64 parent = 0;
  sqlite3_int64 id;

  len = strlen(path);

  stmt = LOAD(Q_ADDKEY); /* "select * from key where parent = ? and name = ?;" */
  if(stmt == NULL)
    /* errno from LOAD */
    return -1;

  base = strdup(path);
  if(base == NULL) {
    errno = ENOMEM;
    return -1;
  }

  while(len && path[len] != '/')
    len--;
  name = path+len+1;
  base[len] = 0;

  if(strcmp(base, "") == 0)
    parent = 0;
  else if((parent = regGetKey(base)) == 0) {
    if(errno == ENOENT) {
      if(regAddKey(base)) {
        free(base);
        /* errno from regAddKey */
        return -1;
      }
      if((parent = regGetKey(base)) == 0) {
        free(base);
        /* errno from regGetKey */
        return -1;
      }
    }
    else {
      free(base);
      /* errno from regGetKey */
      return -1;
    }
  }

  rc = sqlite3_reset(stmt);
  assert(rc == SQLITE_OK);
  rc = sqlite3_bind_int64(stmt, 1, parent);
  assert(rc == SQLITE_OK);
  rc = sqlite3_bind_text(stmt, 2, name, strlen(name), SQLITE_STATIC);
  assert(rc == SQLITE_OK);

  rc = sqlite3_step(stmt);
  if(rc == SQLITE_DONE) {
    stmt = LOAD(Q_ADDKEY2); /* "insert into key (parent, name, type) values(?, ?, ?);" */
    if(stmt == NULL)
      /* errno from LOAD */
      return -1;

    if(LOAD(Q_ADDKEY3) == NULL)
      /* errno from LOAD */
      return -1;

    rc = sqlite3_reset(stmt);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int64(stmt, 1, parent);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_text(stmt, 2, name, strlen(name), SQLITE_STATIC);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int(stmt, 3, KEY_VOID);
    assert(rc == SQLITE_OK);

    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE) {
      errno = errmap(sqlite3_errcode(db));
      return -1;
    }
    id = sqlite3_last_insert_rowid(db);

    stmt = LOAD(Q_ADDKEY3); /* "update key set parent = ? where rowid = ?;" */
    assert(stmt != NULL);

    rc = sqlite3_reset(stmt);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int64(stmt, 1, parent);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int64(stmt, 2, id);
    assert(rc == SQLITE_OK);

    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_DONE);

    free(base);
    return 0;
  }

  assert(rc == SQLITE_ROW);
  errno = EEXIST;

  free(base);
  return -1;
}

KeyType regGetKeyType(KeyId id) {
  int rc;
  sqlite3_stmt *stmt;
  KeyType type;

  stmt = LOAD(Q_GETKEYTYPE); /* "select type from key where rowid = ?;" */
  if(stmt == NULL)
    /* errno from LOAD */
    return -1;

  rc = sqlite3_reset(stmt);
  assert(rc == SQLITE_OK);
  rc = sqlite3_bind_int64(stmt, 1, id);
  assert(rc == SQLITE_OK);

  rc = sqlite3_step(stmt);
  if(rc == SQLITE_DONE) {
    errno = ENOENT;
    return -1;
  }
  assert(rc == SQLITE_ROW);
  
  type = sqlite3_column_int(stmt, 0);
  if(type < KEY_VOID || type > KEY_RAW) {
    errno = EILSEQ;
    return -1;
  }

  return type;
}

int regDelKey(const char *path) {
  int rc;
  sqlite3_stmt *stmt;
  KeyId id;

  stmt = LOAD(Q_DELKEY); /* "delete from key where rowid = ?;" */
  if(stmt == NULL)
    /* errno from LOAD */
    return -1;

  id = regGetKey(path);
  if(id == 0)
    /* errno from regGetKey */
    return -1;

  rc = sqlite3_reset(stmt);
  assert(rc == SQLITE_OK);
  rc = sqlite3_bind_int64(stmt, 1, id);
  assert(rc == SQLITE_OK);


  rc = sqlite3_step(stmt);
  if(rc != SQLITE_DONE) {
    errno = errmap(sqlite3_errcode(db));
    return -1;
  }

  return 0;
}

int regSetVoid(const char *path) {
  int rc;
  KeyId   id;
  KeyType type;

  id = regGetKey(path);
  if(id == 0) {
    if(errno == ENOENT) {
      if(regAddKey(path))
        /* errno from regAddKey */
        return -1;
      id = regGetKey(path);
      if(id == 0)
        /* errno from regGetKey */
        return -1;
    }
    else
      /* errno from regGetKey */
      return -1;
  }

  type = regGetKeyType(id);
  switch(type) {
    case KEY_NUMBER:
      sprintf(query, "delete from number where parent = %lld;", id);
      rc = sqlite3_exec(db, query, NULL, NULL, NULL);
      assert(rc == SQLITE_OK);
      break;
    case KEY_STRING:
      sprintf(query, "delete from string where parent = %lld;", id);
      rc = sqlite3_exec(db, query, NULL, NULL, NULL);
      assert(rc == SQLITE_OK);
      break;
    case KEY_RAW:
      sprintf(query, "delete from raw where parent = %lld;", id);
      rc = sqlite3_exec(db, query, NULL, NULL, NULL);
      assert(rc == SQLITE_OK);
      break;
    case KEY_VOID:
      return 0;
    default:
      /* errno from regGetKeyType */
      return -1;
  }

  sprintf(query, "update key set type = %d where rowid = %lld;", KEY_VOID, id);
  rc = sqlite3_exec(db, query, NULL, NULL, NULL);
  assert(rc == SQLITE_OK);
  (void)rc;

  return 0;
}

int regSetNumber(const char *path, uint64_t value) {
  int rc;
  KeyId   id;
  KeyType type;
  sqlite3_stmt *stmt;

  id = regGetKey(path);
  if(id == 0) {
    if(errno == ENOENT) {
      if(regAddKey(path))
        /* errno from regAddKey */
        return -1;
      id = regGetKey(path);
      if(id == 0)
        /* errno from regGetKey */
        return -1;
    }
    else
      /* errno from regGetKey */
      return -1;
  }

  type = regGetKeyType(id);
  if(type == -1)
    /* errno from regGetKeyType */
    return -1;
  if(type == KEY_NUMBER) {
    stmt = LOAD(Q_SETNUMBER); /* "update number set value = ? where parent = ?;" */
    if(stmt == NULL)
      /* errno from LOAD */
      return -1;

    rc = sqlite3_reset(stmt);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int64(stmt, 1, value);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int64(stmt, 2, id);
    assert(rc == SQLITE_OK);

    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE) {
      errno = errmap(sqlite3_errcode(db));
      return -1;
    }
  }
  else if(type != KEY_VOID && regSetVoid(path))
    /* errno from regSetVoid */
    return -1;
  else {
    sprintf(query,
      "insert into number (parent, value) values(%lld, %lld); update key set type = %d where rowid = %lld;",
      id, value,
      KEY_NUMBER, id);
    rc = sqlite3_exec(db, query, NULL, NULL, NULL);
    if(rc != SQLITE_OK) {
      errno = errmap(sqlite3_errcode(db));
      return -1;
    }
  }

  return 0;
}

int regSetString(const char *path, const char *value) {
  int rc;
  KeyId   id;
  KeyType type;
  sqlite3_stmt *stmt;

  id = regGetKey(path);
  if(id == 0) {
    if(errno == ENOENT) {
      if(regAddKey(path))
        /* errno from regAddKey */
        return -1;
      id = regGetKey(path);
      if(id == 0)
        /* errno from regGetKey */
        return -1;
    }
    else
      /* errno from regGetKey */
      return -1;
  }

  type = regGetKeyType(id);
  if(type == -1)
    /* errno from regGetKeyType */
    return -1;
  if(type == KEY_STRING) {
    stmt = LOAD(Q_SETSTRING); /* "update string set value = ? where parent = ?;" */
    if(stmt == NULL)
      /* errno from LOAD */
      return -1;

    rc = sqlite3_reset(stmt);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_text(stmt, 1, value, strlen(value), SQLITE_STATIC);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int64(stmt, 2, id);
    assert(rc == SQLITE_OK);

    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE) {
      errno = errmap(sqlite3_errcode(db));
      return -1;
    }
  }
  else if(type != KEY_VOID && regSetVoid(path))
    /* errno from regSetVoid */
    return -1;
  else {
    sqlite3_snprintf(sizeof(query), query,
      "insert into string (parent, value) values(%lld, %Q); update key set type = %d where rowid = %lld;",
      id, value,
      KEY_STRING, id);
    rc = sqlite3_exec(db, query, NULL, NULL, NULL);
    if(rc != SQLITE_OK) {
      errno = errmap(sqlite3_errcode(db));
      return -1;
    }
  }

  return 0;
}

int regSetRaw(const char *path, const void *value, size_t length) {
  int rc;
  KeyId   id;
  KeyType type;
  sqlite3_stmt *stmt;

  stmt = LOAD(Q_SETRAW); /* "update raw set value = ? where parent = ?;" */
  if(stmt == NULL)
    /* errno from LOAD */
    return -1;

  id = regGetKey(path);
  if(id == 0) {
    if(errno == ENOENT) {
      if(regAddKey(path))
        /* errno from regAddKey */
        return -1;
      id = regGetKey(path);
      if(id == 0)
        /* errno from regGetKey */
        return -1;
    }
    else
      /* errno from regGetKey */
      return -1;
  }

  type = regGetKeyType(id);
  if(type == -1)
    /* errno from regGetKeyType */
    return -1;
  if(type == KEY_RAW) {
    rc = sqlite3_reset(stmt);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_blob(stmt, 1, value, length, SQLITE_STATIC);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int64(stmt, 2, id) != SQLITE_OK;
    assert(rc == SQLITE_OK);

    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_DONE);
  }
  else if(type != KEY_VOID && regSetVoid(path))
    /* errno from regSetVoid */
    return -1;
  else {
    stmt = LOAD(Q_SETRAW2); /* "insert into raw (parent, value) values(?, ?);" */
    if(stmt == NULL)
      /* errno from LOAD */
      return -1;

    if(LOAD(Q_SETRAW3) == NULL)
      /* errno from LOAD */
      return -1;

    rc = sqlite3_reset(stmt);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int64(stmt, 1, id);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_blob(stmt, 2, value, length, SQLITE_STATIC);
    assert(rc == SQLITE_OK);

    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE) {
      errno = errmap(sqlite3_errcode(db));
      return -1;
    }

    stmt = LOAD(Q_SETRAW3); /* "update key set type = ? where rowid = ?;" */
    assert(stmt != NULL);

    rc = sqlite3_reset(stmt);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int(stmt, 1, KEY_RAW);
    assert(rc == SQLITE_OK);
    rc = sqlite3_bind_int64(stmt, 2, id);
    assert(rc == SQLITE_OK);

    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE) {
      errno = errmap(sqlite3_errcode(db));
      return -1;
    }
  }

  return 0;
}

KeyPair* regGetKeyPair(const char *name) {
  sqlite3_int64 id;
  KeyPair *key;
  sqlite3_stmt *stmt;
  int rc;
  (void)rc;

  key = malloc(sizeof(KeyPair));
  if(key == NULL) {
    errno = ENOMEM;
    return NULL;
  }

  key->name = strdup(name);
  if(key->name == NULL) {
    errno = ENOMEM;
    free(key);
    return NULL;
  }

  id = regGetKey(name);
  if(id == 0) {
    errno = ENOENT;
    goto err;
  }

  key->type = regGetKeyType(id);

  switch(key->type) {
    case KEY_VOID:
      break;

    case KEY_NUMBER:
      stmt = LOAD(Q_GETKPNUM); /* "select value from number where parent = ?;" */
      if(stmt == NULL)
        goto err;

      rc = sqlite3_reset(stmt);
      assert(rc == SQLITE_OK);
      rc = sqlite3_bind_int64(stmt, 1, id);
      assert(rc == SQLITE_OK);

      rc = sqlite3_step(stmt);
      assert(rc == SQLITE_ROW);
      key->number = sqlite3_column_int64(stmt, 0);
      key->length = sizeof(key->number);

      break;

    case KEY_STRING:
      stmt = LOAD(Q_GETKPSTR); /* "select value from string where parent = ?;" */
      if(stmt == NULL)
        goto err;

      rc = sqlite3_reset(stmt);
      assert(rc == SQLITE_OK);
      rc = sqlite3_bind_int64(stmt, 1, id);
      assert(rc == SQLITE_OK);

      rc = sqlite3_step(stmt);
      assert(rc == SQLITE_ROW);
      key->string = strdup((char*)sqlite3_column_text(stmt, 0));

      if(key->string == NULL) {
        errno = ENOMEM;
        goto err;
      }
      key->length = strlen(key->string)+1;

      break;

    case KEY_RAW:
      stmt = LOAD(Q_GETKPRAW); /* "select value from raw    where parent = ?;" */
      if(stmt == NULL)
        goto err;

      rc = sqlite3_reset(stmt);
      assert(rc == SQLITE_OK);
      rc = sqlite3_bind_int64(stmt, 1, id);
      assert(rc == SQLITE_OK);

      rc = sqlite3_step(stmt);
      assert(rc == SQLITE_ROW);
      key->length = sqlite3_column_bytes(stmt, 0);

      key->raw = malloc(key->length);
      if(key->raw == NULL) {
        errno = ENOMEM;
        goto err;
      }
      memcpy(key->raw, sqlite3_column_blob(stmt, 0), key->length);

      break;

    default:
      /* errno from regGetKeyType */
      goto err;
  }

  return key;

err:
  free(key->name);
  free(key);
  return NULL;
}

int regFreeKeyPair(KeyPair *kp) {
  if(kp) {
    switch(kp->type) {
      case KEY_VOID:
      case KEY_NUMBER:
        break;
      case KEY_STRING:
        free(kp->string);
        break;
      case KEY_RAW:
        free(kp->raw);
        break;
      default:
        errno = EINVAL;
        return -1;
    }
    free(kp->name);
    free(kp);
    return 0;
  }

  errno = EINVAL;
  return -1;
}

