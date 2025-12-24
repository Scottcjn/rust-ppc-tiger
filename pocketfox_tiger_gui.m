/*
 * PocketFox Browser - Minimal Web Browser for PowerPC Tiger
 * Tiger-compatible version (no GCD, no modern protocols)
 *
 * Build on Tiger:
 *   gcc -arch ppc -O2 -mcpu=7450 -framework Cocoa -DHAVE_MBEDTLS \
 *       -I./mbedtls-2.28.8/include -o PocketFox \
 *       pocketfox_tiger_gui.m pocketfox_ssl_tiger.c \
 *       -L./mbedtls-2.28.8/library -lmbedtls -lmbedx509 -lmbedcrypto
 */

#import <Cocoa/Cocoa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations from pocketfox_ssl_tiger.c */
typedef struct PocketFoxSSL PocketFoxSSL;
extern int pocketfox_ssl_init(void);
extern void pocketfox_ssl_shutdown(void);
extern PocketFoxSSL* pocketfox_ssl_new(void);
extern void pocketfox_ssl_free(PocketFoxSSL* ctx);
extern int pocketfox_ssl_connect(PocketFoxSSL* ctx, const char* hostname, int port);
extern int pocketfox_ssl_read(PocketFoxSSL* ctx, unsigned char* buf, size_t len);
extern int pocketfox_ssl_write(PocketFoxSSL* ctx, const unsigned char* buf, size_t len);
extern void pocketfox_ssl_close(PocketFoxSSL* ctx);
extern const char* pocketfox_ssl_error(PocketFoxSSL* ctx);

/* ============================================
 * URL Parser
 * ============================================ */

typedef struct {
    char scheme[16];
    char host[256];
    int port;
    char path[1024];
} ParsedURL;

static int parse_url(const char* url, ParsedURL* out) {
    memset(out, 0, sizeof(ParsedURL));
    strcpy(out->path, "/");
    out->port = 443;
    strcpy(out->scheme, "https");

    const char* p = url;

    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        strcpy(out->scheme, "http");
        out->port = 80;
        p += 7;
    }

    int i = 0;
    while (*p && *p != '/' && *p != ':' && i < 255) {
        out->host[i++] = *p++;
    }
    out->host[i] = '\0';

    if (*p == ':') {
        p++;
        out->port = atoi(p);
        while (*p && *p != '/') p++;
    }

    if (*p == '/') {
        strncpy(out->path, p, sizeof(out->path) - 1);
    }

    return strlen(out->host) > 0;
}

/* ============================================
 * HTTP Fetcher
 * ============================================ */

static char* fetch_url(const char* url, size_t* out_len) {
    ParsedURL parsed;
    if (!parse_url(url, &parsed)) {
        return strdup("Error: Invalid URL");
    }

    PocketFoxSSL* ssl = pocketfox_ssl_new();
    if (!ssl) {
        return strdup("Error: Failed to create SSL context");
    }

    if (pocketfox_ssl_connect(ssl, parsed.host, parsed.port) != 0) {
        char* err = (char*)malloc(512);
        snprintf(err, 512, "Error: %s", pocketfox_ssl_error(ssl));
        pocketfox_ssl_free(ssl);
        return err;
    }

    char request[2048];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: PocketFox/1.0 (PowerPC Tiger)\r\n"
             "Accept: text/html,*/*\r\n"
             "Connection: close\r\n\r\n",
             parsed.path, parsed.host);

    pocketfox_ssl_write(ssl, (const unsigned char*)request, strlen(request));

    size_t total = 0;
    size_t capacity = 65536;
    char* response = (char*)malloc(capacity);

    while (1) {
        if (total + 4096 > capacity) {
            capacity *= 2;
            response = (char*)realloc(response, capacity);
        }

        int n = pocketfox_ssl_read(ssl, (unsigned char*)response + total, 4096);
        if (n <= 0) break;
        total += n;
    }

    response[total] = '\0';
    pocketfox_ssl_close(ssl);
    pocketfox_ssl_free(ssl);

    if (out_len) *out_len = total;
    return response;
}

/* ============================================
 * Simple HTML Stripper
 * ============================================ */

static char* strip_html(const char* html) {
    const char* body = strstr(html, "\r\n\r\n");
    if (body) body += 4;
    else body = html;

    size_t len = strlen(body);
    char* text = (char*)malloc(len + 1);
    size_t j = 0;
    int in_tag = 0;
    int in_script = 0;
    size_t i;

    for (i = 0; i < len; i++) {
        char c = body[i];

        if (i + 7 < len && strncasecmp(body + i, "<script", 7) == 0) in_script = 1;
        if (i + 9 < len && strncasecmp(body + i, "</script>", 9) == 0) { in_script = 0; i += 8; continue; }
        if (i + 6 < len && strncasecmp(body + i, "<style", 6) == 0) in_script = 1;
        if (i + 8 < len && strncasecmp(body + i, "</style>", 8) == 0) { in_script = 0; i += 7; continue; }

        if (c == '<') { in_tag = 1; continue; }
        if (c == '>') { in_tag = 0; continue; }

        if (!in_tag && !in_script) {
            if (c == '&') {
                if (strncmp(body + i, "&nbsp;", 6) == 0) { text[j++] = ' '; i += 5; }
                else if (strncmp(body + i, "&lt;", 4) == 0) { text[j++] = '<'; i += 3; }
                else if (strncmp(body + i, "&gt;", 4) == 0) { text[j++] = '>'; i += 3; }
                else if (strncmp(body + i, "&amp;", 5) == 0) { text[j++] = '&'; i += 4; }
                else if (strncmp(body + i, "&quot;", 6) == 0) { text[j++] = '"'; i += 5; }
                else text[j++] = c;
            } else {
                text[j++] = c;
            }
        }
    }

    text[j] = '\0';
    return text;
}

