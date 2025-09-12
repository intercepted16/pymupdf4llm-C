#include <stdio.h>

int main(int charc, char *argv[]) {
    if (charc <= 2) {
        printf("Usage: script num1 num2 num3 ...\n");
        return 1;
    }
    int sum = 0;
    for (int i = 1; i < charc; i++) {
        int num;
        if (sscanf(argv[i], "%d", &num) != 1) {
            printf("Invalid number: %s\n", argv[i]);
            return 1;
        }
        printf("Number %d: %d\n", i, num);
        sum += num;
    }
    printf("Sum: %d\n", sum);
}