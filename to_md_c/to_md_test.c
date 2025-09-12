#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
if (argc <= 2) {
    printf("Usage: %s <input.pdf> [output.md]\n", argv[0]);
    return 1;
}
void* handle = dlopen("./get_raw_markdown.so", RTLD_LAZY);
if (!handle) {
    // Handle error, e.g., fprintf(stderr, "%s\n", dlerror());
    fprintf(stderr, "Error: %s\n", dlerror());
    dlclose(handle);
    return 1;
}
typedef int (*to_md_func_t)(const char*, const char*);
to_md_func_t to_md = (to_md_func_t)dlsym(handle, "to_markdown");
if (!to_md) {
    // Handle error, e.g., fprintf(stderr, "%s\n", dlerror());
    fprintf(stderr, "Error: %s\n", dlerror());
    dlclose(handle);
    return 1;
}
int result = to_md(argv[1], argv[2]);
dlclose(handle);
return result;
}