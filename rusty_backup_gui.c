/*
 * rusty_backup_gui.c — Carbon GUI for rusty-backup on Mac OS X Tiger PPC
 *
 * Native Aqua interface with tabs for Backup, Restore, Inspect, Devices.
 * Calls the same functions as the CLI (rust_cli_real.c).
 *
 * Build on Tiger:
 *   gcc-4.0 -std=c99 -O2 -framework Carbon -o rusty-backup-gui rusty_backup_gui.c rust_cli_real.o <runtime .o files> -lgcc
 *
 * Or as a .app bundle:
 *   mkdir -p RustyBackup.app/Contents/MacOS
 *   cp rusty-backup-gui RustyBackup.app/Contents/MacOS/RustyBackup
 *   cp Info.plist RustyBackup.app/Contents/
 */

#include <Carbon/Carbon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/mount.h>

#ifdef __APPLE__
#include <sys/disk.h>
#endif

/* ============================================================
 * Forward declarations from rust_cli_real.c
 * We reuse its partition detection and backup/restore logic
 * ============================================================ */

/* We'll call the CLI as a subprocess for simplicity and thread safety.
 * The GUI captures stdout/stderr from the child process. */

/* ============================================================
 * Constants
 * ============================================================ */

#define APP_NAME      "Rusty Backup"
#define APP_VERSION   "0.2.0-ppc"
#define WIN_WIDTH     640
#define WIN_HEIGHT    520
#define LOG_HEIGHT    160
#define MAX_LOG       16384
#define MAX_PATH_LEN  1024

/* Control IDs */
enum {
    kTabControlID       = 100,
    /* Backup tab */
    kBackupSourceLabel  = 200,
    kBackupSourceField  = 201,
    kBackupSourceBtn    = 202,
    kBackupDestLabel    = 203,
    kBackupDestField    = 204,
    kBackupDestBtn      = 205,
    kBackupNameLabel    = 206,
    kBackupNameField    = 207,
    kBackupStartBtn     = 210,
    kBackupProgress     = 211,
    /* Restore tab */
    kRestoreBackupLabel = 300,
    kRestoreBackupField = 301,
    kRestoreBackupBtn   = 302,
    kRestoreTargetLabel = 303,
    kRestoreTargetField = 304,
    kRestoreTargetBtn   = 305,
    kRestoreStartBtn    = 310,
    kRestoreProgress    = 311,
    /* Inspect tab */
    kInspectPathLabel   = 400,
    kInspectPathField   = 401,
    kInspectPathBtn     = 402,
    kInspectStartBtn    = 410,
    /* Devices tab */
    kDevicesRefreshBtn  = 500,
    /* Shared */
    kLogText            = 900,
    kStatusText         = 901
};

/* Tab indices */
enum {
    TAB_BACKUP  = 1,
    TAB_RESTORE = 2,
    TAB_INSPECT = 3,
    TAB_DEVICES = 4
};

/* ============================================================
 * Globals
 * ============================================================ */

static WindowRef       gMainWindow = NULL;
static ControlRef      gTabControl = NULL;
static ControlRef      gLogText    = NULL;
static ControlRef      gStatusText = NULL;
static ControlRef      gBackupProgress = NULL;
static ControlRef      gRestoreProgress = NULL;

/* Backup tab controls */
static ControlRef      gBackupSourceField = NULL;
static ControlRef      gBackupDestField   = NULL;
static ControlRef      gBackupNameField   = NULL;
static ControlRef      gBackupStartBtn    = NULL;

/* Restore tab controls */
static ControlRef      gRestoreBackupField = NULL;
static ControlRef      gRestoreTargetField = NULL;
static ControlRef      gRestoreStartBtn    = NULL;

/* Inspect tab controls */
static ControlRef      gInspectPathField = NULL;
static ControlRef      gInspectStartBtn  = NULL;

/* Devices tab */
static ControlRef      gDevicesRefreshBtn = NULL;

/* State */
static char            gLogBuffer[MAX_LOG];
static int             gLogLen = 0;
static int             gOperationRunning = 0;
static char            gExePath[MAX_PATH_LEN]; /* path to rusty-backup-ppc binary */

/* ============================================================
 * Utility: Find our CLI binary
 * ============================================================ */

