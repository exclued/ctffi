/*
 * Test library with CTF debug information
 * This library will be used to test the CTF-FFI bridge
 */

#include <stdio.h>
#include <stdlib.h>

int add_numbers(int a, int b) { return a + b; }

double compute(double x, float y, int z) { return x * y + z; }

char* get_message(void) { return "Hello from CTF!"; }

typedef struct { int x; int y; } Point2D;

int point_distance(Point2D p1, Point2D p2) {
    int dx = p1.x - p2.x;
    int dy = p1.y - p2.y;
    return dx * dx + dy * dy;
}

Point2D create_point(int x, int y) {
    Point2D p = { x, y };
    return p;
}

typedef struct { Point2D origin; double scale; } ScaledPoint;

ScaledPoint scale_point(ScaledPoint point) {
    point.origin.x = (int)(point.origin.x * point.scale);
    point.origin.y = (int)(point.origin.y * point.scale);
    return point;
}

typedef union { int i; double d; } Number;

int union_int(Number number) { return number.i; }

void print_status(void) { printf("Status: OK\n"); }

int sum_array(int *arr, int size) {
    int sum = 0;
    for (int i = 0; i < size; i++) sum += arr[i];
    return sum;
}

typedef int (*operation_t)(int, int);

int apply_operation(int a, int b, operation_t op) {
    return op ? op(a, b) : 0;
}

int test_function_signature(void) { return 42; }

#ifdef BUILD_TEST_MAIN
int main(void) {
    Point2D p1 = { 0, 0 }, p2 = { 3, 4 };
    printf("point_distance = %d\n", point_distance(p1, p2));
    Point2D p3 = create_point(10, 20);
    printf("create_point = (%d, %d)\n", p3.x, p3.y);
    ScaledPoint sp = { { 2, 3 }, 2.0 };
    sp = scale_point(sp);
    printf("scale_point = (%d, %d)\n", sp.origin.x, sp.origin.y);
    Number n = { .i = 1234 };
    printf("union_int = %d\n", union_int(n));
    return 0;
}
#endif
