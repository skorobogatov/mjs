/*
 * Copyright (c) 2014-2017 Cesanta Software Limited
 * All rights reserved
 */

#ifdef MJS_MAIN

// #include <dlfcn.h>

#include "mjs_core.h"
#include "mjs_exec.h"
#include "mjs_internal.h"
#include "mjs_primitive.h"
#include "mjs_util.h"

int main(int argc, char *argv[]) {
  if (argc == 1) {
    printf("mJS (c) Cesanta, built: " __DATE__ "\n");
    printf("USAGE:\n");
    printf("  %s [-l level] [-c|-r|-j] file...\n", argv[0]);
    printf("  %s [-l level] -e string\n", argv[0]);
    printf("OPTIONS:\n");
    printf("  -l level     - Set debug level, from 0 to 5\n");
    printf("  -c           - Compile JavaScript files to .jsc files\n");
    printf("  -r           - Run bytecode from .jsc files\n");
    printf("  -j           - Enable code precompiling to .jsc files\n");
    printf("  -e string    - Execute JavaScript expression\n");
    return EXIT_SUCCESS;
  }

  /* Used args mask. */
  int *mask = calloc(argc, sizeof(int));

  /* Determine and set debug level. */
  int i, debug_level = -1;
  char *arg;
  for (i = 1; i < argc; i++) {
    arg = argv[i];
    if (arg[0] == '-' && arg[1] == 'l' && arg[2] == 0) {
      mask[i] = 1;

      if (i+1 == argc) {
        printf("debug level number must follow '-l' option\n");
        goto failure;
      }

      char *s = argv[++i];
      char *endptr;
      int level = (int)strtol(s, &endptr, 10);
      if (level < 0 || level > 5 || endptr == s || *endptr != 0) {
        printf("invalid debug level '%s'\n", s);
        goto failure;
      }

      mask[i] = 1;

      if (level > debug_level) {
        debug_level = level;
      }
    }
  }

  if (debug_level != -1) {
    cs_log_set_level(debug_level);
  }

  /* Search for '-e' option. */
  char *expr = NULL;
  for (i = 1; i < argc; i++) {
    if (mask[i]) {
      continue;
    }

    arg = argv[i];
    if (arg[0] == '-' && arg[1] == 'e' && arg[2] == 0) {
      mask[i] = 1;

      if (i+1 == argc || mask[i+1]) {
        printf("JavaScript expression must follow '-e' option\n");
        goto failure;
      }

      if (expr) {
        printf("multiple '-e' options not allowed\n");
        goto failure;
      }

      expr = argv[++i];
      mask[i] = 1;
    }
  }

  struct mjs *mjs;
  mjs_val_t res = MJS_UNDEFINED;
  mjs_err_t err = MJS_OK;

  if (expr) {
    /* If expression is present, check for unused arguments first. */
    for (i = 1; i < argc; i++) {
      if (mask[i] == 0) {
        printf("invalid argument '%s'\n", argv[i]);
        goto failure;
      }
    }
    free(mask);

    /* Execute expression. */
    mjs = mjs_create();
    err = mjs_exec(mjs, expr, &res);
  } else {
    /* Search for '-c', '-r' and '-j' options. */
    int compile = 0, run_jsc = 0, precompile = 0;
    for (i = 1; i < argc; i++) {
      if (mask[i]) {
        continue;
      }

      arg = argv[i];
      if (arg[0] == '-' && arg[2] == 0) {
        mask[i] = 1;

        switch (arg[1]) {
        case 'c':
          if (compile) {
            printf("multiple '-c' options not allowed\n");
            goto failure;
          }
          compile = 1;
          break;
        case 'r':
          if (run_jsc) {
            printf("multiple '-r' options not allowed\n");
            goto failure;
          }
          run_jsc = 1;
          break;
        case 'j':
          if (precompile) {
            printf("multiple '-j' options not allowed\n");
            goto failure;
          }
          precompile = 1;
          break;
        case 'f':
          /* Obsolete '-f' option will be processed further! */
          mask[i] = 0;
          break;
        default:
          printf("invalid option '%s'\n", arg);
          goto failure;
        }
      }
    }

    /* Determine conflicts. */
    if (compile && run_jsc) {
      printf("conflicting options '-c' and '-r'\n");
      goto failure;
    }
    if (compile && precompile) {
      printf("conflicting options '-c' and '-j'\n");
      goto failure;
    }
    if (run_jsc && precompile) {
      printf("conflicting options '-r' and '-j'\n");
      goto failure;
    }

    /* Count files and check extensions of their names.*/
    const char *jscext = ".jsc";
    const char *ext = run_jsc ? jscext : ".js";
    int count = 0;
    for (i = 1; i < argc; i++) {
      if (mask[i]) {
        continue;
      }

      arg = argv[i];
      if (arg[0] == '-' && arg[1] == 'f' && arg[2] == 0) {
        /* Process now obsolete '-f' option. */
        mask[i] = 1;
        if (i+1 == argc || mask[i+1]) {
          printf("file name must follow obsolete '-f' option\n");
          goto failure;
        }
      } else {
        int basename_len = (int)strlen(arg) - strlen(ext);
        if (basename_len <= 0 || strcmp(arg + basename_len, ext) != 0) {
          printf("file name %s must have %s extension\n", arg, ext);
          goto failure;
        }

        count++;
      }
    }

    if (count == 0) {
      printf("no input files\n");
      goto failure;
    }

    /* Process files. */
    mjs = mjs_create();
    if (precompile) {
      mjs_set_generate_jsc(mjs, 1);
    }

    for (i = 1; i < argc && err == MJS_OK; i++) {
      if (mask[i]) {
        continue;
      }

      arg = argv[i];
      if (compile) {
        err = mjs_load_file(mjs, arg);
        if (err == MJS_OK) {
          int basename_len = (int)strlen(arg) - strlen(ext);
          char *filename_jsc = (char*)malloc(basename_len + strlen(jscext) + 1);
          memcpy(filename_jsc, arg, basename_len);
          strcpy(filename_jsc + basename_len, jscext);
          err = mjs_save_jsc(mjs, filename_jsc);
          free(filename_jsc);
        }
      } else if (run_jsc) {
        err = mjs_exec_jsc(mjs, arg, &res);
      } else {
        err = mjs_exec_file(mjs, arg, &res);
      }
    }

    free(mask);
  }

  /* Print result and destroy context. */
  if (err != MJS_OK) {
    mjs_print_error(mjs, stdout, NULL, 1 /* print_stack_trace */);
  } else if (res != MJS_UNDEFINED) {
    mjs_fprintf(res, mjs, stdout);
    putchar('\n');
  }
  mjs_destroy(mjs);
  return EXIT_SUCCESS;

failure:
  free(mask);
  return EXIT_FAILURE;
}
#endif