static void find_cli_binary(void) {
    /* Look in same directory as GUI, then in PATH */
    CFBundleRef bundle = CFBundleGetMainBundle();
    if (bundle) {
        CFURLRef url = CFBundleCopyExecutableURL(bundle);
        if (url) {
            CFURLRef dir = CFURLCreateCopyDeletingLastPathComponent(NULL, url);
            if (dir) {
                CFURLRef cli = CFURLCreateCopyAppendingPathComponent(NULL, dir,
                    CFSTR("rusty-backup-ppc"), false);
                if (cli) {
                    CFStringRef path = CFURLCopyFileSystemPath(cli, kCFURLPOSIXPathStyle);
                    if (path) {
                        CFStringGetCString(path, gExePath, sizeof(gExePath),
                            kCFStringEncodingUTF8);
                        CFRelease(path);
                    }
                    CFRelease(cli);
                }
                CFRelease(dir);
            }
            CFRelease(url);
        }
    }

    /* Check if found path exists */
    if (gExePath[0] && access(gExePath, X_OK) == 0) return;

    /* Try current directory */
    if (getcwd(gExePath, sizeof(gExePath) - 32)) {
        strcat(gExePath, "/rusty-backup-ppc");
        if (access(gExePath, X_OK) == 0) return;
    }

    /* Try /usr/local/bin */
    strcpy(gExePath, "/usr/local/bin/rusty-backup-ppc");
    if (access(gExePath, X_OK) == 0) return;

    /* Fallback: just use name and hope it's in PATH */
    strcpy(gExePath, "rusty-backup-ppc");
}

/* ============================================================
 * Log Panel
 * ============================================================ */

static void log_append(const char *text) {
    int tlen = strlen(text);
    if (gLogLen + tlen >= MAX_LOG - 1) {
        /* Trim first half */
        int keep = MAX_LOG / 2;
        memmove(gLogBuffer, gLogBuffer + gLogLen - keep, keep);
        gLogLen = keep;
    }
    memcpy(gLogBuffer + gLogLen, text, tlen);
    gLogLen += tlen;
    gLogBuffer[gLogLen] = '\0';

    /* Update the text control */
    if (gLogText) {
        CFStringRef str = CFStringCreateWithCString(NULL, gLogBuffer,
            kCFStringEncodingUTF8);
        if (str) {
            SetControlData(gLogText, kControlEditTextPart,
                kControlStaticTextCFStringTag, sizeof(CFStringRef), &str);
            CFRelease(str);
            DrawOneControl(gLogText);
        }
    }
}

static void log_line(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    log_append(buf);
}

static void set_status(const char *text) {
    if (gStatusText) {
        CFStringRef str = CFStringCreateWithCString(NULL, text,
            kCFStringEncodingUTF8);
        if (str) {
            SetControlData(gStatusText, kControlEntireControl,
                kControlStaticTextCFStringTag, sizeof(CFStringRef), &str);
            CFRelease(str);
            DrawOneControl(gStatusText);
        }
    }
}

/* ============================================================
 * Get text from an EditText control
 * ============================================================ */

static void get_field_text(ControlRef ctrl, char *buf, int bufsz) {
    buf[0] = '\0';
    if (!ctrl) return;

    Size actual = 0;
    CFStringRef str = NULL;
    OSStatus err = GetControlData(ctrl, kControlEditTextPart,
        kControlEditTextCFStringTag, sizeof(CFStringRef), &str, &actual);
    if (err == noErr && str) {
        CFStringGetCString(str, buf, bufsz, kCFStringEncodingUTF8);
        /* Don't release — GetControlData for CFString tags doesn't retain */
    }
}

static void set_field_text(ControlRef ctrl, const char *text) {
    if (!ctrl) return;
    CFStringRef str = CFStringCreateWithCString(NULL, text,
        kCFStringEncodingUTF8);
    if (str) {
        SetControlData(ctrl, kControlEditTextPart,
            kControlEditTextCFStringTag, sizeof(CFStringRef), &str);
        CFRelease(str);
        DrawOneControl(ctrl);
    }
}

/* ============================================================
 * File/Folder Chooser Dialogs (Navigation Services)
 * ============================================================ */