/* ============================================
 * Application Controller
 * ============================================ */

@interface PocketFoxApp : NSObject {
    NSWindow* window;
    NSTextField* urlField;
    NSTextView* contentView;
    NSButton* goButton;
}
- (void)createUI;
- (void)goAction:(id)sender;
@end

@implementation PocketFoxApp

- (void)createUI {
    /* Create window */
    window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(50, 50, 800, 600)
        styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                   NSMiniaturizableWindowMask | NSResizableWindowMask)
        backing:NSBackingStoreBuffered
        defer:NO];

    [window setTitle:@"PocketFox - PowerPC Tiger"];
    [window setMinSize:NSMakeSize(400, 300)];

    NSView* content = [window contentView];

    /* URL bar */
    urlField = [[NSTextField alloc] initWithFrame:NSMakeRect(10, 566, 700, 24)];
    [urlField setStringValue:@"example.com"];
    [urlField setTarget:self];
    [urlField setAction:@selector(goAction:)];
    [content addSubview:urlField];
    [urlField release];

    /* Go button */
    goButton = [[NSButton alloc] initWithFrame:NSMakeRect(720, 564, 70, 28)];
    [goButton setTitle:@"Go"];
    [goButton setTarget:self];
    [goButton setAction:@selector(goAction:)];
    [goButton setBezelStyle:NSRoundedBezelStyle];
    [content addSubview:goButton];
    [goButton release];

    /* Content scroll view */
    NSScrollView* scrollView = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(10, 10, 780, 545)];
    [scrollView setHasVerticalScroller:YES];
    [scrollView setHasHorizontalScroller:NO];
    [scrollView setAutohidesScrollers:NO];
    [scrollView setBorderType:NSBezelBorder];
    [scrollView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    NSSize contentSize = [scrollView contentSize];
    contentView = [[NSTextView alloc]
        initWithFrame:NSMakeRect(0, 0, contentSize.width, contentSize.height)];
    [contentView setEditable:NO];
    [contentView setFont:[NSFont fontWithName:@"Monaco" size:11]];
    [contentView setMinSize:NSMakeSize(0, contentSize.height)];
    [contentView setMaxSize:NSMakeSize(FLT_MAX, FLT_MAX)];
    [contentView setVerticallyResizable:YES];
    [contentView setHorizontallyResizable:NO];
    [contentView setAutoresizingMask:NSViewWidthSizable];
    [[contentView textContainer] setContainerSize:NSMakeSize(contentSize.width, FLT_MAX)];
    [[contentView textContainer] setWidthTracksTextView:YES];

    [scrollView setDocumentView:contentView];
    [content addSubview:scrollView];
    [scrollView release];

    /* Welcome message */
    [contentView setString:
        @"╔═══════════════════════════════════════════════════════════╗\n"
        @"║                                                           ║\n"
        @"║            P O C K E T   F O X   B R O W S E R            ║\n"
        @"║                                                           ║\n"
        @"║       Modern HTTPS on PowerPC Mac OS X Tiger              ║\n"
        @"║                                                           ║\n"
        @"╚═══════════════════════════════════════════════════════════╝\n\n"
        @"Enter a URL in the address bar above and press Go or Return.\n\n"
        @"Features:\n"
        @"  • TLS 1.2 with modern ciphers (ChaCha20-Poly1305)\n"
        @"  • Built-in mbedTLS - no system SSL dependency\n"
        @"  • Works on Tiger 10.4+ and Leopard 10.5+\n"
        @"  • Native PowerPC G4/G5 binary\n\n"
        @"Try these sites:\n"
        @"  • example.com\n"
        @"  • httpbin.org/html\n"
        @"  • info.cern.ch (first website ever!)\n"
        @"  • wttr.in/Paris (weather in text)\n\n"
        @"Built with mbedTLS 2.28 LTS\n"
        @"github.com/Scottcjn/rust-ppc-tiger\n"];

    [window makeKeyAndOrderFront:nil];
}

- (void)goAction:(id)sender {
    NSString* urlString = [urlField stringValue];

    /* Add https:// if no scheme */
    if (![urlString hasPrefix:@"http://"] && ![urlString hasPrefix:@"https://"]) {
        urlString = [@"https://" stringByAppendingString:urlString];
    }

    [contentView setString:@"Loading..."];
    [[contentView window] display];

    const char* url = [urlString UTF8String];
    size_t len = 0;
    char* response = fetch_url(url, &len);
    char* text = strip_html(response);

    NSString* content = [NSString stringWithUTF8String:text];
    if (!content) {
        content = [NSString stringWithCString:text encoding:NSISOLatin1StringEncoding];
    }
    if (!content) {
        content = @"(Could not decode response)";
    }

    [contentView setString:content];

    free(response);
    free(text);
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
    return YES;
}

@end

/* ============================================
 * Main
 * ============================================ */

int main(int argc, char *argv[]) {
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

    pocketfox_ssl_init();

    [NSApplication sharedApplication];

    PocketFoxApp* app = [[PocketFoxApp alloc] init];
    [NSApp setDelegate:app];

    /* Create menu bar */
    NSMenu* mainMenu = [[NSMenu alloc] init];
    NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:appMenuItem];

    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"PocketFox"];
    NSMenuItem* quitItem = [[NSMenuItem alloc]
        initWithTitle:@"Quit PocketFox"
               action:@selector(terminate:)
        keyEquivalent:@"q"];
    [appMenu addItem:quitItem];
    [appMenuItem setSubmenu:appMenu];

    [NSApp setMainMenu:mainMenu];

    /* Create UI */
    [app createUI];

    /* Run */
    [NSApp run];

    pocketfox_ssl_shutdown();
    [pool release];

    return 0;
}
