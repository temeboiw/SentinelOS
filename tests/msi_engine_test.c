#include <stdio.h>
#include <string.h>

/* Keep msiexec's freestanding memcpy symbol separate from libc for host tests. */
#define memcpy msi_test_memcpy
#include "../user/msiexec.c"
#undef memcpy

static int expect_int(const char *name, int actual, int expected) {
    if (actual != expected) {
        fprintf(stderr, "%s: got %d, expected %d\n", name, actual, expected);
        return 0;
    }
    return 1;
}
int main(void) {
    int ok = 1;
    char outbuf[128];

    ok &= expect_int("empty condition", msi_condition_eval(""), 1);
    ok &= expect_int("TRUE condition", msi_condition_eval("TRUE"), 1);
    ok &= expect_int("parenthesized TRUE", msi_condition_eval("(TRUE)"), 1);
    ok &= expect_int("FALSE condition", msi_condition_eval("FALSE"), 0);
    ok &= expect_int("Installed is false in fresh install", msi_condition_eval("Installed"), 0);
    ok &= expect_int("NOT Installed", msi_condition_eval("NOT Installed"), 1);
    ok &= expect_int("NOT FALSE", msi_condition_eval("NOT FALSE"), 1);
    ok &= expect_int("unsupported numeric comparison", msi_condition_eval("VersionNT >= 601"), -1);
    ok &= expect_int("unsupported boolean operator", msi_condition_eval("A OR B"), -1);

    ok &= expect_int("InstallFiles supported", msi_action_supported("InstallFiles"), 1);
    ok &= expect_int("InstallInitialize supported", msi_action_supported("InstallInitialize"), 1);
    ok &= expect_int("registration action refused", msi_action_supported("RegisterProduct"), 0);
    ok &= expect_int("unknown action refused", msi_action_supported("RunArbitraryCode"), 0);
    ok &= expect_int("custom action refused without a recognized table row",
                     msi_custom_action_kind("RunArbitraryCode"), 0);

    ok &= expect_int("literal formatting", msi_format_value("safe literal", outbuf, sizeof outbuf), 1);
    ok &= expect_int("literal text preserved", same_name(outbuf, "safe literal"), 1);
    ok &= expect_int("malformed property reference rejected",
                     msi_format_value("prefix [MISSING", outbuf, sizeof outbuf), 0);

    if (!ok) return 1;
    puts("MSI engine tests passed: supported conditions/actions and custom-action refusal.");
    return 0;
}