static int choose_file(char *path, int pathsz) {
    NavDialogRef dialog = NULL;
    NavDialogCreationOptions opts;

    NavGetDefaultDialogCreationOptions(&opts);
    opts.optionFlags &= ~kNavAllowMultipleFiles;
    opts.windowTitle = CFSTR("Choose Source Device or Image");

    OSStatus err = NavCreateChooseFileDialog(&opts, NULL, NULL, NULL, NULL,
        NULL, &dialog);
    if (err != noErr || !dialog) return 0;

    err = NavDialogRun(dialog);
    if (err != noErr) { NavDialogDispose(dialog); return 0; }

    NavReplyRecord reply;
    err = NavDialogGetReply(dialog, &reply);
    if (err != noErr || !reply.validRecord) {
        NavDialogDispose(dialog);
        return 0;
    }

    AEDesc desc;
    err = AEGetNthDesc(&reply.selection, 1, typeFSRef, NULL, &desc);
    if (err == noErr) {
        FSRef ref;
        err = AEGetDescData(&desc, &ref, sizeof(FSRef));
        if (err == noErr)
            FSRefMakePath(&ref, (UInt8 *)path, pathsz);
        AEDisposeDesc(&desc);
    }

    NavDisposeReply(&reply);
    NavDialogDispose(dialog);
    return path[0] != '\0';
}

static int choose_folder(char *path, int pathsz) {
    NavDialogRef dialog = NULL;
    NavDialogCreationOptions opts;

    NavGetDefaultDialogCreationOptions(&opts);
    opts.windowTitle = CFSTR("Choose Destination Folder");

    OSStatus err = NavCreateChooseFolderDialog(&opts, NULL, NULL, NULL, &dialog);
    if (err != noErr || !dialog) return 0;

    err = NavDialogRun(dialog);
    if (err != noErr) { NavDialogDispose(dialog); return 0; }

    NavReplyRecord reply;
    err = NavDialogGetReply(dialog, &reply);
    if (err != noErr || !reply.validRecord) {
        NavDialogDispose(dialog);
        return 0;
    }

    AEDesc desc;
    err = AEGetNthDesc(&reply.selection, 1, typeFSRef, NULL, &desc);
    if (err == noErr) {
        FSRef ref;
        err = AEGetDescData(&desc, &ref, sizeof(FSRef));
        if (err == noErr)
            FSRefMakePath(&ref, (UInt8 *)path, pathsz);
        AEDisposeDesc(&desc);
    }

    NavDisposeReply(&reply);
    NavDialogDispose(dialog);
    return path[0] != '\0';
}

/* ============================================================
 * Run CLI command in background thread
 * ============================================================ */

typedef struct {
    char cmd[2048];
    ControlRef progress;
} OperationArgs;

static void *run_operation_thread(void *arg) {
    OperationArgs *oa = (OperationArgs *)arg;

    /* Run the command and capture output */
    FILE *pipe = popen(oa->cmd, "r");
    if (!pipe) {
        log_line("Error: Cannot execute command");
        gOperationRunning = 0;
        free(oa);
        return NULL;
    }

    char line[512];
    while (fgets(line, sizeof(line), pipe)) {
        /* Strip trailing newline */
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Parse progress bar output */
        if (line[0] == '[' && strstr(line, "%")) {
            /* Extract percentage */
            const char *pct = strstr(line, "]");
            if (pct) {
                pct++;
                while (*pct == ' ') pct++;
                double val = 0;
                sscanf(pct, "%lf", &val);
                if (oa->progress) {
                    SetControl32BitValue(oa->progress, (SInt32)val);
                    DrawOneControl(oa->progress);
                }
            }
        } else if (len > 0) {
            log_line("%s", line);
        }
    }

    int status = pclose(pipe);
    if (status == 0) {
        log_line("Operation completed successfully.");
        set_status("Ready");
    } else {
        log_line("Operation failed (exit code %d).", WEXITSTATUS(status));
        set_status("Error");
    }

    if (oa->progress) {
        SetControl32BitValue(oa->progress, 0);
        DrawOneControl(oa->progress);
    }

    gOperationRunning = 0;
    free(oa);
    return NULL;
}

