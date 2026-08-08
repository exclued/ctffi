#include <stdbool.h>

typedef int scalar_t;
typedef const scalar_t const_scalar_t;

enum Color {
    COLOR_RED = 1,
    COLOR_GREEN = 7,
    COLOR_BLUE = 9
};

typedef int (*operation_t)(int, int);

char accept_char(char value) { return value; }
unsigned char accept_uchar(unsigned char value) { return value; }
short accept_short(short value) { return value; }
unsigned long accept_ulong(unsigned long value) { return value; }
_Bool accept_bool(_Bool value) { return value; }

enum Color enum_value(enum Color value) { return value; }

int accept_typedef(scalar_t value) { return value; }
int accept_const_scalar(const_scalar_t value) { return value; }

int add_numbers(int a, int b) { return a + b; }
int apply_operation(int a, int b, operation_t op) { return op ? op(a, b) : 0; }
