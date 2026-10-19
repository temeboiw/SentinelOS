#include "ntdll_user.h"

typedef unsigned int u32;
typedef int BOOL;

typedef struct {
    u32 hwnd, message;
    u64 wParam;
    i64 lParam;
    u32 time;
    int pt_x, pt_y;
} MSG;

typedef void *(*CreateWindowExA_t)(
    u32, const char *, const char *, u32,
    int, int, int, int,
    void *, void *, void *, void *
);
typedef int (*ShowWindow_t)(void *, int);
typedef int (*GetMessageA_t)(MSG *, void *, u32, u32);
typedef int (*TextOutA_t)(void *, int, int, const char *, int);
typedef int (*InvalidateRect_t)(void *, void *, int);

#define LINE_COUNT 256
#define LINE_SIZE  80
#define CMD_SIZE   96
#define DIR_MAX    16
#define FILE_MAX   32
#define PATH_SIZE  64
#define DATA_SIZE  256

typedef struct {
    char path[PATH_SIZE];
    int used;
} ShellDir;

typedef struct {
    char path[PATH_SIZE];
    char data[DATA_SIZE];
    int used;
} ShellFile;

static void *g_window;
static TextOutA_t g_text;
static InvalidateRect_t g_invalidate;
static char g_lines[LINE_COUNT][LINE_SIZE];
static int g_line_count;
static char g_command[CMD_SIZE];
static int g_command_len;
static ShellDir g_dirs[DIR_MAX];
static ShellFile g_files[FILE_MAX];
static char g_cwd[PATH_SIZE];