static void start_operation(const char *cmd, ControlRef progress) {
    if (gOperationRunning) {
        log_line("An operation is already running.");
        return;
    }

    OperationArgs *oa = (OperationArgs *)malloc(sizeof(OperationArgs));
    if (!oa) return;
    strncpy(oa->cmd, cmd, sizeof(oa->cmd) - 1);
    oa->cmd[sizeof(oa->cmd) - 1] = '\0';
    oa->progress = progress;

    gOperationRunning = 1;
    log_line("> %s", cmd);
    set_status("Working...");

    pthread_t thread;
    pthread_create(&thread, NULL, run_operation_thread, oa);
    pthread_detach(thread);
}

/* ============================================================
 * Command Handlers
 * ============================================================ */

static void do_backup(void) {
    char source[MAX_PATH_LEN], dest[MAX_PATH_LEN], name[256];
    get_field_text(gBackupSourceField, source, sizeof(source));
    get_field_text(gBackupDestField, dest, sizeof(dest));
    get_field_text(gBackupNameField, name, sizeof(name));

    if (!source[0] || !dest[0]) {
        log_line("Error: Source and destination are required.");
        return;
    }
    if (!name[0]) strcpy(name, "backup");

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "'%s' backup --source '%s' --dest '%s' --name '%s' 2>&1",
        gExePath, source, dest, name);
    start_operation(cmd, gBackupProgress);
}

static void do_restore(void) {
    char backup_dir[MAX_PATH_LEN], target[MAX_PATH_LEN];
    get_field_text(gRestoreBackupField, backup_dir, sizeof(backup_dir));
    get_field_text(gRestoreTargetField, target, sizeof(target));

    if (!backup_dir[0] || !target[0]) {
        log_line("Error: Backup directory and target are required.");
        return;
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "'%s' restore --backup-dir '%s' --target '%s' 2>&1",
        gExePath, backup_dir, target);
    start_operation(cmd, gRestoreProgress);
}

static void do_inspect(void) {
    char path[MAX_PATH_LEN];
    get_field_text(gInspectPathField, path, sizeof(path));

    if (!path[0]) {
        log_line("Error: Backup path is required.");
        return;
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "'%s' inspect '%s' 2>&1", gExePath, path);
    start_operation(cmd, NULL);
}

static void do_list_devices(void) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "'%s' list-devices 2>&1", gExePath);
    start_operation(cmd, NULL);
}

/* ============================================================
 * Create Controls
 * ============================================================ */

static ControlRef make_label(WindowRef win, int x, int y, int w, int h,
                              const char *text) {
    Rect r = { y, x, y + h, x + w };
    ControlRef ctrl = NULL;
    CFStringRef str = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
    CreateStaticTextControl(win, &r, str, NULL, &ctrl);
    CFRelease(str);
    return ctrl;
}

static ControlRef make_edittext(WindowRef win, int x, int y, int w, int h) {
    Rect r = { y, x, y + h, x + w };
    ControlRef ctrl = NULL;
    CreateEditUnicodeTextControl(win, &r, CFSTR(""), false, NULL, &ctrl);
    return ctrl;
}

static ControlRef make_button(WindowRef win, int x, int y, int w, int h,
                               const char *title, UInt32 cmd) {
    Rect r = { y, x, y + h, x + w };
    ControlRef ctrl = NULL;
    CFStringRef str = CFStringCreateWithCString(NULL, title, kCFStringEncodingUTF8);
    CreatePushButtonControl(win, &r, str, &ctrl);
    CFRelease(str);

    if (ctrl && cmd) {
        SetControlCommandID(ctrl, cmd);
    }
    return ctrl;
}

static ControlRef make_progress(WindowRef win, int x, int y, int w, int h) {
    Rect r = { y, x, y + h, x + w };
    ControlRef ctrl = NULL;
    CreateProgressBarControl(win, &r, 0, 0, 100, false, &ctrl);
    return ctrl;
}

/* ============================================================
 * Tab Visibility Management
 * ============================================================ */

/* Control arrays per tab for show/hide */
static ControlRef gBackupControls[10];
static int        gBackupControlCount = 0;
static ControlRef gRestoreControls[10];
static int        gRestoreControlCount = 0;
static ControlRef gInspectControls[10];
static int        gInspectControlCount = 0;
static ControlRef gDevicesControls[10];
static int        gDevicesControlCount = 0;

