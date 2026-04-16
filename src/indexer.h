#ifndef _NIX_RUN_INDEXER_H
#define _NIX_RUN_INDEXER_H

#include <stddef.h>
#include <time.h>

typedef struct {
    char* name;
    char* description;
} NixPackage;

void NixPackage_free(NixPackage*);

typedef struct {
    NixPackage* packages;
    size_t size;
    size_t capacity;
} NixPackages;

void NixPackages_free(NixPackages*);
NixPackage* NixPackages_at(NixPackages const*, size_t index);
void NixPackages_add(NixPackages*, NixPackage const*);

typedef struct {
    time_t last_updated;
    NixPackages packages;
} Index;

Index Index_load();
void Index_free(Index*);

#endif
