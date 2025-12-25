/*
 * PocketFox Browser - Minimal Web Browser for PowerPC Tiger
 * Uses mbedTLS for modern HTTPS, Cocoa for GUI
 *
 * Build on Tiger:
 *   gcc -arch ppc -std=c99 -O2 -mcpu=7450 -framework Cocoa -DHAVE_MBEDTLS \
 *       -I./mbedtls-2.28.8/include -o PocketFox \
 *       pocketfox_browser.m pocketfox_ssl_tiger.c \
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

    /* Skip scheme */
    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        strcpy(out->scheme, "http");
        out->port = 80;
        p += 7;
    }

    /* Extract host */
    int i = 0;
    while (*p && *p != '/' && *p != ':' && i < 255) {
        out->host[i++] = *p++;
    }
    out->host[i] = '\0';

    /* Port? */
    if (*p == ':') {
        p++;
        out->port = atoi(p);
        while (*p && *p != '/') p++;
    }

    /* Path */
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
        char* err = malloc(512);
        snprintf(err, 512, "Error: %s", pocketfox_ssl_error(ssl));
        pocketfox_ssl_free(ssl);
        return err;
    }

    /* Send request */
    char request[2048];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: PocketFox/1.0 (PowerPC Tiger)\r\n"
             "Accept: text/html,*/*\r\n"
             "Connection: close\r\n\r\n",
             parsed.path, parsed.host);

    pocketfox_ssl_write(ssl, (const unsigned char*)request, strlen(request));

    /* Read response */
    size_t total = 0;
    size_t capacity = 65536;
    char* response = malloc(capacity);

    while (1) {
        if (total + 4096 > capacity) {
            capacity *= 2;
            response = realloc(response, capacity);
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
 * Simple HTML Stripper (for text display)
 * ============================================ */

static char* strip_html(const char* html) {
    /* Skip HTTP headers */
    const char* body = strstr(html, "\r\n\r\n");
    if (body) body += 4;
    else body = html;

    size_t len = strlen(body);
    char* text = malloc(len + 1);
    size_t j = 0;
    int in_tag = 0;
    int in_script = 0;

    for (size_t i = 0; i < len; i++) {
        char c = body[i];

        if (strncasecmp(body + i, "<script", 7) == 0) in_script = 1;
        if (strncasecmp(body + i, "</script>", 9) == 0) in_script = 0;

        if (c == '<') in_tag = 1;
        else if (c == '>') in_tag = 0;
        else if (!in_tag && !in_script) {
            if (c == '&') {
                /* Decode common entities */
                if (strncmp(body + i, "&nbsp;", 6) == 0) { text[j++] = ' '; i += 5; }
                else if (strncmp(body + i, "&lt;", 4) == 0) { text[j++] = '<'; i += 3; }
                else if (strncmp(body + i, "&gt;", 4) == 0) { text[j++] = '>'; i += 3; }
                else if (strncmp(body + i, "&amp;", 5) == 0) { text[j++] = '&'; i += 4; }
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
 * Cocoa GUI
 * ============================================ */

@interface PocketFoxController : NSObject {
    NSTextField* urlField;
    NSTextView* contentView;
    NSProgressIndicator* spinner;
    NSWindow* window;
}
- (void)goAction:(id)sender;
- (void)loadURL:(NSString*)url;
@end

@implementation PocketFoxController

- (void)awakeFromNib {
    pocketfox_ssl_init();
}

- (void)goAction:(id)sender {
    NSString* url = [urlField stringValue];
    [self loadURL:url];
}

- (void)loadURL:(NSString*)url {
    [spinner startAnimation:nil];

    /* Add https:// if missing */
    if (![url hasPrefix:@"http://"] && ![url hasPrefix:@"https://"]) {
        url = [@"https://" stringByAppendingString:url];
    }

    const char* curl = [url UTF8String];

    /* Fetch in background */
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        size_t len;
        char* response = fetch_url(curl, &len);
        char* text = strip_html(response);

        dispatch_async(dispatch_get_main_queue(), ^{
            [spinner stopAnimation:nil];

            NSString* content = [NSString stringWithUTF8String:text];
            if (!content) content = @"(Failed to decode response)";

            [[contentView textStorage] setAttributedString:
                [[NSAttributedString alloc] initWithString:content]];

            free(response);
            free(text);
        });
    });
}

- (void)dealloc {
    pocketfox_ssl_shutdown();
    [super dealloc];
}

@end

/* ============================================
 * App Delegate
 * ============================================ */

@interface PocketFoxAppDelegate : NSObject <NSApplicationDelegate> {
    NSWindow* window;
    NSTextField* urlField;
    NSTextView* contentView;
    NSProgressIndicator* spinner;
    NSButton* goButton;
}
@end

@implementation PocketFoxAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)note {
    pocketfox_ssl_init();

    /* Create window */
    window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(100, 100, 800, 600)
        styleMask:NSTitledWindowMask | NSClosableWindowMask |
                  NSMiniaturizableWindowMask | NSResizableWindowMask
        backing:NSBackingStoreBuffered
        defer:NO];

    [window setTitle:@"PocketFox - PowerPC Tiger Browser"];

    NSView* content = [window contentView];

    /* URL bar */
    urlField = [[NSTextField alloc] initWithFrame:NSMakeRect(10, 560, 680, 24)];
    [urlField setStringValue:@"example.com"];
    [content addSubview:urlField];

    /* Go button */
    goButton = [[NSButton alloc] initWithFrame:NSMakeRect(700, 558, 60, 28)];
    [goButton setTitle:@"Go"];
    [goButton setTarget:self];
    [goButton setAction:@selector(goAction:)];
    [goButton setBezelStyle:NSRoundedBezelStyle];
    [content addSubview:goButton];

    /* Spinner */
    spinner = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(770, 562, 20, 20)];
    [spinner setStyle:NSProgressIndicatorSpinningStyle];
    [spinner setDisplayedWhenStopped:NO];
    [content addSubview:spinner];

    /* Content area */
    NSScrollView* scrollView = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(10, 10, 780, 540)];
    [scrollView setHasVerticalScroller:YES];
    [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

    contentView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 780, 540)];
    [contentView setEditable:NO];
    [contentView setFont:[NSFont fontWithName:@"Monaco" size:12]];
    [scrollView setDocumentView:contentView];
    [content addSubview:scrollView];

    /* Show welcome */
    [contentView setString:
        @"╔═══════════════════════════════════════════════════╗\n"
        @"║  POCKETFOX BROWSER                                ║\n"
        @"║  Modern HTTPS on PowerPC Mac OS X Tiger           ║\n"
        @"╚═══════════════════════════════════════════════════╝\n\n"
        @"Enter a URL above and click Go!\n\n"
        @"Features:\n"
        @"• TLS 1.2 with modern ciphers\n"
        @"• ChaCha20-Poly1305 encryption\n"
        @"• Works on Tiger 10.4+\n\n"
        @"Try: example.com, httpbin.org/html, info.cern.ch"];

    [window makeKeyAndOrderFront:nil];
}

