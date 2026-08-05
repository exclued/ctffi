/*
 * Test library with CTF debug information
 * This library will be used to test the CTF-FFI bridge
 */

#include <stdio.h>
#include <stdlib.h>

/* Simple function returning int */
int add_numbers(int a, int b) {
    return a + b;
}

/* Function with multiple argument types */
double compute(double x, float y, int z) {
    return x * y + z;
}

/* Function returning pointer */
char* get_message(void) {
    return "Hello from CTF!";
}

/* Function with struct argument */
typedef struct {
    int x;
    int y;
} Point2D;

int point_distance(Point2D p1, Point2D p2) {
    int dx = p1.x - p2.x;
    int dy = p1.y - p2.y;
    return dx * dx + dy * dy;
}

/* Function returning struct */
Point2D create_point(int x, int y) {
    Point2D p = { x, y };
    return p;
}

/* Void function */
void print_status(void) {
    printf("Status: OK\n");
}

/* Function with array (pointer) argument */
int sum_array(int *arr, int size) {
    int sum = 0;
    for (int i = 0; i < size; i++) {
        sum += arr[i];
    }
    return sum;
}

/* Callback-style function pointer parameter */
typedef int (*operation_t)(int, int);

int apply_operation(int a, int b, operation_t op) {
    if (op) {
        return op(a, b);
    }
    return 0;
}

/* Test function for verification */
int test_function_signature(void) {
    return 42;
}

#ifdef BUILD_TEST_MAIN
/* Main function for standalone testing */
int main(void) {
    printf("Test Library Functions:\n");
    printf("add_numbers(5, 3) = %d\n", add_numbers(5, 3));
    printf("compute(2.5, 4.0f, 10) = %f\n", compute(2.5, 4.0f, 10));
    printf("get_message() = %s\n", get_message());
    
    Point2D p1 = { 0, 0 };
    Point2D p2 = { 3, 4 };
    printf("point_distance((0,0), (3,4)) = %d\n", point_distance(p1, p2));
    
    Point2D p3 = create_point(10, 20);
    printf("create_point(10, 20) = (%d, %d)\n", p3.x, p3.y);
    
    print_status();
    
    int arr[] = { 1, 2, 3, 4, 5 };
    printf("sum_array([1,2,3,4,5], 5) = %d\n", sum_array(arr, 5));
    
    printf("test_function_signature() = %d\n", test_function_signature());
    
    return 0;
}
#endif
