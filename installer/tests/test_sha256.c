#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sha256.h"

static int expect(const uint8_t *data, uint64_t size, const char *want) {
    static const char hex[] = "0123456789abcdef";
    uint8_t digest[32];
    char got[65];
    sentinel_sha256(data, size, digest);
    for (unsigned i=0;i<32;i++) {
        got[i*2]=hex[digest[i]>>4];
        got[i*2+1]=hex[digest[i]&15];
    }
    got[64]=0;
    if (strcmp(got,want)!=0) {
        fprintf(stderr,"SHA-256 mismatch: got %s expected %s\n",got,want);
        return 0;
    }
    return 1;
}

int main(void) {
    static const uint8_t empty[] = "";
    static const uint8_t abc[] = "abc";
    if (!expect(empty,0,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")) return 1;
    if (!expect(abc,3,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) return 1;
    puts("PASS: SHA-256 empty and abc vectors");
    return 0;
}
