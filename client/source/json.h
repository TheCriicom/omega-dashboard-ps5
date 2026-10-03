// Omega UI — parser JSON minimo.
#pragma once
#include <stddef.h>

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;
typedef struct JVal {
  JType t;
  double n;
  char *s;            // J_STR
  char *key;          // chiave se figlio di un oggetto
  int len;            // figli (array/oggetto)
  struct JVal *child, *next;
} JVal;

JVal *json_parse(const char *src);
void  json_free(JVal *v);
JVal *jget(JVal *o, const char *key);
const char *jstr(JVal *o, const char *key, const char *def);   // key NULL = o stesso
double jnum(JVal *o, const char *key, double def);
int   jbool(JVal *o, const char *key);
int   jlen(JVal *a);
void  jcpy(char *dst, size_t n, JVal *o, const char *key);     // stringa (o numero) → dst
void  json_escape(char *dst, size_t n, const char *src);
#define JFOR(it, arr) for (JVal *it = (arr) ? (arr)->child : NULL; it; it = it->next)