static int str_len(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int equals(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int starts_with(const char *s, const char *prefix) {
    int i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static void str_copy(char *dst, const char *src, int cap) {
    int i = 0;
    if (cap <= 0) return;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void trim_right(char *s) {
    int n = str_len(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
}

static void print_line(const char *s) {
    if (g_line_count >= LINE_COUNT) {
        for (int i = 1; i < LINE_COUNT; i++)
            for (int j = 0; j < LINE_SIZE; j++)
                g_lines[i - 1][j] = g_lines[i][j];
        g_line_count = LINE_COUNT - 1;
    }

    str_copy(g_lines[g_line_count], s, LINE_SIZE);
    g_line_count++;
    if (g_text) {
        g_text(g_window, 0, -2, "", 0); /* begin output batch; preserve scroll position */
        for (int n = 0; n < g_line_count; n++)
            g_text(g_window, 0, n, g_lines[n], -1);
    }
    if (g_invalidate) g_invalidate(g_window, 0, 1);
}

static void redraw_prompt(void) {
    char line[LINE_SIZE];
    int n = 0;
    const char *prefix = "C:\\>";
    while (prefix[n] && n < LINE_SIZE - 1) {
        line[n] = prefix[n];
        n++;
    }
    for (int i = 0; g_command[i] && n < LINE_SIZE - 1; i++)
        line[n++] = g_command[i];
    line[n] = 0;

    if (g_text) {
        g_text(g_window, 0, -2, "", 0);
        g_text(g_window, 0, g_line_count, line, -1);
    }
    if (g_invalidate) g_invalidate(g_window, 0, 1);
}

/* Canonicalize a shell path into a root-relative path using backslashes. */
static int normalize_path(const char *input, char *out, int cap) {
    int i = 0, n = 0;
    if (!input || !input[0] || cap < 2) return -1;

    if ((input[0] == 'C' || input[0] == 'c') &&
        input[1] == ':' && (input[2] == '\\' || input[2] == '/'))
        i = 3;
    else if (input[0] == '\\' || input[0] == '/')
        i = 1;

    while (input[i] == '\\' || input[i] == '/') i++;

    while (input[i]) {
        char c = input[i++];
        if (c == '/') c = '\\';
        if (c == '\\') {
            if (n == 0 || out[n - 1] == '\\') continue;
        }
        if (n >= cap - 1) return -1;
        out[n++] = c;
    }
    while (n > 0 && out[n - 1] == '\\') n--;
    out[n] = 0;
    return n ? 0 : -1;
}

static int dir_exists(const char *path) {
    if (!path[0]) return 1; /* C:\ root */
    for (int i = 0; i < DIR_MAX; i++)
        if (g_dirs[i].used && equals(g_dirs[i].path, path))
            return 1;
    return 0;
}

static int file_index(const char *path) {
    for (int i = 0; i < FILE_MAX; i++)
        if (g_files[i].used && equals(g_files[i].path, path))
            return i;
    return -1;
}

static int parent_path(const char *path, char *parent, int cap) {
    int n = str_len(path), slash = -1;
    for (int i = 0; i < n; i++)
        if (path[i] == '\\') slash = i;
    if (slash < 0) {
        parent[0] = 0;
        return 0;
    }
    if (slash >= cap) return -1;
    for (int i = 0; i < slash; i++) parent[i] = path[i];
    parent[slash] = 0;
    return 0;
}

static void command_mkdir(const char *arg) {
    (void)arg;print_line("Mounted ext4 volume is read-only; mkdir is disabled until ext4 writes are implemented.");return;
    char path[PATH_SIZE], parent[PATH_SIZE];
    if (normalize_path(arg, path, sizeof path) != 0) {
        print_line("Usage: mkdir <folder>");
        return;
    }
    if (dir_exists(path) || file_index(path) >= 0) {
        print_line("A file or directory already exists.");
        return;
    }
    if (parent_path(path, parent, sizeof parent) != 0 ||
        !dir_exists(parent)) {
        print_line("Parent directory not found.");
        return;
    }
    for (int i = 0; i < DIR_MAX; i++) {
        if (!g_dirs[i].used) {
            str_copy(g_dirs[i].path, path, PATH_SIZE);
            g_dirs[i].used = 1;
            print_line("Directory created.");
            return;
        }
    }
    print_line("Directory table full.");
}

static void command_echo(const char *arg) {
    (void)arg;print_line("Mounted ext4 volume is read-only; persistent writes are not implemented yet.");return;
    char text[DATA_SIZE], path[PATH_SIZE], parent[PATH_SIZE];
    int i = 0, out = 0, gt = -1;
    while (arg[i]) {
        if (arg[i] == '>' && (i == 0 || arg[i - 1] == ' ')) {
            gt = i;
            break;
        }
        i++;
    }
    if (gt < 0) {
        print_line(arg);
        return;
    }

    while (gt > 0 && arg[gt - 1] == ' ') gt--;
    for (i = 0; i < gt && out < DATA_SIZE - 1; i++)
        text[out++] = arg[i];
    text[out] = 0;

    i = gt;
    while (arg[i] && arg[i] != '>') i++;
    if (arg[i] == '>') i++;
    while (arg[i] == ' ') i++;

    char raw[PATH_SIZE];
    out = 0;
    while (arg[i] && arg[i] != ' ' && out < PATH_SIZE - 1)
        raw[out++] = arg[i++];
    raw[out] = 0;

    if (!raw[0] || normalize_path(raw, path, sizeof path) != 0) {
        print_line("Usage: echo <text> > <path>");
        return;
    }
    if (parent_path(path, parent, sizeof parent) != 0 ||
        !dir_exists(parent)) {
        print_line("Parent directory not found.");
        return;
    }

    int fi = file_index(path);
    if (fi < 0) {
        for (fi = 0; fi < FILE_MAX; fi++)
            if (!g_files[fi].used) break;
        if (fi == FILE_MAX) {
            print_line("File table full.");
            return;
        }
        g_files[fi].used = 1;
        str_copy(g_files[fi].path, path, PATH_SIZE);
    }

    str_copy(g_files[fi].data, text, DATA_SIZE);
    print_line("File created.");
}

static void command_cat(const char *arg) {
    char path[PATH_SIZE],buffer[128],full[PATH_SIZE+4];
    if(normalize_path(arg,path,sizeof path)!=0){print_line("Usage: cat <file>");return;}
    str_copy(full,"C:\\",sizeof full);str_copy(full+str_len(full),path,sizeof full-str_len(full));
    u64 h=NtOpenFile(full);if((i64)h<0||!h){print_line("File not found on mounted disk.");return;}
    for(;;){i64 got=NtReadFile(h,buffer,sizeof(buffer)-1);if(got<=0)break;buffer[got]=0;char line[LINE_SIZE];int n=0;
        for(int i=0;i<got;i++){if(buffer[i]=='\n'||n==LINE_SIZE-1){line[n]=0;print_line(line);n=0;}else if(buffer[i]!='\r')line[n++]=buffer[i];}
        if(n){line[n]=0;print_line(line);}
    }NtCloseFile(h);
}

static void command_dir(void) {
    print_line("Directory of mounted C: volume");
    for(u64 index=0;index<64;index++){char name[64];i64 kind=NtQueryDirectory(index,name);if(!kind)break;char line[LINE_SIZE];str_copy(line,kind==2?"<DIR> ":"       ",sizeof line);str_copy(line+str_len(line),name,sizeof line-str_len(line));print_line(line);}
    return;
    for (int i = 0; i < DIR_MAX; i++) {
        if (g_dirs[i].used) {
            char line[LINE_SIZE];
            str_copy(line, "<DIR> ", sizeof line);
            int n = str_len(line);
            str_copy(line + n, g_dirs[i].path, LINE_SIZE - n);
            print_line(line);
        }
    }
    for (int i = 0; i < FILE_MAX; i++) {
        if (g_files[i].used) {
            char line[LINE_SIZE];
            str_copy(line, "       ", sizeof line);
            int n = str_len(line);
            str_copy(line + n, g_files[i].path, LINE_SIZE - n);
            print_line(line);
        }
    }
}

static char hex_digit(unsigned v) {
    v &= 15u;
    return (char)(v < 10 ? '0' + v : 'A' + v - 10);
}

static void command_ipconfig(void);
static void command_ps(void){
    SENTINEL_PROCESS_INFORMATION e[8];i64 count=NtQueryProcesses(e,sizeof e);
    if(count<0){print_line("ps: process query failed.");return;}print_line("PID       PPID      STATE   IMAGE");
    for(i64 i=0;i<count&&i<8;i++){char line[LINE_SIZE],num[24];int n=0;format_u64(e[i].ProcessId,num,sizeof num);str_copy(line,num,sizeof line);n=str_len(line);
        while(n<10)line[n++]=' ';line[n]=0;format_u64(e[i].ParentProcessId,num,sizeof num);str_copy(line+n,num,sizeof line-n);n=str_len(line);
        while(n<20)line[n++]=' ';line[n]=0;str_copy(line+n,e[i].State==1?"NEW    ":"RUNNING",sizeof line-n);n=str_len(line);str_copy(line+n,e[i].ImageName,sizeof line-n);print_line(line);}
}
static void command_kill(const char*a){u64 pid=0;int d=0;while(*a>='0'&&*a<='9'){pid=pid*10+(*a++-'0');d++;}while(*a==' ')a++;if(!d||*a||!pid){print_line("Usage: kill <pid>");return;}print_line(NtTerminatePid(pid)==0?"Process terminated.":"Cannot terminate that PID.");}
static void command_udpsend(const char*a){
    char t[24];unsigned i=0;while(a[i]&&a[i]!=' '&&i<sizeof t-1){t[i]=a[i];i++;}t[i]=0;while(a[i]==' ')i++;
    u32 ip=0;if(parse_ipv4(t,&ip)){print_line("Usage: udpsend <IPv4> <port> <text>");return;}
    unsigned p=0,d=0;while(a[i]>='0'&&a[i]<='9'){p=p*10+(a[i++]-'0');d++;if(p>65535)break;}while(a[i]==' ')i++;
    if(!d||!p||p>65535||!a[i]){print_line("Usage: udpsend <IPv4> <port> <text>");return;}
    print_line(NtUdpSend(ip,49154,p,a+i,(u32)str_len(a+i))==0?"UDP datagram sent.":"UDP send failed; run dhcp first.");
}
static void command_udplisten(const char*a){
    unsigned p=0,d=0;while(a[d]>='0'&&a[d]<='9'){p=p*10+(a[d]-'0');d++;if(p>65535)break;}
    if(!d||a[d]||!p||p>65535){print_line("Usage: udplisten <port>");return;}
    if(NtUdpBind(p)){print_line("UDP bind failed.");return;}print_line("Waiting for one UDP datagram...");
    char data[512];u32 ip=0;u16 sport=0;int n=NtUdpReceive(data,sizeof(data)-1,&ip,&sport);if(n<0){print_line("UDP receive timed out.");return;}
    data[n]=0;char addr[24],line[LINE_SIZE],num[24];format_ipv4(ip,addr,sizeof addr);format_u64(sport,num,sizeof num);
    str_copy(line,"From ",sizeof line);str_copy(line+str_len(line),addr,sizeof line-str_len(line));str_copy(line+str_len(line),":",sizeof line-str_len(line));str_copy(line+str_len(line),num,sizeof line-str_len(line));print_line(line);print_line(data);
}
static int parse_ipv4(const char *s, u32 *out) {
    u32 value = 0; int parts = 0;
    while (*s) {
        if (parts >= 4) return -1;
        unsigned v = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10u + (unsigned)(*s++ - '0');
            if (v > 255u) return -1;
            digits++;
        }
        if (!digits) return -1;
        value = (value << 8) | v; parts++;
        if (!*s) break;
        if (*s++ != '.') return -1;
        if (!*s) return -1;
    }
    if (parts != 4) return -1;
    *out = value; return 0;
}
static void format_ipv4(u32 ip, char *out, int cap) {
    int n = 0;
    for (int shift = 24; shift >= 0; shift -= 8) {
        unsigned v = (ip >> shift) & 255u;
        char digits[3]; int d = 0;
        if (v >= 100) digits[d++] = (char)('0' + v / 100);
        if (v >= 10) digits[d++] = (char)('0' + (v / 10) % 10);
        digits[d++] = (char)('0' + v % 10);
        for (int i = 0; i < d && n < cap - 1; i++) out[n++] = digits[i];
        if (shift && n < cap - 1) out[n++] = '.';
    }
    out[n] = 0;
}
static void format_u64(u64 value, char *out, int cap) {
    char reverse[20];
    int n = 0, pos = 0;
    if (cap < 2) return;
    do {
        reverse[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && n < (int)sizeof reverse);
    while (n > 0 && pos < cap - 1) out[pos++] = reverse[--n];
    out[pos] = 0;
}

static void command_dhcp(void) {
    print_line("Requesting IPv4 configuration via DHCP...");
    int result = NtNetworkDhcp();
    if (result == 0) print_line("DHCP lease acquired.");
    else print_line("DHCP failed or timed out.");
    SENTINELOS_NET_CONFIG cfg;
    for (unsigned i = 0; i < sizeof cfg; i++)
        ((unsigned char *)&cfg)[i] = 0;
    if (NtQueryNetworkConfig(&cfg) == 0) {
        char number[24], line[LINE_SIZE];
        format_u64(cfg.TxPackets, number, sizeof number);
        str_copy(line, "   TX packets . . . . . : ", sizeof line);
        str_copy(line + str_len(line), number, sizeof line - str_len(line));
        print_line(line);
        format_u64(cfg.RxPackets, number, sizeof number);
        str_copy(line, "   RX packets . . . . . : ", sizeof line);
        str_copy(line + str_len(line), number, sizeof line - str_len(line));
        print_line(line);
#define NET_DIAG(label, field) do { \
            format_u64((u64)cfg.field, number, sizeof number); \
            str_copy(line, label, sizeof line); \
            str_copy(line + str_len(line), number, sizeof line - str_len(line)); \
            print_line(line); \
        } while (0)
        NET_DIAG("   RX head . . . . . . : ", RxHead);
        NET_DIAG("   RX tail . . . . . . : ", RxTail);
        NET_DIAG("   RX descriptor status: ", RxDescStatus);
        NET_DIAG("   RX descriptor errors: ", RxDescErrors);
        NET_DIAG("   RX control . . . . . : ", RxControl);
        NET_DIAG("   HW good RX . . . . . : ", RxGoodCount);
        NET_DIAG("   HW missed RX . . . . : ", RxMissedCount);
        NET_DIAG("   TX head / tail . . . : ", TxHead);
        NET_DIAG("   TX descriptor status: ", TxDescStatus);
#undef NET_DIAG
    }
    command_ipconfig();
}
static void command_ping(const char *arg) {
    u32 ip;
    if (parse_ipv4(arg, &ip)) { print_line("Usage: ping <IPv4 address>"); return; }
    char line[LINE_SIZE], address[20];
    format_ipv4(ip, address, sizeof address);
    str_copy(line, "Pinging ", sizeof line);
    int n = str_len(line); str_copy(line+n, address, sizeof line-n); print_line(line);
    if (NtNetworkPing(ip) == 0) print_line("Reply received (ICMP echo).");
    else print_line("Request timed out or ARP resolution failed.");
}
static void command_tcpconnect(const char *arg) {
    char address_text[32];
    unsigned i=0;
    while(arg[i] && arg[i]!=' ' && i<sizeof(address_text)-1) {
        address_text[i]=arg[i]; i++;
    }
    address_text[i]=0;
    while(arg[i]==' ') i++;
    if(!address_text[0] || !arg[i]) {
        print_line("Usage: tcpconnect <IPv4 address> <port>");
        return;
    }
    u32 ip=0;
    if(parse_ipv4(address_text,&ip)) {
        print_line("tcpconnect: enter a numeric IPv4 address.");
        return;
    }
    unsigned port=0, digits=0;
    while(arg[i]>='0' && arg[i]<='9') {
        port=port*10u+(unsigned)(arg[i]-'0');
        if(port>65535u) break;
        i++; digits++;
    }
    if(!digits || arg[i] || port==0 || port>65535u) {
        print_line("tcpconnect: port must be 1-65535.");
        return;
    }
    print_line("Starting TCP three-way handshake...");
    if(NtNetworkTcpConnect(ip,port)==0)
        print_line("TCP handshake succeeded (SYN/SYN-ACK/ACK).");
    else
        print_line("TCP connect failed or timed out.");
}
static void command_nslookup(const char *arg) {
    u32 ip = 0;
    if (!arg[0]) { print_line("Usage: nslookup <hostname>"); return; }
    if (NtNetworkDnsQuery(arg, &ip) != 0) { print_line("DNS query failed or timed out."); return; }
    char line[LINE_SIZE], address[20];
    format_ipv4(ip, address, sizeof address);
    str_copy(line, "Address: ", sizeof line);
    int n = str_len(line); str_copy(line+n, address, sizeof line-n); print_line(line);
}
static void command_nettest(void) {
    SENTINELOS_NET_CONFIG cfg;
    for (unsigned i = 0; i < sizeof cfg; i++)
        ((unsigned char *)&cfg)[i] = 0;

    print_line("SentinelOS network self-test");
    if (NtQueryNetworkConfig(&cfg) != 0 ||
        !(cfg.Flags & SENTINELOS_NET_DRIVER_READY)) {
        print_line("[FAIL] E1000 driver is not ready.");
        return;
    }
    print_line("[ OK ] E1000 driver detected.");

    if (!(cfg.Flags & SENTINELOS_NET_LINK_UP)) {
        print_line("[FAIL] Ethernet link is down.");
        return;
    }
    print_line("[ OK ] Ethernet link is up.");

    if (!(cfg.Flags & SENTINELOS_NET_IPV4_CONFIGURED)) {
        print_line("[FAIL] No IPv4 lease. Run 'dhcp' first.");
        return;
    }
    print_line("[ OK ] IPv4 configuration is present.");

    u32 resolved = 0;
    if (NtNetworkDnsQuery("example.com", &resolved) == 0) {
        char line[LINE_SIZE], address[20];
        format_ipv4(resolved, address, sizeof address);
        str_copy(line, "[ OK ] DNS example.com -> ", sizeof line);
        str_copy(line + str_len(line), address,
                 sizeof line - str_len(line));
        print_line(line);
    } else {
        print_line("[FAIL] DNS lookup for example.com failed.");
    }

    /* QEMU user networking commonly permits ICMP echo, but hosts or
       upstream networks may filter it. Treat this as a diagnostic only. */
    if (NtNetworkPing(0x01010101u) == 0)
        print_line("[ OK ] ICMP echo to 1.1.1.1 succeeded.");
    else
        print_line("[FAIL] ICMP echo to 1.1.1.1 failed (may be filtered).");

    print_line("Self-test complete; use tcpconnect <IPv4> <port> to test TCP.");
}

static void command_ipconfig(void) {
    SENTINELOS_NET_CONFIG cfg;
    for (unsigned i = 0; i < sizeof cfg; i++)
        ((unsigned char *)&cfg)[i] = 0;

    if (NtQueryNetworkConfig(&cfg) != 0) {
        print_line("ipconfig: network query failed.");
        return;
    }

    print_line("Windows IP Configuration");
    print_line("");
    print_line("Ethernet adapter E1000:");

    char line[LINE_SIZE];
    int n = 0;
    const char *prefix = "   Physical Address . . . : ";
    while (prefix[n] && n < LINE_SIZE - 1) { line[n] = prefix[n]; n++; }
    for (int i = 0; i < 6 && n < LINE_SIZE - 3; i++) {
        line[n++] = hex_digit(cfg.MacAddress[i] >> 4);
        line[n++] = hex_digit(cfg.MacAddress[i]);
        if (i != 5) line[n++] = '-';
    }
    line[n] = 0;
    print_line(line);

    if (cfg.Flags & SENTINELOS_NET_LINK_UP)
        print_line("   Media State . . . . . : Connected");
    else if (cfg.Flags & SENTINELOS_NET_DRIVER_READY)
        print_line("   Media State . . . . . : Disconnected");
    else
        print_line("   Media State . . . . . : Adapter not detected");

    if (!(cfg.Flags & SENTINELOS_NET_IPV4_CONFIGURED)) {
        print_line("   IPv4 Address . . . . : 0.0.0.0");
        print_line("   DHCP . . . . . . . . : Run 'dhcp' to request a lease");
        print_line("   DNS Servers . . . . . : Not configured");
        print_line("");
        print_line("Run dhcp to configure IPv4, then ping or nslookup.");
        return;
    }

    /* IPv4 fields are stored in network byte order. */
    const uint32_t values[3] = { cfg.IPv4Address, cfg.SubnetMask, cfg.Gateway };
    const char *labels[3] = {
        "   IPv4 Address . . . . : ",
        "   Subnet Mask . . . . . : ",
        "   Default Gateway . . . : "
    };
    for (int j = 0; j < 3; j++) {
        n = 0;
        while (labels[j][n] && n < LINE_SIZE - 1) { line[n] = labels[j][n]; n++; }
        for (int b = 24; b >= 0; b -= 8) {
            unsigned v = (values[j] >> b) & 255u;
            char digits[3]; int dn = 0;
            if (v >= 100) digits[dn++] = (char)('0' + v / 100);
            if (v >= 10) digits[dn++] = (char)('0' + (v / 10) % 10);
            digits[dn++] = (char)('0' + v % 10);
            for (int k = 0; k < dn && n < LINE_SIZE - 1; k++) line[n++] = digits[k];
            if (b && n < LINE_SIZE - 1) line[n++] = '.';
        }
        line[n] = 0;
        print_line(line);
    }
    char dnsline[LINE_SIZE], dnsip[20];
    format_ipv4(cfg.DnsServer, dnsip, sizeof dnsip);
    str_copy(dnsline, "   DNS Server . . . . . : ", sizeof dnsline);
    str_copy(dnsline + str_len(dnsline), dnsip, sizeof dnsline - str_len(dnsline));
    print_line(dnsline);
    print_line("");
}

static void run_command(const char *raw) {
    char cmd[CMD_SIZE], *arg;
    str_copy(cmd, raw, sizeof cmd);
    trim_right(cmd);
    if (!cmd[0]) return;

    arg = cmd;
    while (*arg && *arg != ' ') arg++;
    if (*arg) *arg++ = 0;
    while (*arg == ' ') arg++;

    if (equals(cmd, "help")) {
        print_line("Commands: help, ver, cls, dir, mkdir, echo, cat, ps, kill, ipconfig, dhcp, ping, nslookup, nettest, tcpconnect, udpsend, udplisten, exit");
        print_line("Example: mkdir test");
        print_line("echo Hello from SentinelOS > test\\hello.txt");
        print_line("cat test\\hello.txt");
    } else if (equals(cmd, "ver")) {
        print_line("SentinelOS [experimental x86-64]");
    } else if (equals(cmd, "cls") || equals(cmd, "clear")) {
        g_line_count = 0;
        for (int i = 0; i < LINE_COUNT; i++) g_lines[i][0] = 0;
        if (g_text) g_text(g_window, 0, -1, "", 0);
    } else if (equals(cmd, "dir")) {
        command_dir();
    } else if (equals(cmd, "mkdir")) {
        command_mkdir(arg);
    } else if (equals(cmd, "echo")) {
        command_echo(arg);
    } else if (equals(cmd, "cat")) {
        command_cat(arg);
    } else if (equals(cmd, "ps")) {
        command_ps();
    } else if (equals(cmd, "kill")) {
        command_kill(arg);
    } else if (equals(cmd, "udpsend")) {
        command_udpsend(arg);
    } else if (equals(cmd, "udplisten")) {
        command_udplisten(arg);
    } else if (equals(cmd, "ipconfig")) {
        command_ipconfig();
    } else if (equals(cmd, "dhcp")) {
        command_dhcp();
    } else if (equals(cmd, "ping")) {
        command_ping(arg);
    } else if (equals(cmd, "nslookup")) {
        command_nslookup(arg);
    } else if (equals(cmd, "nettest")) {
        command_nettest();
    } else if (equals(cmd, "tcpconnect")) {
        command_tcpconnect(arg);
    } else if (equals(cmd, "exit")) {
        NtExit(0);
    } else {
        print_line("Unknown command. Type help.");
    }
}

void mainCRTStartup(void) {
    char u32name[] = "user32.dll";

    CreateWindowExA_t create =
        (CreateWindowExA_t)NtGetProcAddress(u32name, "CreateWindowExA");
    ShowWindow_t show =
        (ShowWindow_t)NtGetProcAddress(u32name, "ShowWindow");
    GetMessageA_t getmsg =
        (GetMessageA_t)NtGetProcAddress(u32name, "GetMessageA");
    g_text = (TextOutA_t)NtGetProcAddress(u32name, "TextOutA");
    g_invalidate = (InvalidateRect_t)NtGetProcAddress(u32name, "InvalidateRect");

    if (!create || !show || !getmsg || !g_text) {
        NtWriteConsole("cmd: required user32 APIs missing\n");
        NtExit(1);
    }

    for (int i = 0; i < DIR_MAX; i++) g_dirs[i].used = 0;
    for (int i = 0; i < FILE_MAX; i++) g_files[i].used = 0;
    g_cwd[0] = 0;
    g_command_len = 0;
    g_command[0] = 0;

    g_window = create(0, "CMD", "SentinelOS CMD",
                      0, 120, 70, 680, 420, 0, 0, 0, 0);
    show(g_window, 1);

    print_line("SentinelOS Command Prompt");
    print_line("Type help for available commands.");
    print_line("");

    redraw_prompt();

    MSG msg;
    for (;;) {
        if (!getmsg(&msg, 0, 0, 0)) continue;
        if (msg.message != 0x0100) continue; /* WM_KEYDOWN */

        char key = (char)(msg.wParam & 0xFF);
        if (key == '\r' || key == '\n') {
            print_line("C:\\>");
            /* Replace the last prompt line with the entered command. */
            if (g_line_count > 0) {
                int n = str_len(g_lines[g_line_count - 1]);
                if (n + g_command_len < LINE_SIZE - 1) {
                    for (int i = 0; i < g_command_len; i++)
                        g_lines[g_line_count - 1][n++] = g_command[i];
                    g_lines[g_line_count - 1][n] = 0;
                }
                if (g_text)
                    g_text(g_window, 0, g_line_count - 1,
                           g_lines[g_line_count - 1], -1);
            }
            g_command[g_command_len] = 0;
            run_command(g_command);
            g_command_len = 0;
            g_command[0] = 0;
            redraw_prompt();
            continue;
        }

        if (key == '\b') {
            if (g_command_len > 0)
                g_command[--g_command_len] = 0;
            redraw_prompt();
            continue;
        }

        if (((unsigned char)key >= 32) && g_command_len < CMD_SIZE - 1) {
            g_command[g_command_len++] = key;
            g_command[g_command_len] = 0;
            redraw_prompt();
        }
    }
}
