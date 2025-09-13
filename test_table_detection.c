/*
 * Test program for table detection functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "include/table.h"

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <pdf_path> <page_number>\n", argv[0]);
        return 1;
    }

    const char* pdf_path = argv[1];
    int page_number = atoi(argv[2]);

    time_t start, end;
    time(&start);
    int original_has_table = original_page_has_table(pdf_path, page_number);
    time(&end);

    printf("Original detection took %ld seconds\n", end - start);

    time(&start);
    int improved_has_table = page_has_table(pdf_path, page_number);
    time(&end);

    printf("Improved detection took %ld seconds\n", end - start);

    printf("Original table detection: %s\n",
           original_has_table ? "Table detected" : "No table detected");
    printf("Improved table detection: %s\n",
           improved_has_table ? "Table detected" : "No table detected");

    return 0;
}
