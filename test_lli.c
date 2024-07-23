#include <stdio.h>

void foo() {
    printf("%d\n", *(int*)NULL);  // Crash here
}

void bar() {
    foo();
}

void baz() {
    bar();
}

int main(int argc, char **argv) {
    baz();
}