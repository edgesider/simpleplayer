#include <stdio.h>

void test_list();
void test_queue();

void test() {
    test_list();
    test_queue();
}

int main(int argc, char *argv[]) {
    printf("testing...\n");
    test();
    printf("all test passed\n");
    return 0;

    return 0;
}
