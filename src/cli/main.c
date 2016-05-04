
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "linenoise.h"
#include "sjs/sjs.h"


/* generated from src/cli/greet.js */
static const char SJS__CLI_GREET_CODE[] = {
    40, 102, 117, 110, 99, 116, 105, 111, 110, 40, 41, 32, 123, 10, 32, 32,
    32, 32, 112, 114, 105, 110, 116, 40, 39, 83, 107, 111, 111, 107, 117, 109,
    32, 74, 83, 32, 39, 32, 43, 32, 115, 121, 115, 116, 101, 109, 46, 118, 101,
    114, 115, 105, 111, 110, 115, 46, 115, 106, 115, 32, 43, 32, 39, 32, 111,
    110, 32, 39, 32, 43, 32, 115, 121, 115, 116, 101, 109, 46, 112, 108, 97,
    116, 102, 111, 114, 109, 32, 43, 32, 39, 32, 40, 39, 32, 43, 32, 115, 121,
    115, 116, 101, 109, 46, 97, 114, 99, 104, 32, 43, 32, 39, 41, 39, 41, 59,
    10, 32, 32, 32, 32, 112, 114, 105, 110, 116, 40, 39, 91, 68, 117, 107, 116,
    97, 112, 101, 32, 39, 32, 43, 32, 115, 121, 115, 116, 101, 109, 46, 118,
    101, 114, 115, 105, 111, 110, 115, 46, 100, 117, 107, 116, 97, 112, 101,
    32, 43, 32, 39, 32, 40, 39, 32, 43, 32, 115, 121, 115, 116, 101, 109, 46,
    118, 101, 114, 115, 105, 111, 110, 115, 46, 100, 117, 107, 116, 97, 112,
    101, 67, 111, 109, 109, 105, 116, 32, 43, 32, 39, 41, 93, 39, 41, 59, 10,
    125, 41, 40, 41, 59, 10, 0
};

#define SJS_CLI_STDIN_BUF_SIZE    65536

/* exit if user presses Ctrl-C twice */
static int got_sigint = 0;


static int handle_stdin(sjs_vm_t* vm) {
    char *buf;
    size_t bufsz;
    size_t bufoff;
    size_t got;
    int r;

    buf = malloc(SJS_CLI_STDIN_BUF_SIZE);
    if (!buf) {
        goto error;
    }

    bufsz = SJS_CLI_STDIN_BUF_SIZE;
    bufoff = 0;

    /* Read until EOF, avoid fseek/stat because it won't work with stdin. */
    for (;;) {
        size_t avail;

        avail = bufsz - bufoff;
        if (avail < 1024) {
            bufsz = bufsz + SJS_CLI_STDIN_BUF_SIZE;
            buf = realloc(buf, bufsz);
            if (!buf) {
                goto error;
            }
        }

        avail = bufsz - bufoff;
        got = fread((void *) (buf + bufoff), 1, avail, stdin);
        if (got == 0) {
            break;
        }
        bufoff += got;
    }

    r = sjs_vm_eval_code(vm, "stdin", buf, bufoff, NULL, stderr);

    free(buf);
    buf = NULL;

    if (r == DUK_EXEC_SUCCESS) {
        return 0;
    }

error:
    fprintf(stderr, "error executing <stdin>\n");
    fflush(stderr);
    return -1;
}


static int handle_interactive(sjs_vm_t* vm) {
    const char *prompt = "sjs> ";
    duk_context* ctx;
    char* line;
    char history_file[4096];
    int use_history;

    ctx = sjs_vm_get_duk_ctx(vm);
    duk_eval_string_noresult(ctx, SJS__CLI_GREET_CODE);

    /* setup history file
     * TODO: make configurable?
     */
    if (sjs_path_normalize("~", history_file, sizeof(history_file)) == 0) {
        strcat(history_file, "/.sjs_history");
        linenoiseHistorySetMaxLen(1000);
        linenoiseHistoryLoad(history_file);
        use_history = 1;
    } else {
        use_history = 0;
    }

    while((line = linenoise(prompt)) != NULL) {
        /* reset sigint signal state */
        got_sigint = 0;

        if (line[0] != '\0') {
            linenoiseHistoryAdd(line);
        }

        sjs_vm_eval_code(vm, "input", line, strlen(line), stdout, stdout);
        free(line);
    }

    if (use_history) {
        linenoiseHistorySave(history_file);
    }

    return 0;
}


static void handle_sigint(int sig) {
    assert(sig == SIGINT);
    if (got_sigint == 1) {
        exit(0);
    } else {
        got_sigint = 1;
        fprintf(stdout, "\r\n(^C again to exit)\r\n");
        fflush(stdout);
    }
}


int main(int argc, char *argv[]) {
    sjs_vm_t* vm = NULL;
    char* run_file = NULL;
    char* eval_code = NULL;
    int interactive = 0;
    int run_stdin = 0;
    int retval = 0;

    /* Parse options */
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        assert(arg);
        if (strcmp(arg, "-i") == 0) {
            interactive = 1;
        } else if (strcmp(arg, "-h") == 0) {
            goto usage;
        } else if (strcmp(arg, "-e") == 0) {
            if (i == argc - 1) {
                retval = 1;
                goto usage;
            }
            eval_code = argv[i + 1];
            break;
        } else if (strlen(arg) > 1 && arg[0] == '-') {
            retval = 1;
            goto usage;
        } else if (strcmp(arg, "-") == 0) {
            run_stdin = 1;
            break;
        } else {
            run_file = arg;
            break;
        }
    }

    if (!run_file && !eval_code && !run_stdin) {
        interactive = 1;
    }

    /* Create VM */
    vm = sjs_vm_create();
    sjs_vm_setup_args(vm, argc, argv);

    /* run */
    if (run_file) {
        if (sjs_vm_eval_file(vm, run_file, NULL, stderr) != 0) {
            retval = 1;
            goto cleanup;
        }
    } else if (run_stdin) {
        if (handle_stdin(vm) != 0) {
            retval = 1;
            goto cleanup;
        }
    } else if (eval_code) {
        if (sjs_vm_eval_code(vm, "eval", eval_code, strlen(eval_code), NULL, stderr) != 0) {
            retval = 1;
            goto cleanup;
        }
    }

    if (!isatty(STDIN_FILENO)) {
        interactive = 0;
        if (handle_stdin(vm) != 0) {
            retval = 1;
            goto cleanup;
        }
    }

    /* Enter interactive mode */
    if (interactive) {
        /* setup signal handling */
        signal(SIGPIPE, SIG_IGN);
        signal(SIGINT, handle_sigint);

        if (handle_interactive(vm) != 0) {
            retval = 1;
            goto cleanup;
        }
    }

cleanup:
    sjs_vm_destroy(vm);
    vm = NULL;
    return retval;

usage:
    fprintf(stderr, "Usage: sjs [options] [ <code> | <file> | - ]\n"
                    "\n"
                    "   -h         show help text\n"
                    "   -i         enter interactive mode after executing argument file(s) / eval code\n"
                    "   -e CODE    evaluate code\n"
                    "\n"
                    "If <file> is omitted, interactive mode is started automatically.\n");
    fflush(stderr);
    exit(retval);
}
