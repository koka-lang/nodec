/* ----------------------------------------------------------------------------
Copyright (c) 2018, Microsoft Research, Daan Leijen
This is free software; you can redistribute it and/or modify it under the
terms of the Apache License, Version 2.0. A copy of the License can be
found in the file "license.txt" at the root of this distribution.
-----------------------------------------------------------------------------*/
#include "nodec.h"
#include "nodec-primitive.h"
#include "nodec-internal.h"
#include <assert.h>

#include "mime-db.h"

static const mime_type_t* mime_from_fname(const char* fname) {
  if (fname == NULL) return NULL;
  // find the extension
  const char* base = strrchr(fname, '/');
  if (base == NULL) base = fname;
  const char* ext = strchr(base, '.');
  if (ext == NULL) ext = fname;
              else ext++;
  // binary search for the extension
  int hi = mime_extensions_len - 1;
  int lo = 0;                    
  while (lo <= hi) {
    int i = lo + ((hi - lo) / 2);
    int cmp = nodec_stricmp(ext, mime_extensions[i].extension);
    if (cmp == 0) { 
      lo = hi = i;  
      break;
    }
    else if (cmp > 0) {
      lo = i + 1;
    }
    else {
      hi = i - 1;
    }
  }
  if (hi < lo) return NULL;
  assert(hi == lo);
  return &mime_types[mime_extensions[lo].index];
}

static const mime_type_t* mime_from_name(const char* name) {
  if (name == NULL) return NULL;
  // binary search for the extension
  int hi = mime_types_len - 1;
  int lo = 0;
  while (lo <= hi) {
    int i = lo + ((hi - lo) / 2);
    int cmp = nodec_stricmp(name, mime_types[i].name);
    if (cmp == 0) {
      lo = hi = i;
      break;
    }
    else if (cmp > 0) {
      lo = i + 1;
    }
    else {
      hi = i - 1;
    }
  }
  if (hi < lo) return NULL;
  assert(hi == lo);
  return &mime_types[lo];
}


const char* nodec_mime_info_from_fname(const char* fname, bool* compressible, const char** charset) {
  const mime_type_t* mime = mime_from_fname(fname);
  if (mime == NULL) {
    if (compressible != NULL) *compressible = false;
    if (charset != NULL) *charset = NULL;
    return NULL;
  }
  else {
    if (compressible != NULL) *compressible = mime->compressible;
    if (charset != NULL) *charset = mime->charset;
    return mime->name;
  }
}

const char* nodec_mime_from_fname(const char* fname) {
  return nodec_mime_info_from_fname(fname, NULL, NULL);
}

void nodec_info_from_mime(const char* mime_type,  const char** preferred_ext, bool* compressible, const char** charset) {
  const mime_type_t* mime = mime_from_name(mime_type);
  if (mime == NULL) {
    if (preferred_ext != NULL) *preferred_ext = NULL;
    if (compressible != NULL) *compressible = false;
    if (charset != NULL) *charset = NULL;
  }
  else {
    if (preferred_ext != NULL) *preferred_ext = mime->extension;
    if (compressible != NULL) *compressible = mime->compressible;
    if (charset != NULL) *charset = mime->charset;    
  }
}

const char* nodec_ext_from_mime(const char* mime_type) {
  const char* ext = NULL;
  nodec_info_from_mime(mime_type, &ext, NULL, NULL);
  return ext;
}