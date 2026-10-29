#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../user/temp_path.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main(void) {
    char narrow[16] = "untouched";
    uint16_t wide[16] = {0};
    uint32_t error = 99;

    CHECK(K32GetTempPathA(9, narrow, &error) == 8);
    CHECK(strcmp(narrow, "C:\\TEMP\\") == 0 && error == 0);
    CHECK(K32GetTempPathA(8, narrow, &error) == 9);
    CHECK(narrow[0] == 0 && error == 122);
    narrow[0] = 'x';
    CHECK(K32GetTempPathA(1, narrow, &error) == 9);
    CHECK(narrow[0] == 0 && error == 122);
    CHECK(K32GetTempPathA(0, narrow, &error) == 9);
    CHECK(error == 122);
    CHECK(K32GetTempPathA(0, NULL, &error) == 9 && error == 122);

    CHECK(K32GetTempPathW(9, wide, &error) == 8);
    CHECK(wide[0] == 'C' && wide[1] == ':' && wide[2] == '\\' &&
          wide[3] == 'T' && wide[4] == 'E' && wide[5] == 'M' &&
          wide[6] == 'P' && wide[7] == '\\' && wide[8] == 0 && error == 0);
    CHECK(K32GetTempPathW(8, wide, &error) == 9);
    CHECK(wide[0] == 0 && error == 122);
    CHECK(K32GetTempPathW(0, NULL, &error) == 9 && error == 122);

    puts("kernel32 GetTempPathA/W buffer and return semantics: PASS");
    return 0;
}
