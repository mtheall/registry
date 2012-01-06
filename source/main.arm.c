#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "registry.h"

unsigned char rawbuf[4096];

KeyPair adds[] = {
  {
    .name = "/key1",
    .type = KEY_NUMBER,
    .value = {
      .number = 10,
    },
  },
  {
    .name = "/key2",
    .type = KEY_STRING, 
    .value = {
      .string = "Hello world!",
    },
  },
  {
    .name = "/key1/subkey1",
    .type = KEY_VOID,
  },
  {
    .name = "/key2/subkey1",
    .type = KEY_STRING,
    .value = {
      .string = "\"Hello,\" world's greatest!\\(worst maybe)\n",
    },
  },
  { 
    .name = "/key2/subkey1/rawkey",
    .type = KEY_RAW,
    .value = {
      .raw = {
        .length = sizeof(rawbuf),
        .data   = rawbuf,
      },
    },
  },
};

int main() {
  int i, j = 0;
  KeyPair *Key;

while(j < 10) {
  printf("Round %d\n", ++j);
  if(regOpen()) {
    fprintf(stderr, "regOpen: %s\n", strerror(errno));
    return 1;
  }

  if(regInit()) {
    fprintf(stderr, "regClose: %s\n", strerror(errno));
    regClose();
    return 1;
  }

  for(i = 0; i < sizeof(rawbuf); i++)
    rawbuf[i] = rand()%256;

  for(i = 0; i < sizeof(adds)/sizeof(adds[0]); i++) {
    if(regAddKey(adds[i].name))
      fprintf(stderr, "regAddKey('%s'): %s\n", adds[i].name, strerror(errno));
  }
  for(i = 0; i < sizeof(adds)/sizeof(adds[0]); i++) {
    switch(adds[i].type) {
      case KEY_NUMBER:
        regSetNumber(adds[i].name, adds[i].value.number);
        break;
      case KEY_STRING:
        regSetString(adds[i].name, adds[i].value.string);
        break;
      case KEY_RAW:
        regSetRaw(adds[i].name, adds[i].value.raw.data, adds[i].value.raw.length);
        break;
      case KEY_VOID:
        regSetVoid(adds[i].name);
        break;
      default:
        fprintf(stderr, "Invalid type!\n");
        break;
    }
  }

  for(i = 0; i < sizeof(adds)/sizeof(adds[0]); i++) {
    Key = regGetKeyPair(adds[i].name);
    if(Key == NULL)
      fprintf(stderr, "regGetKeyPair: %s\n", strerror(errno));
    else {
      if(Key->type != adds[i].type) {
        fprintf(stderr, "%s: type mismatch\n", adds[i].name);
      }
      else switch(Key->type) {
        case KEY_NUMBER:
          if(Key->value.number != adds[i].value.number)
            fprintf(stderr, "%s: value mismatch\n", adds[i].name);
          break;
        case KEY_STRING:
          if(strcmp(Key->value.string, adds[i].value.string))
            fprintf(stderr, "%s: value mismatch\n", adds[i].name);
          break;
        case KEY_RAW:
          if(memcmp(Key->value.raw.data, adds[i].value.raw.data, Key->value.raw.length))
            fprintf(stderr, "%s: value mismatch\n", adds[i].name);
          break;
        case KEY_VOID:
          break;
      }
      switch(Key->type) {
        case KEY_NUMBER:
        case KEY_VOID:
          break;
        case KEY_STRING:
          free(Key->value.string);
          break;
        case KEY_RAW:
          free(Key->value.raw.data);
          break;
      }
      free(Key->name);
      free(Key);
    }
  }

  if(regDelKey("/key2") != 0)
    fprintf(stderr, "regDelKey: %s\n", strerror(errno));

  if(regClose()) {
    fprintf(stderr, "regClose: %s\n", strerror(errno));
    return 1;
  }
}
  return 0;
}

