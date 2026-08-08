#include <stddef.h>

typedef struct {
    int values[3];
    char tag;
} IntArrayRecord;

typedef struct {
    unsigned short matrix[2][3];
    long total;
} MatrixRecord;

int sum_array_record(IntArrayRecord value) {
    return value.values[0] + value.values[1] + value.values[2] + value.tag;
}

IntArrayRecord make_array_record(int a, int b, int c, char tag) {
    IntArrayRecord value = {{a, b, c}, tag};
    return value;
}

long sum_matrix_record(MatrixRecord value) {
    return (long)value.matrix[0][0] + value.matrix[0][1] + value.matrix[0][2]
         + value.matrix[1][0] + value.matrix[1][1] + value.matrix[1][2]
         + value.total;
}