static void show_tab(int tab) {
    int i;
    /* Hide all */
    for (i = 0; i < gBackupControlCount; i++)
        if (gBackupControls[i]) SetControlVisibility(gBackupControls[i], false, true);
    for (i = 0; i < gRestoreControlCount; i++)
        if (gRestoreControls[i]) SetControlVisibility(gRestoreControls[i], false, true);
    for (i = 0; i < gInspectControlCount; i++)
        if (gInspectControls[i]) SetControlVisibility(gInspectControls[i], false, true);
    for (i = 0; i < gDevicesControlCount; i++)
        if (gDevicesControls[i]) SetControlVisibility(gDevicesControls[i], false, true);

    /* Show selected */
    switch (tab) {
        case TAB_BACKUP:
            for (i = 0; i < gBackupControlCount; i++)
                if (gBackupControls[i]) SetControlVisibility(gBackupControls[i], true, true);
            break;
        case TAB_RESTORE:
            for (i = 0; i < gRestoreControlCount; i++)
                if (gRestoreControls[i]) SetControlVisibility(gRestoreControls[i], true, true);
            break;
        case TAB_INSPECT:
            for (i = 0; i < gInspectControlCount; i++)
                if (gInspectControls[i]) SetControlVisibility(gInspectControls[i], true, true);
            break;
        case TAB_DEVICES:
            for (i = 0; i < gDevicesControlCount; i++)
                if (gDevicesControls[i]) SetControlVisibility(gDevicesControls[i], true, true);
            break;
    }
}

/* ============================================================
 * Build the Window
 * ============================================================ */

