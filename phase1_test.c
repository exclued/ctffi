#include <stdio.h>
#include <stdlib.h>

void *call_function_via_ctf(const char *lib_path, const char *func_name,
                            void **arg_values, size_t nargs);

typedef struct { int x; int y; } Point2D;
typedef struct { Point2D origin; double scale; } ScaledPoint;
typedef union { int i; double d; } Number;

static int test_structs(const char *lib_path) {
    Point2D p1 = { 0, 0 }, p2 = { 3, 4 };
    void *args[2] = { &p1, &p2 };
    int *distance = call_function_via_ctf(lib_path, "point_distance", args, 2);
    if (!distance || *distance != 25) {
        free(distance);
        return 1;
    }
    free(distance);

    ScaledPoint input = { { 2, 3 }, 2.0 };
    void *scale_args[1] = { &input };
    ScaledPoint *scaled = call_function_via_ctf(lib_path, "scale_point",
                                                scale_args, 1);
    if (!scaled || scaled->origin.x != 4 || scaled->origin.y != 6) {
        free(scaled);
        return 1;
    }
    free(scaled);
    return 0;
}

static int test_union(const char *lib_path) {
    Number number = { .i = 1234 };
    void *args[1] = { &number };
    int *result = call_function_via_ctf(lib_path, "union_int", args, 1);
    if (!result || *result != 1234) {
        free(result);
        return 1;
    }
    free(result);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <library.so>\n", argv[0]);
        return 2;
    }

    if (test_structs(argv[1]) != 0 || test_union(argv[1]) != 0) {
        fprintf(stderr, "Phase 1 aggregate tests failed\n");
        return 1;
    }

    puts("Phase 1 aggregate tests passed");
    return 0;
}
