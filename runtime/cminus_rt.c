/*
 * Runtime support for programs compiled by cmc.
 *
 * C- has no I/O statements; spec 3.10 instead says that `input` and `output`
 * behave as if declared in the global environment. The compiler emits calls
 * to them, and this file provides the definitions to link against.
 */

#include <stdio.h>
#include <stdlib.h>

/* Read one integer from standard input. */
int input(void) {
  int value;
  if (scanf("%d", &value) != 1)
    value = 0;
  return value;
}

/* Print one integer, followed by a newline. */
void output(int value) { printf("%d\n", value); }

/*
 * Spec 3.6: a negative subscript is a run-time error that stops the program.
 * The upper bound is deliberately not checked.
 */
_Noreturn void __cminus_index_error(int index) {
  fflush(stdout);
  fprintf(stderr, "runtime error: array index %d is negative\n", index);
  exit(1);
}