- (void)goAction:(id)sender {
    [spinner startAnimation:nil];

    NSString* url = [urlField stringValue];
    if (![url hasPrefix:@"http://"] && ![url hasPrefix:@"https://"]) {
        url = [@"https://" stringByAppendingString:url];
    }

    const char* curl = [url UTF8String];
    size_t len;
    char* response = fetch_url(curl, &len);
    char* text = strip_html(response);

    NSString* content = [NSString stringWithUTF8String:text];
    if (!content) content = @"(Failed to decode response)";

    [contentView setString:content];
    [spinner stopAnimation:nil];

    free(response);
    free(text);
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)note {
    pocketfox_ssl_shutdown();
}

@end

/* ============================================
 * Main
 * ============================================ */

int main(int argc, char *argv[]) {
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

    NSApplication* app = [NSApplication sharedApplication];

    PocketFoxAppDelegate* delegate = [[PocketFoxAppDelegate alloc] init];
    [app setDelegate:delegate];

    /* Create menu */
    NSMenu* menubar = [[NSMenu alloc] init];
    NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
    [menubar addItem:appMenuItem];
    [app setMainMenu:menubar];

    NSMenu* appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"Quit PocketFox"
                       action:@selector(terminate:)
                keyEquivalent:@"q"];
    [appMenuItem setSubmenu:appMenu];

    [app run];

    [pool drain];
    return 0;
}
