#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <ole2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <shellapi.h>
#include "WebView2.h"
#include "utils.h"
#include "handlers.h"

// WebView2 loader function types
typedef HRESULT (STDAPICALLTYPE *PFN_GETWV2VERSION)(PCWSTR, LPWSTR *);
typedef HRESULT (STDAPICALLTYPE *PFN_CREATEWV2ENV)(PCWSTR, void *, void *);

#define APP_NAME L"Steam Tools Lua"
#define APP_VER  L"1.9.0"
#define VHOST    L"steamtools.local"

static HWND g_hwnd;
static ICoreWebView2 *g_wv;
static ICoreWebView2Controller *g_ctrl;
static wchar_t g_exe_dir[MAX_PATH];

static HMODULE g_wv2_dll;
static PFN_GETWV2VERSION g_GetVersion;
static PFN_CREATEWV2ENV g_CreateEnv;

static void SendToJS(const wchar_t *json) {
    if (g_wv) g_wv->lpVtbl->PostWebMessageAsJson(g_wv, json);
}

// === Load WebView2Loader.dll ===
static int LoadWV2(void) {
    wchar_t p[MAX_PATH];
    swprintf_s(p, MAX_PATH, L"%s\\WebView2Loader.dll", g_exe_dir);
    g_wv2_dll = LoadLibraryW(p);
    if (!g_wv2_dll) return 0;
    g_GetVersion = (PFN_GETWV2VERSION)GetProcAddress(g_wv2_dll, "GetAvailableCoreWebView2BrowserVersionString");
    g_CreateEnv = (PFN_CREATEWV2ENV)GetProcAddress(g_wv2_dll, "CreateCoreWebView2EnvironmentWithOptions");
    return (g_CreateEnv != NULL);
}

// === COM Callback Structs ===

// Environment completed handler
typedef struct {
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl *lpVtbl;
    LONG ref;
    HWND hwnd;
} EnvCb;

// Controller completed handler
typedef struct {
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl *lpVtbl;
    LONG ref;
    HWND hwnd;
} CtrlCb;

// Web message received handler
typedef struct {
    ICoreWebView2WebMessageReceivedEventHandlerVtbl *lpVtbl;
    LONG ref;
} MsgCb;

// New window requested handler
typedef struct {
    ICoreWebView2NewWindowRequestedEventHandlerVtbl *lpVtbl;
    LONG ref;
} NewWinCb;

// === VTable declarations ===
static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl g_env_vtbl;
static ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl g_ctrl_vtbl;
static ICoreWebView2WebMessageReceivedEventHandlerVtbl g_msg_vtbl;
static ICoreWebView2NewWindowRequestedEventHandlerVtbl g_newwin_vtbl;

// Generic Qi/AddRef/Release — cast to (void*) for vtable assignment
static HRESULT STDMETHODCALLTYPE Qi_Gen(void *self, REFIID riid, void **ppv) { *ppv = NULL; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE AR_Gen(void *self) { return InterlockedIncrement(&((LONG*)self)[1]); }
static ULONG STDMETHODCALLTYPE RL_Gen(void *self) {
    LONG r = InterlockedDecrement(&((LONG*)self)[1]);
    if (r <= 0) free(self);
    return r;
}

// === Environment handler invoke ===
static HRESULT STDMETHODCALLTYPE EnvCb_Invoke(EnvCb *self, HRESULT res, ICoreWebView2Environment *env) {
    if (FAILED(res)) {
        // Fallback: open browser
        wchar_t url[256];
        swprintf_s(url, 256, L"http://127.0.0.1:%d", g_http_port);
        ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
        return res;
    }
    CtrlCb *cb = calloc(1, sizeof(CtrlCb));
    cb->lpVtbl = &g_ctrl_vtbl;
    cb->ref = 1;
    cb->hwnd = self->hwnd;
    env->lpVtbl->CreateCoreWebView2Controller(env, self->hwnd, (ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*)cb);
    return S_OK;
}

// === Controller handler invoke ===
static HRESULT STDMETHODCALLTYPE CtrlCb_Invoke(CtrlCb *self, HRESULT res, ICoreWebView2Controller *ctrl) {
    if (FAILED(res)) { PostQuitMessage(1); return res; }
    g_ctrl = ctrl;
    g_ctrl->lpVtbl->get_CoreWebView2(g_ctrl, &g_wv);

    if (g_wv) {
        ICoreWebView2Settings *s;
        g_wv->lpVtbl->get_Settings(g_wv, &s);
        if (s) {
            s->lpVtbl->put_IsScriptEnabled(s, TRUE);
            s->lpVtbl->put_AreDefaultScriptDialogsEnabled(s, TRUE);
            s->lpVtbl->put_IsWebMessageEnabled(s, TRUE);
            s->lpVtbl->Release(s);
        }

        MsgCb *mc = calloc(1, sizeof(MsgCb));
        mc->lpVtbl = &g_msg_vtbl; mc->ref = 1;
        EventRegistrationToken tok;
        g_wv->lpVtbl->add_WebMessageReceived(g_wv, (ICoreWebView2WebMessageReceivedEventHandler*)mc, &tok);

        NewWinCb *nw = calloc(1, sizeof(NewWinCb));
        nw->lpVtbl = &g_newwin_vtbl; nw->ref = 1;
        g_wv->lpVtbl->add_NewWindowRequested(g_wv, (ICoreWebView2NewWindowRequestedEventHandler*)nw, &tok);

        // Navigate to HTTP server
        wchar_t url[256];
        swprintf_s(url, 256, L"http://127.0.0.1:%d/index.html", g_http_port);
        g_wv->lpVtbl->Navigate(g_wv, url);
    }

    ShowWindow(self->hwnd, SW_SHOW);
    SetForegroundWindow(self->hwnd);
    SetFocus(self->hwnd);
    return S_OK;
}

// === Web message received ===
static HRESULT STDMETHODCALLTYPE MsgCb_Invoke(MsgCb *self, ICoreWebView2 *wv, ICoreWebView2WebMessageReceivedEventArgs *a) {
    LPWSTR msg = NULL;
    a->lpVtbl->TryGetWebMessageAsString(a, &msg);
    if (msg) {
        // Dispatch
        char *req = WideToUtf8(msg);
        CoTaskMemFree(msg);
        if (!req) return S_OK;

        char *cmd = GetJsonStr(req, "cmd");
        int id = GetJsonInt(req, "id");
        char *args = GetJsonRaw(req, "args");

        char *res = DispatchCommand(cmd, args);
        if (res) {
            char buf[131072];
            int n = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "{\"id\":%d,\"result\":%s}", id, res);
            if (n > 0) {
                wchar_t *w = Utf8ToWide(buf);
                if (w) { SendToJS(w); free(w); }
            }
            free(res);
        }
        free(req); free(cmd); free(args);
    }
    return S_OK;
}