static void create_main_window(void) {
    Rect bounds = { 100, 100, 100 + WIN_HEIGHT, 100 + WIN_WIDTH };
    OSStatus err;

    err = CreateNewWindow(kDocumentWindowClass,
        kWindowStandardDocumentAttributes | kWindowStandardHandlerAttribute |
        kWindowCompositingAttribute,
        &bounds, &gMainWindow);
    if (err != noErr) return;

    CFStringRef title = CFStringCreateWithCString(NULL,
        APP_NAME " " APP_VERSION " (PowerPC Tiger)",
        kCFStringEncodingUTF8);
    SetWindowTitleWithCFString(gMainWindow, title);
    CFRelease(title);

    int lm = 20;   /* left margin */
    int tw = WIN_WIDTH - 40;  /* tab width */

    /* ---- Tab Control ---- */
    Rect tabRect = { 10, lm, 310, lm + tw };
    ControlTabEntry tabs[4];
    memset(tabs, 0, sizeof(tabs));
    tabs[0].name = CFSTR("Backup");
    tabs[1].name = CFSTR("Restore");
    tabs[2].name = CFSTR("Inspect");
    tabs[3].name = CFSTR("Devices");
    CreateTabsControl(gMainWindow, &tabRect, kControlTabSizeLarge,
        kControlTabDirectionNorth, 4, tabs, &gTabControl);

    /* ---- Content area starts at y=50 (inside tabs) ---- */
    int cy = 55;  /* content y start */

    /* ======== BACKUP TAB ======== */
    gBackupControls[gBackupControlCount++] =
        make_label(gMainWindow, lm + 10, cy, 100, 18, "Source:");
    gBackupSourceField =
        make_edittext(gMainWindow, lm + 110, cy - 2, tw - 200, 22);
    gBackupControls[gBackupControlCount++] = gBackupSourceField;
    gBackupControls[gBackupControlCount++] =
        make_button(gMainWindow, lm + tw - 80, cy - 2, 70, 22, "Browse\xE2\x80\xA6", 'bksb');

    cy += 32;
    gBackupControls[gBackupControlCount++] =
        make_label(gMainWindow, lm + 10, cy, 100, 18, "Destination:");
    gBackupDestField =
        make_edittext(gMainWindow, lm + 110, cy - 2, tw - 200, 22);
    gBackupControls[gBackupControlCount++] = gBackupDestField;
    gBackupControls[gBackupControlCount++] =
        make_button(gMainWindow, lm + tw - 80, cy - 2, 70, 22, "Browse\xE2\x80\xA6", 'bkdb');

    cy += 32;
    gBackupControls[gBackupControlCount++] =
        make_label(gMainWindow, lm + 10, cy, 100, 18, "Backup Name:");
    gBackupNameField =
        make_edittext(gMainWindow, lm + 110, cy - 2, 200, 22);
    gBackupControls[gBackupControlCount++] = gBackupNameField;
    set_field_text(gBackupNameField, "backup");

    cy += 40;
    gBackupProgress = make_progress(gMainWindow, lm + 10, cy, tw - 130, 16);
    gBackupControls[gBackupControlCount++] = gBackupProgress;
    gBackupStartBtn =
        make_button(gMainWindow, lm + tw - 110, cy - 4, 100, 24, "Start Backup", 'bkgo');
    gBackupControls[gBackupControlCount++] = gBackupStartBtn;

    /* ======== RESTORE TAB ======== */
    cy = 55;
    gRestoreControls[gRestoreControlCount++] =
        make_label(gMainWindow, lm + 10, cy, 100, 18, "Backup Dir:");
    gRestoreBackupField =
        make_edittext(gMainWindow, lm + 110, cy - 2, tw - 200, 22);
    gRestoreControls[gRestoreControlCount++] = gRestoreBackupField;
    gRestoreControls[gRestoreControlCount++] =
        make_button(gMainWindow, lm + tw - 80, cy - 2, 70, 22, "Browse\xE2\x80\xA6", 'rsbk');

    cy += 32;
    gRestoreControls[gRestoreControlCount++] =
        make_label(gMainWindow, lm + 10, cy, 100, 18, "Target:");
    gRestoreTargetField =
        make_edittext(gMainWindow, lm + 110, cy - 2, tw - 200, 22);
    gRestoreControls[gRestoreControlCount++] = gRestoreTargetField;
    gRestoreControls[gRestoreControlCount++] =
        make_button(gMainWindow, lm + tw - 80, cy - 2, 70, 22, "Browse\xE2\x80\xA6", 'rstg');

    cy += 40;
    gRestoreProgress = make_progress(gMainWindow, lm + 10, cy, tw - 130, 16);
    gRestoreControls[gRestoreControlCount++] = gRestoreProgress;
    gRestoreStartBtn =
        make_button(gMainWindow, lm + tw - 110, cy - 4, 100, 24, "Start Restore", 'rsgo');
    gRestoreControls[gRestoreControlCount++] = gRestoreStartBtn;

    /* ======== INSPECT TAB ======== */
    cy = 55;
    gInspectControls[gInspectControlCount++] =
        make_label(gMainWindow, lm + 10, cy, 100, 18, "Backup Dir:");
    gInspectPathField =
        make_edittext(gMainWindow, lm + 110, cy - 2, tw - 200, 22);
    gInspectControls[gInspectControlCount++] = gInspectPathField;
    gInspectControls[gInspectControlCount++] =
        make_button(gMainWindow, lm + tw - 80, cy - 2, 70, 22, "Browse\xE2\x80\xA6", 'inbr');

    cy += 40;
    gInspectStartBtn =
        make_button(gMainWindow, lm + 10, cy, 120, 24, "Inspect Backup", 'ingo');
    gInspectControls[gInspectControlCount++] = gInspectStartBtn;

    /* ======== DEVICES TAB ======== */
    cy = 55;
    gDevicesRefreshBtn =
        make_button(gMainWindow, lm + 10, cy, 140, 24, "Refresh Devices", 'dvrf');
    gDevicesControls[gDevicesControlCount++] = gDevicesRefreshBtn;

    /* ---- Log Panel (shared, below tabs) ---- */
    Rect logRect = { 320, lm, 320 + LOG_HEIGHT, lm + tw };
    CFStringRef emptyStr = CFSTR("Rusty Backup GUI ready.\n");
    CreateStaticTextControl(gMainWindow, &logRect, emptyStr, NULL, &gLogText);
    /* Make it scrollable-looking with a frame */

    /* Status bar */
    Rect statusRect = { WIN_HEIGHT - 30, lm, WIN_HEIGHT - 12, lm + tw };
    CreateStaticTextControl(gMainWindow, &statusRect, CFSTR("Ready"), NULL, &gStatusText);

    /* Initialize log */
    strcpy(gLogBuffer, "Rusty Backup GUI ready.\n");
    gLogLen = strlen(gLogBuffer);
    log_line("CLI binary: %s", gExePath);

    /* Show only backup tab initially */
    show_tab(TAB_BACKUP);

    ShowWindow(gMainWindow);
}

/* ============================================================
 * Event Handlers
 * ============================================================ */

