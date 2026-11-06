#!/usr/bin/awk -f
# Audit dllexport functions: categorize as real implementation vs honest stub
# Fixed: properly handle multi-line declarations
BEGIN {
    in_func = 0
    depth = 0
    func_name = ""
    ret_type = ""
    body = ""
    total = 0
    real_count = 0
    stub_count = 0
    other_count = 0
}
/__attribute__\(\(dllexport\)\)/ {
    # If we were already in a function, close it as "other"
    if (in_func) {
        other_count++
        in_func = 0
        body = ""
        depth = 0
    }
    line = $0
    # Remove __attribute__((dllexport))
    sub(/.*__attribute__\(\(dllexport\)\)\s*/, "", line)
    # Find function name: last word before (
    paren_pos = index(line, "(")
    if (paren_pos > 0) {
        before_paren = substr(line, 1, paren_pos - 1)
        n2 = split(before_paren, parts, /[ \t]+/)
        func_name = parts[n2]
        ret_type = parts[n2 - 1]
    } else {
        # Function name and ( on next lines
        func_name = ""
        ret_type = line
    }
    total++

    # Look for opening brace
    brace_pos = index(line, "{")
    if (brace_pos > 0) {
        in_func = 1
        depth = 1
        body = substr(line, brace_pos + 1)
        # Check if function is complete on this line
        close_pos = index(body, "}")
        if (close_pos > 0) {
            check_and_classify(func_name, ret_type, body)
            in_func = 0
            depth = 0
            body = ""
        }
    } else {
        in_func = 1
        depth = 0
        body = ""
    }
    next
}
in_func {
    body = body $0 "\n"
    n = split($0, chars, "")
    for (i = 1; i <= n; i++) {
        if (chars[i] == "{") depth++
        if (chars[i] == "}") depth--
    }
    if (depth <= 0) {
        check_and_classify(func_name, ret_type, body)
        in_func = 0
        body = ""
        depth = 0
    }
    next
}
END {
    print "Total exports:", total
    print "Real implementations:", real_count
    print "Honest stubs:", stub_count
    print "Other/uncertain:", other_count
}
function check_and_classify(name, ret, body,    stripped, is_real, is_stub) {
    is_real = 0
    is_stub = 1

    # Check for real implementation patterns
    if (body ~ /k32_syscall/ || body ~ /SYS_/ || body ~ /__asm__/ || body ~ /int\s+0x80/ || body ~ /syscall/) {
        is_real = 1
        is_stub = 0
    }

    # Remove (void)x; casts and comments
    stripped = body
    gsub(/\(\s*void\s+\w+\s*\);?/, "", stripped)
    gsub(/\/\/.*$/, "", stripped)
    gsub(/\/\*.*?\*\//, "", stripped)
    gsub(/^\s+|\s+$/, "", stripped)

    # If body has meaningful content beyond just error setting
    if (stripped != "" && stripped != ";" && stripped != "0" && stripped != "1") {
        # Check if it's just error + return
        if (stripped !~ /(ws2_last_error|k32_last_error|SetLastError)\s*=\s*\w+;\s*return/) {
            is_real = 1
            is_stub = 0
        }
    }

    if (is_real) {
        real_count++
        printf "REAL: %s %s\n", ret_type, name
    } else if (is_stub) {
        stub_count++
        printf "STUB: %s %s\n", ret_type, name
    } else {
        other_count++
        printf "OTHER: %s %s\n", ret_type, name
    }
}