// === New window request (open in external browser) ===
static HRESULT STDMETHODCALLTYPE NewWinCb_Invoke(NewWinCb *self, ICoreWebView2 *wv, ICoreWebView2NewWindowRequestedEventArgs *a) {
    LPWSTR uri = NULL;
    a->lpVtbl->get_Uri(a, &uri);
    if (uri) {
        ShellExecuteW(NULL, L"open", uri, NULL, NULL, SW_SHOWNORMAL);
        CoTaskMemFree(uri);
    }
    a->lpVtbl->put_Handled(a, TRUE);
    return S_OK;
}

// === Initialize vtables ===
static void InitVtables(void) {
    g_env_vtbl.QueryInterface = (void*)Qi_Gen;
    g_env_vtbl.AddRef = (void*)AR_Gen;
    g_env_vtbl.Release = (void*)RL_Gen;
    g_env_vtbl.Invoke = (void*)EnvCb_Invoke;

    g_ctrl_vtbl.QueryInterface = (void*)Qi_Gen;
    g_ctrl_vtbl.AddRef = (void*)AR_Gen;
    g_ctrl_vtbl.Release = (void*)RL_Gen;
    g_ctrl_vtbl.Invoke = (void*)CtrlCb_Invoke;

    g_msg_vtbl.QueryInterface = (void*)Qi_Gen;
    g_msg_vtbl.AddRef = (void*)AR_Gen;
    g_msg_vtbl.Release = (void*)RL_Gen;
    g_msg_vtbl.Invoke = (void*)MsgCb_Invoke;

    g_newwin_vtbl.QueryInterface = (void*)Qi_Gen;
    g_newwin_vtbl.AddRef = (void*)AR_Gen;
    g_newwin_vtbl.Release = (void*)RL_Gen;
    g_newwin_vtbl.Invoke = (void*)NewWinCb_Invoke;
}

// === Init WebView2 ===
static void InitWV2(HWND hwnd) {
    g_hwnd = hwnd;
    if (!LoadWV2()) {
        wchar_t url[256];
        swprintf_s(url, 256, L"http://127.0.0.1:%d", g_http_port);
        ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
        return;
    }
    InitVtables();
    EnvCb *cb = calloc(1, sizeof(EnvCb));
    cb->lpVtbl = &g_env_vtbl; cb->ref = 1; cb->hwnd = hwnd;
    g_CreateEnv(NULL, NULL, (ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*)cb);
}

// === Window Proc ===
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_CREATE:
            InitUtils(g_exe_dir);
            StartHttpServer(g_exe_dir);
            SetExeDir(g_exe_dir);
            InitWV2(hwnd);
            return 0;
        case WM_SIZE:
            if (g_ctrl) {
                RECT r; GetClientRect(hwnd, &r);
                RECT bounds = {0,0,r.right,r.bottom};
                g_ctrl->lpVtbl->put_Bounds(g_ctrl, bounds);
            }
            return 0;
        case WM_DESTROY:
            if (g_ctrl) g_ctrl->lpVtbl->Release(g_ctrl);
            if (g_wv) g_wv->lpVtbl->Release(g_wv);
            StopHttpServer();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

// === WinMain ===
int WINAPI WinMain(HINSTANCE hI, HINSTANCE, LPSTR, int nShow) {
    GetModuleFileNameW(NULL, g_exe_dir, MAX_PATH);
    wchar_t *p = wcsrchr(g_exe_dir, L'\\');
    if (p) *p = L'\0';

    OleInitialize(NULL);

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hI;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = APP_NAME;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, APP_NAME, APP_NAME L" v" APP_VER,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        1400, 900, NULL, NULL, hI, NULL);
    if (!hwnd) return 1;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    OleUninitialize();
    if (g_wv2_dll) FreeLibrary(g_wv2_dll);
    return (int)msg.wParam;
}
