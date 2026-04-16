/**
 * rofi-nix-run
 *
 * MIT License
 *
 * Copyright (c) 2026 Davide Carella <carelladavide1@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "constants.h"
#include "indexer.h"

#include <assert.h>
#include <glib.h>
#include <rofi/helper.h>
#include <rofi/mode-private.h>
#include <rofi/mode.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

G_MODULE_EXPORT Mode mode;

typedef struct {
    Index index;
    char* display_format;
    char* message;
} NixRunPrivateData;

// static void remove_message(NixRunPrivateData* data) {
//     g_free(data->message);
//     data->message = NULL;
// }
//
// static void set_message(NixRunPrivateData* data, char const* format, ...) __attribute__((__format__(printf, 2, 3)));
// static void set_message(NixRunPrivateData* data, char const* format, ...) {
//     va_list args;
//     va_start(args, format);
//     g_free(data->message);
//     data->message = g_strdup_vprintf(format, args);
//     va_end(args);
// }

static void build_and_run_package(char const* package_name) {
    GPid nix_build_pid;
    FILE* nix_build_stdout = NULL;
    {
        GError* error = NULL;
        int nix_build_stdout_fd;
        char* tmp = g_strdup_printf(NIX_BINARY " build --no-link nixpkgs#%s", package_name);
        g_spawn_async_with_pipes(
            NULL, (char*[]) { "script", "-qec", tmp, "/dev/null", NULL }, NULL,
            G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
            NULL,
            NULL,
            &nix_build_pid,
            NULL,
            &nix_build_stdout_fd,
            NULL,
            &error
        );
        g_free(tmp);

        if (error != NULL) {
            g_error("Failed to start nix build process: %s\n", error->message);
            g_error_free(error);
            return;
        }

        nix_build_stdout = fdopen(nix_build_stdout_fd, "r");
    }

    GPid zenity_pid;
    FILE* zenity_stdin = NULL;
    {
        GError* error = NULL;
        int zenity_stdin_fd;
        g_spawn_async_with_pipes(
            NULL, (char*[]) { ZENITY_BINARY, "--progress", "--percentage=0", "--title=nix-run", "--text=Building package...", "--auto-close", NULL }, NULL,
            G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
            NULL,
            NULL,
            &zenity_pid,
            &zenity_stdin_fd,
            NULL,
            NULL,
            &error
        );

        if (error != NULL) {
            g_error("Failed to start zenity process: %s\n", error->message);
            g_error_free(error);
            return;
        }

        zenity_stdin = fdopen(zenity_stdin_fd, "w");
    }

    GRegex* ansi_regex = g_regex_new("\x1b\\[[0-9;]*m", 0, 0, NULL);
    GRegex* progress_regex = g_regex_new("\\(([^\\/]+)\\/([^ ]+) .iB\\)", 0, 0, NULL);

    char* line = NULL;
    size_t line_size = 0;
    while (getdelim(&line, &line_size, '\r', nix_build_stdout) != -1) {
        int zenity_status;
        if (waitpid(zenity_pid, &zenity_status, WNOHANG) && WEXITSTATUS(zenity_status) == 0) {
            kill(nix_build_pid, SIGKILL);
            break;
        }

        char* no_ansi_line = g_regex_replace(ansi_regex, line, strlen(line), 0, "", 0, NULL);
        GMatchInfo* match_info;
        if (g_regex_match(progress_regex, no_ansi_line, 0, &match_info)) {
            char* current = g_match_info_fetch(match_info, 1);
            char* total = g_match_info_fetch(match_info, 2);
            double current_value = g_ascii_strtod(current, NULL);
            double total_value = g_ascii_strtod(total, NULL);
            fprintf(zenity_stdin, "%d\n", (int) ((current_value / total_value) * 100));
            fflush(zenity_stdin);
            g_free(current);
            g_free(total);
        }
        g_match_info_free(match_info);
        g_free(no_ansi_line);
    }
    free(line);

    fprintf(zenity_stdin, "100\n");
    fflush(zenity_stdin);

    waitpid(nix_build_pid, NULL, 0);
    waitpid(zenity_pid, NULL, 0);

    fclose(nix_build_stdout);
    fclose(zenity_stdin);

    g_regex_unref(ansi_regex);
    g_regex_unref(progress_regex);
    g_spawn_close_pid(nix_build_pid);
    g_spawn_close_pid(zenity_pid);

    {
        GError* error = NULL;
        char* tmp = g_strdup_printf("nixpkgs#%s", package_name);
        g_spawn_async(
            NULL, (char*[]) { NIX_BINARY, "run", tmp, NULL }, NULL,
            G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
            NULL,
            NULL,
            NULL,
            &error
        );
        g_free(tmp);

        if (error != NULL) {
            g_error("Failed to run nix package: %s\n", error->message);
            g_error_free(error);
        }
    }
}

static int nix_run_init(Mode* mode) {
    if (mode_get_private_data(mode) != NULL) {
        return TRUE;
    }

    NixRunPrivateData* data = g_malloc0(sizeof(NixRunPrivateData));
    mode_set_private_data(mode, data);
    data->display_format = "{name} <span size='small'><i>({description})</i></span>";
    data->index = Index_load();
    return TRUE;
}

static void nix_run_destroy(Mode* mode) {
    NixRunPrivateData* data = mode_get_private_data(mode);
    if (data == NULL) {
        return;
    }

    Index_free(&data->index);
    g_free(data);
    mode_set_private_data(mode, NULL);
}

static unsigned int nix_run_get_num_entries(Mode const* mode) {
    NixRunPrivateData* data = mode_get_private_data(mode);
    assert(data != NULL);
    return (unsigned int) data->index.packages.size;
}

static int nix_run_token_match(Mode const* mode, rofi_int_matcher** tokens, unsigned int index) {
    NixRunPrivateData* data = mode_get_private_data(mode);
    assert(data != NULL);
    return index < data->index.packages.size && helper_token_match(tokens, NixPackages_at(&data->index.packages, index)->name);
}

static char* nix_run_get_display_value(Mode const* mode, unsigned int selected_line, G_GNUC_UNUSED int *state, G_GNUC_UNUSED GList **attr_list, int get_entry) {
    NixRunPrivateData* data = mode_get_private_data(mode);
    assert(data != NULL);
    if (!get_entry) {
        return NULL;
    }

    if (selected_line >= data->index.packages.size) {
        return g_strdup("");
    }

    *state |= MARKUP;

    NixPackage* pkg = NixPackages_at(&data->index.packages, selected_line);
    char* name = g_markup_escape_text(pkg->name, -1);
    char* description = g_markup_escape_text(pkg->description, -1);
    char* formatted = helper_string_replace_if_exists(
        data->display_format,
        "{name}", name,
        "{description}", description,
        NULL
    );
    g_free(name);
    g_free(description);
    return formatted;
}

static ModeMode nix_run_result(Mode* mode, int mretv, G_GNUC_UNUSED char** input, unsigned int selected_line) {
    if (!(mretv & MENU_OK)) {
        return MODE_EXIT;
    }

    NixRunPrivateData* data = mode_get_private_data(mode);
    if (data == NULL || selected_line >= data->index.packages.size) {
        return MODE_EXIT;
    }

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        build_and_run_package(NixPackages_at(&data->index.packages, selected_line)->name);
        exit(0);
    } else if (pid < 0) {
        g_error("Failed to fork background process.");
    }

    return MODE_EXIT;
}

static char* nix_run_get_message(Mode const* mode) {
    NixRunPrivateData* data = mode_get_private_data(mode);
    assert(data != NULL);
    return g_strdup(data->message ? data->message : NULL);
}

Mode mode = {
    .abi_version = ABI_VERSION,
    .name = "nix-run",
    .cfg_name_key = "display-nix-run",
    ._init = nix_run_init,
    ._get_num_entries = nix_run_get_num_entries,
    ._result = nix_run_result,
    ._destroy = nix_run_destroy,
    ._token_match = nix_run_token_match,
    ._get_display_value = nix_run_get_display_value,
    ._get_message = nix_run_get_message,
    ._get_completion = NULL,
    ._preprocess_input = NULL,
    .private_data = NULL,
    .free = NULL
};