static OSStatus handle_command(EventHandlerCallRef next, EventRef event,
                                void *userData) {
    HICommandExtended cmd;
    GetEventParameter(event, kEventParamDirectObject, typeHICommand,
        NULL, sizeof(cmd), NULL, &cmd);

    switch (cmd.commandID) {
        case 'bksb': {  /* Backup source browse */
            char path[MAX_PATH_LEN] = "";
            if (choose_file(path, sizeof(path)))
                set_field_text(gBackupSourceField, path);
            return noErr;
        }
        case 'bkdb': {  /* Backup dest browse */
            char path[MAX_PATH_LEN] = "";
            if (choose_folder(path, sizeof(path)))
                set_field_text(gBackupDestField, path);
            return noErr;
        }
        case 'bkgo':    /* Start backup */
            do_backup();
            return noErr;

        case 'rsbk': {  /* Restore backup dir browse */
            char path[MAX_PATH_LEN] = "";
            if (choose_folder(path, sizeof(path)))
                set_field_text(gRestoreBackupField, path);
            return noErr;
        }
        case 'rstg': {  /* Restore target browse */
            char path[MAX_PATH_LEN] = "";
            if (choose_file(path, sizeof(path)))
                set_field_text(gRestoreTargetField, path);
            return noErr;
        }
        case 'rsgo':    /* Start restore */
            do_restore();
            return noErr;

        case 'inbr': {  /* Inspect browse */
            char path[MAX_PATH_LEN] = "";
            if (choose_folder(path, sizeof(path)))
                set_field_text(gInspectPathField, path);
            return noErr;
        }
        case 'ingo':    /* Start inspect */
            do_inspect();
            return noErr;

        case 'dvrf':    /* Refresh devices */
            do_list_devices();
            return noErr;

        case kHICommandQuit:
            if (gOperationRunning) {
                log_line("Warning: Operation still running!");
                /* TODO: Could show alert */
            }
            QuitApplicationEventLoop();
            return noErr;

        default:
            break;
    }

    return eventNotHandledErr;
}

static OSStatus handle_tab_change(EventHandlerCallRef next, EventRef event,
                                   void *userData) {
    ControlRef ctrl;
    GetEventParameter(event, kEventParamDirectObject, typeControlRef,
        NULL, sizeof(ctrl), NULL, &ctrl);

    if (ctrl == gTabControl) {
        SInt16 tab = GetControl32BitValue(gTabControl);
        show_tab(tab);
    }
    return eventNotHandledErr;  /* let system process too */
}

/* ============================================================
 * Menu Bar
 * ============================================================ */

static void create_menus(void) {
    MenuRef appleMenu, fileMenu;

    CreateNewMenu(0, 0, &appleMenu);
    SetMenuTitleWithCFString(appleMenu, CFSTR("\x14"));  /* Apple menu */
    AppendMenuItemTextWithCFString(appleMenu, CFSTR("About Rusty Backup"),
        0, 'abou', NULL);
    InsertMenu(appleMenu, 0);

    CreateNewMenu(1, 0, &fileMenu);
    SetMenuTitleWithCFString(fileMenu, CFSTR("File"));
    AppendMenuItemTextWithCFString(fileMenu, CFSTR("Quit"),
        kMenuItemAttrSeparator, 0, NULL);
    AppendMenuItemTextWithCFString(fileMenu, CFSTR("Quit Rusty Backup"),
        0, kHICommandQuit, NULL);
    SetMenuItemCommandKey(fileMenu, 2, false, 'Q');
    InsertMenu(fileMenu, 0);

    DrawMenuBar();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Find CLI binary */
    find_cli_binary();

    /* Install event handlers */
    EventTypeSpec cmdEvents[] = {
        { kEventClassCommand, kEventCommandProcess }
    };
    InstallApplicationEventHandler(
        NewEventHandlerUPP(handle_command),
        1, cmdEvents, NULL, NULL);

    EventTypeSpec tabEvents[] = {
        { kEventClassControl, kEventControlValueFieldChanged }
    };
    /* We'll install this after window creation */

    /* Create UI */
    create_menus();
    create_main_window();

    /* Install tab change handler on the window */
    InstallWindowEventHandler(gMainWindow,
        NewEventHandlerUPP(handle_tab_change),
        1, tabEvents, NULL, NULL);

    /* Run */
    RunApplicationEventLoop();

    return 0;
}
