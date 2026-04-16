#include "indexer.h"

#include "constants.h"

#include <glib.h>
#include <stdlib.h>
#include <yyjson.h>

#define INITIAL_CAPACITY 16
#define DAY_IN_SECONDS (24 * 60 * 60)

void NixPackage_free(NixPackage* pkg) {
    free(pkg->name);
    free(pkg->description);
    pkg->name = NULL;
    pkg->description = NULL;
}

void NixPackages_free(NixPackages* pkgs) {
    for (size_t i = 0; i < pkgs->size; ++i) {
        NixPackage_free(&pkgs->packages[i]);
    }
    free(pkgs->packages);
    pkgs->packages = NULL;
    pkgs->size = 0;
    pkgs->capacity = 0;
}

NixPackage* NixPackages_at(NixPackages const* pkgs, size_t index) {
    return &pkgs->packages[index];
}

void NixPackages_add(NixPackages* pkgs, NixPackage const* pkg) {
    if (pkgs->size >= pkgs->capacity) {
        pkgs->capacity = pkgs->capacity == 0 ? INITIAL_CAPACITY : pkgs->capacity + (pkgs->capacity >> 1);
        pkgs->packages = realloc(pkgs->packages, pkgs->capacity * sizeof(NixPackage));
    }
    pkgs->packages[pkgs->size++] = *pkg;
}

// FIXME:Convert all the g_asserts to proper error handling and return an optional Index instead

static char* cache_path = NULL;
char* get_cache_path() {
    if (cache_path == NULL) {
        char* cache_dir = g_build_filename(g_get_user_cache_dir(), "rofi-nix-run", NULL);
        g_mkdir_with_parents(cache_dir, 0700);
        cache_path = g_build_filename(cache_dir, "index.json", NULL);
        g_free(cache_dir);
    }
    return cache_path;
}

static NixPackages fetch_packages() {
    GPid nix_search_pid;
    FILE* nix_search_stdout;
    {
        GError* error = NULL;
        int nix_search_stdout_fd;
        g_spawn_async_with_pipes(
            NULL, (char*[]) { NIX_BINARY, "search", "nixpkgs", "^", "--json", "--no-pretty", NULL }, NULL,
            G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
            NULL,
            NULL,
            &nix_search_pid,
            NULL,
            &nix_search_stdout_fd,
            NULL,
            &error
        );
        g_assert(error == NULL);
        nix_search_stdout = fdopen(nix_search_stdout_fd, "r");
    }

    NixPackages packages = { 0 };

    yyjson_doc* nix_search_json = yyjson_read_fp(nix_search_stdout, YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS, NULL, NULL);
    g_assert(nix_search_json != NULL);
    yyjson_val* root = yyjson_doc_get_root(nix_search_json);
    g_assert(yyjson_is_obj(root));
    yyjson_obj_iter iterator = yyjson_obj_iter_with(root);
    yyjson_val* name;
    yyjson_val* value;
    while ((name = yyjson_obj_iter_next(&iterator))) {
        value = yyjson_obj_iter_get_val(name);
        g_assert(yyjson_is_obj(value));
        yyjson_val* description = yyjson_obj_get(value, "description");
        g_assert(yyjson_is_str(description));

        char const* raw_name = yyjson_get_str(name);
        char const* final_name = raw_name;
        char const* first_dot = strchr(raw_name, '.');
        if (first_dot != NULL) {
            char const* second_dot = strchr(first_dot + 1, '.');
            if (second_dot != NULL) {
                final_name = second_dot + 1;
            }
        }

        NixPackage package = {
            .name = g_strdup(final_name),
            .description = g_strdup(yyjson_get_str(description))
        };
        NixPackages_add(&packages, &package);
    }

    return packages;
}

static void save_index(Index const* index) {
    char* path = get_cache_path();
    yyjson_mut_doc* index_json = yyjson_mut_doc_new(NULL);
    yyjson_mut_val* root = yyjson_mut_obj(index_json);
    yyjson_mut_doc_set_root(index_json, root);

    yyjson_mut_val* packages_val = yyjson_mut_arr(index_json);
    for (size_t i = 0; i < index->packages.size; ++i) {
        NixPackage const* pkg = NixPackages_at(&index->packages, i);
        yyjson_mut_val* package_val = yyjson_mut_obj(index_json);
        yyjson_mut_obj_add_str(index_json, package_val, "name", pkg->name);
        yyjson_mut_obj_add_str(index_json, package_val, "description", pkg->description);
        yyjson_mut_arr_add_val(packages_val, package_val);
    }

    yyjson_mut_obj_add_uint(index_json, root, "last_updated", (uint64_t) index->last_updated);
    yyjson_mut_obj_add_val(index_json, root, "packages", packages_val);

    yyjson_mut_write_file(path, index_json, YYJSON_WRITE_NOFLAG, NULL, NULL);
    yyjson_mut_doc_free(index_json);
}

Index Index_load() {
    char* path = get_cache_path();
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        Index index = {
            .last_updated = time(NULL),
            .packages = fetch_packages()
        };

        save_index(&index);

        return index;
    }

    yyjson_doc* index_json = yyjson_read_file(path, YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS, NULL, NULL);
    g_assert(index_json != NULL);
    yyjson_val* root = yyjson_doc_get_root(index_json);
    g_assert(yyjson_is_obj(root));
    yyjson_val* last_updated_val = yyjson_obj_get(root, "last_updated");
    g_assert(yyjson_is_uint(last_updated_val));
    time_t last_updated = (time_t) yyjson_get_uint(last_updated_val);

    if (time(NULL) - last_updated >= DAY_IN_SECONDS) {
        yyjson_doc_free(index_json);
        Index index = {
            .last_updated = time(NULL),
            .packages = fetch_packages()
        };

        save_index(&index);

        return index;
    }

    Index index = { 0 };
    index.last_updated = last_updated;

    yyjson_val* packages_val = yyjson_obj_get(root, "packages");
    g_assert(yyjson_is_arr(packages_val));

    yyjson_val* package_val;
    yyjson_arr_iter iter = yyjson_arr_iter_with(packages_val);
    while ((package_val = yyjson_arr_iter_next(&iter))) {
        g_assert(yyjson_is_obj(package_val));
        yyjson_val* name_val = yyjson_obj_get(package_val, "name");
        g_assert(yyjson_is_str(name_val));
        yyjson_val* description_val = yyjson_obj_get(package_val, "description");
        g_assert(yyjson_is_str(description_val));
        NixPackage package = {
            .name = g_strdup(yyjson_get_str(name_val)),
            .description = g_strdup(yyjson_get_str(description_val))
        };
        NixPackages_add(&index.packages, &package);
    }

    yyjson_doc_free(index_json);
    return index;
}

void Index_free(Index* index) {
    NixPackages_free(&index->packages);
    index->last_updated = 0;
}
