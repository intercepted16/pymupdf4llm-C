/*
 * Test program for improved table detection
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "improved_table_detection.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <pdf_path> <page_number>\n", argv[0]);
        return 1;
    }
    
    const char *pdf_path = argv[1];
    int page_number = atoi(argv[2]);
    
    printf("Testing table detection on %s, page %d\n", pdf_path, page_number);
    
    // Test original detection
    clock_t start = clock();
    int original_result = original_page_has_table(pdf_path, page_number);
    clock_t end = clock();
    double original_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    // Test improved detection
    start = clock();
    int improved_result = improved_page_has_table(pdf_path, page_number);
    end = clock();
    double improved_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    printf("\nResults:\n");
    printf("Original detection: %s (took %.3f seconds)\n", 
           original_result ? "TABLE FOUND" : "NO TABLE", original_time);
    printf("Improved detection: %s (took %.3f seconds)\n", 
           improved_result ? "TABLE FOUND" : "NO TABLE", improved_time);
    
    if (original_result != improved_result) {
        printf("\n*** DIFFERENT RESULTS ***\n");
        printf("This indicates the improved algorithm may be more accurate!\n");
    }
    
    return 0;
}