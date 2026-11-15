/* NOVA Installer - SHA-256 host tests. SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <string.h>

#include "nova_sha256.h"

static int checks;
static int failures;

static void expect_hash(const char *label, const char *data, size_t size, const char *expected)
{
    char actual[65];
    nova_sha256_hex(data, size, actual);
    checks++;
    if (strcmp(actual, expected) != 0)
    {
        failures++;
        printf("  FAIL %s\n    expected %s\n    actual   %s\n", label, expected, actual);
    }
}

static void expect_streaming(void)
{
    static char block[1000];
    memset(block, 'a', sizeof(block));

    nova_sha256_t ctx;
    nova_sha256_init(&ctx);
    for (int i = 0; i < 1000; i++)
        nova_sha256_update(&ctx, block, sizeof(block));

    uint8_t digest[32];
    nova_sha256_final(&ctx, digest);

    static const char hex[] = "0123456789abcdef";
    char actual[65];
    for (int i = 0; i < 32; i++)
    {
        actual[i * 2] = hex[digest[i] >> 4];
        actual[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    actual[64] = '\0';

    checks++;
    if (strcmp(actual, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") != 0)
    {
        failures++;
        printf("  FAIL streaming 1,000,000 x 'a'\n    actual %s\n", actual);
    }
}

int main(void)
{
    printf("sha256 tests\n");

    expect_hash("empty", "", 0,
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    expect_hash("abc", "abc", 3,
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    expect_hash("quick brown fox", "The quick brown fox jumps over the lazy dog", 43,
                "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
    expect_hash("two blocks", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
                "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    expect_streaming();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
