#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <direct.h>

#include "utils.h"
#include "handlers.h"

#define STB_HTTP_PORT_MAX_ATTEMPTS 10
#define HTTP_BUF_SIZE 65536
#define MAX_SEARCH_HISTORY 50

int g_http_port = 0;
static wchar_t *g_exe_dir = NULL;
static HANDLE g_http_thread = NULL;
static volatile LONG g_http_stop = 0;
static SOCKET g_listen_socket = INVALID_SOCKET;

static char *StrDup(const char *s) {
    size_t l = strlen(s) + 1;
    char *d = (char *)malloc(l);
    if (d) memcpy(d, s, l);
    return d;
}

static int StrCaseCmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (*a) ? 1 : (*b) ? -1 : 0;
}

static int StrCaseCmpN(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!a[i] && !b[i]) return 0;
        if (!a[i]) return -1;
        if (!b[i]) return 1;
        int ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
    }
    return 0;
}

static int IsDigit(int c) { return c >= '0' && c <= '9'; }

static int IsSpace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

char *WideToUtf8(const wchar_t *w) {
    if (!w) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char *r = (char *)malloc((size_t)len);
    if (!r) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, r, len, NULL, NULL);
    return r;
}

wchar_t *Utf8ToWide(const char *u) {
    if (!u) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, u, -1, NULL, 0);
    if (len <= 0) return NULL;
    wchar_t *r = (wchar_t *)malloc((size_t)len * sizeof(wchar_t));
    if (!r) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, u, -1, r, len);
    return r;
}

static char *JsonUnescape(const char *s, size_t len) {
    char *r = (char *)malloc(len + 1);
    if (!r) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\\' && i + 1 < len) {
            i++;
            switch (s[i]) {
                case '"':  r[j++] = '"'; break;
                case '\\': r[j++] = '\\'; break;
                case '/':  r[j++] = '/'; break;
                case 'n':  r[j++] = '\n'; break;
                case 'r':  r[j++] = '\r'; break;
                case 't':  r[j++] = '\t'; break;
                case 'b':  r[j++] = '\b'; break;
                case 'f':  r[j++] = '\f'; break;
                case 'u': {
                    unsigned int cp = 0;
                    for (int k = 0; k < 4 && i + 1 + k < len; k++) {
                        char c = s[i + 1 + k];
                        cp <<= 4;
                        if (c >= '0' && c <= '9') cp |= (c - '0');
                        else if (c >= 'a' && c <= 'f') cp |= (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') cp |= (c - 'A' + 10);
                    }
                    if (cp < 0x80) { r[j++] = (char)cp; }
                    else if (cp < 0x800) { r[j++] = (char)(0xC0 | (cp >> 6)); r[j++] = (char)(0x80 | (cp & 0x3F)); }
                    else { r[j++] = (char)(0xE0 | (cp >> 12)); r[j++] = (char)(0x80 | ((cp >> 6) & 0x3F)); r[j++] = (char)(0x80 | (cp & 0x3F)); }
                    i += 4;
                    break;
                }
                default: r[j++] = s[i]; break;
            }
        } else {
            r[j++] = s[i];
        }
    }
    r[j] = 0;
    return r;
}

char *GetJsonStr(const char *json, const char *key) {
    if (!json || !key) return NULL;
    size_t keylen = strlen(key);
    char *pattern = (char *)malloc(keylen + 4);
    if (!pattern) return NULL;
    pattern[0] = '"';
    memcpy(pattern + 1, key, keylen);
    pattern[keylen + 1] = '"';
    pattern[keylen + 2] = ':';
    pattern[keylen + 3] = '"';
    pattern[keylen + 4] = 0;
    const char *p = strstr(json, pattern);
    free(pattern);
    if (!p) return NULL;
    p += keylen + 4;
    const char *end = p;
    while (*end && !(*end == '"' && (end == p || *(end - 1) != '\\'))) end++;
    if (!*end) return NULL;
    return JsonUnescape(p, (size_t)(end - p));
}

int GetJsonInt(const char *json, const char *key) {
    if (!json || !key) return 0;
    size_t keylen = strlen(key);
    char *pattern = (char *)malloc(keylen + 4);
    if (!pattern) return 0;
    pattern[0] = '"';
    memcpy(pattern + 1, key, keylen);
    pattern[keylen + 1] = '"';
    pattern[keylen + 2] = ':';
    pattern[keylen + 3] = 0;
    const char *p = strstr(json, pattern);
    free(pattern);
    if (!p) return 0;
    p += keylen + 3;
    while (*p && IsSpace(*p)) p++;
    int sign = 1, val = 0;
    if (*p == '-') { sign = -1; p++; }
    while (*p && IsDigit(*p)) { val = val * 10 + (*p - '0'); p++; }
    return val * sign;
}

char *GetJsonRaw(const char *json, const char *key) {
    if (!json || !key) return NULL;
    size_t keylen = strlen(key);
    char *pattern = (char *)malloc(keylen + 4);
    if (!pattern) return NULL;
    pattern[0] = '"';
    memcpy(pattern + 1, key, keylen);
    pattern[keylen + 1] = '"';
    pattern[keylen + 2] = ':';
    pattern[keylen + 3] = 0;
    const char *p = strstr(json, pattern);
    free(pattern);
    if (!p) return NULL;
    p += keylen + 3;
    while (*p && IsSpace(*p)) p++;
    if (!*p) return NULL;
    const char *start = p;
    if (*p == '"') {
        p++;
        while (*p && !(*p == '"' && *(p - 1) != '\\')) p++;
        if (*p) p++;
    } else if (*p == '{' || *p == '[') {
        char open = *p;
        char close = (open == '{') ? '}' : ']';
        int depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '"') {
                p++;
                while (*p && !(*p == '"' && *(p - 1) != '\\')) p++;
            } else if (*p == open) depth++;
            else if (*p == close) depth--;
            if (depth > 0) p++;
        }
        if (*p) p++;
    } else {
        while (*p && !IsSpace(*p) && *p != ',' && *p != '}' && *p != ']') p++;
    }
    if (p == start) return NULL;
    char *r = (char *)malloc((size_t)(p - start) + 1);
    if (!r) return NULL;
    memcpy(r, start, (size_t)(p - start));
    r[p - start] = 0;
    return r;
}

static char *JsonEscape(const char *value, size_t *olen) {
    if (!value) { *olen = 0; return NULL; }
    size_t cap = strlen(value) * 2 + 3;
    char *r = (char *)malloc(cap);
    if (!r) return NULL;
    size_t j = 0;
    r[j++] = '"';
    for (const char *p = value; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') { r[j++] = '\\'; r[j++] = '"'; }
        else if (c == '\\') { r[j++] = '\\'; r[j++] = '\\'; }
        else if (c == '/') { r[j++] = '\\'; r[j++] = '/'; }
        else if (c == '\n') { r[j++] = '\\'; r[j++] = 'n'; }
        else if (c == '\r') { r[j++] = '\\'; r[j++] = 'r'; }
        else if (c == '\t') { r[j++] = '\\'; r[j++] = 't'; }
        else if (c == '\b') { r[j++] = '\\'; r[j++] = 'b'; }
        else if (c == '\f') { r[j++] = '\\'; r[j++] = 'f'; }
        else if (c < 0x20) {
            if (j + 6 >= cap) { cap += 8; r = (char *)realloc(r, cap); if (!r) return NULL; }
            j += sprintf(r + j, "\\u%04x", c);
        } else if (c >= 0x7F) {
            if (j + 6 >= cap) { cap += 8; r = (char *)realloc(r, cap); if (!r) return NULL; }
            j += sprintf(r + j, "\\u%04x", c);
        } else {
            r[j++] = c;
        }
        if (j + 6 >= cap) { cap += 8; r = (char *)realloc(r, cap); if (!r) return NULL; }
    }
    r[j++] = '"';
    r[j] = 0;
    *olen = j;
    return r;
}

char *JsonStr(const char *value) {
    size_t len = 0;
    return JsonEscape(value, &len);
}

char *JsonInt(int value) {
    char buf[32];
    sprintf(buf, "%d", value);
    return StrDup(buf);
}

char *JsonObj(const char **keys, const char **vals, int n) {
    size_t cap = 256;
    char *r = (char *)malloc(cap);
    if (!r) return NULL;
    size_t j = 0;
    r[j++] = '{';
    for (int i = 0; i < n; i++) {
        if (i > 0) r[j++] = ',';
        size_t klen = 0;
        char *ek = JsonEscape(keys[i], &klen);
        if (ek) {
            while (j + klen + 1 >= cap) { cap *= 2; r = (char *)realloc(r, cap); if (!r) { free(ek); return NULL; } }
            memcpy(r + j, ek, klen);
            j += klen;
            free(ek);
        }
        r[j++] = ':';
        if (vals[i]) {
            size_t vlen = strlen(vals[i]);
            while (j + vlen + 1 >= cap) { cap *= 2; r = (char *)realloc(r, cap); if (!r) return NULL; }
            memcpy(r + j, vals[i], vlen);
            j += vlen;
        } else {
            while (j + 5 >= cap) { cap *= 2; r = (char *)realloc(r, cap); if (!r) return NULL; }
            memcpy(r + j, "null", 4);
            j += 4;
        }
    }
    r[j++] = '}';
    r[j] = 0;
    return r;
}

char *JsonArr(const char **items, int n) {
    size_t cap = 128;
    char *r = (char *)malloc(cap);
    if (!r) return NULL;
    size_t j = 0;
    r[j++] = '[';
    for (int i = 0; i < n; i++) {
        if (i > 0) r[j++] = ',';
        if (items[i]) {
            size_t ilen = strlen(items[i]);
            while (j + ilen + 1 >= cap) { cap *= 2; r = (char *)realloc(r, cap); if (!r) return NULL; }
            memcpy(r + j, items[i], ilen);
            j += ilen;
        }
    }
    r[j++] = ']';
    r[j] = 0;
    return r;
}

static int ParseUrl(const char *url, char *scheme, int scheme_max,
                    char *host, int host_max, int *port, char *path, int path_max) {
    if (!url) return -1;
    const char *p = strstr(url, "://");
    if (!p) return -1;
    size_t slen = (size_t)(p - url);
    if (slen >= (size_t)scheme_max) return -1;
    memcpy(scheme, url, slen);
    scheme[slen] = 0;
    p += 3;
    const char *host_start = p;
    while (*p && *p != ':' && *p != '/' && *p != '?') p++;
    size_t hlen = (size_t)(p - host_start);
    if (hlen >= (size_t)host_max) return -1;
    memcpy(host, host_start, hlen);
    host[hlen] = 0;
    if (*p == ':') {
        p++;
        *port = 0;
        while (*p && IsDigit(*p)) { *port = *port * 10 + (*p - '0'); p++; }
    } else {
        *port = (StrCaseCmp(scheme, "https") == 0) ? 443 : 80;
    }
    if (!*p || *p == '?') {
        path[0] = '/';
        path[1] = 0;
    } else {
        size_t plen = 0;
        const char *pp = p;
        while (*pp && *pp != '?') { pp++; plen++; }
        if (plen >= (size_t)path_max) return -1;
        memcpy(path, p, plen);
        path[plen] = 0;
    }
    return 0;
}

char *HttpReq(const char *method, const char *url, const char *headers, const char *body, int *status, int timeout_ms) {
    if (!method || !url) return NULL;
    char scheme[16], host[256], path[4096];
    int port = 0;
    if (ParseUrl(url, scheme, 16, host, 256, &port, path, 4096) != 0) return NULL;
    HINTERNET hSession = WinHttpOpen(L"SteamToolsLua/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return NULL;
    wchar_t *whost = Utf8ToWide(host);
    if (!whost) { WinHttpCloseHandle(hSession); return NULL; }
    HINTERNET hConnect = WinHttpConnect(hSession, whost, (INTERNET_PORT)port, 0);
    free(whost);
    if (!hConnect) { WinHttpCloseHandle(hSession); return NULL; }
    wchar_t *wpath = Utf8ToWide(path);
    wchar_t *wmethod = Utf8ToWide(method);
    if (!wpath || !wmethod) { free(wpath); free(wmethod); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return NULL; }
    BOOL secure = (StrCaseCmp(scheme, "https") == 0) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wmethod, wpath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure);
    free(wpath);
    free(wmethod);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return NULL; }
    if (timeout_ms > 0) {
        WinHttpSetTimeouts(hRequest, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    }
    if (headers && headers[0]) {
        wchar_t *wheaders = Utf8ToWide(headers);
        if (wheaders) {
            WinHttpAddRequestHeaders(hRequest, wheaders, (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
            free(wheaders);
        }
    }
    wchar_t *wbody = NULL;
    DWORD body_len = 0;
    if (body) {
        wbody = Utf8ToWide(body);
        if (wbody) body_len = (DWORD)wcslen(wbody);
    }
    BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)wbody, body_len, body_len, 0);
    if (wbody) free(wbody);
    if (!sent) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return NULL; }
    BOOL recv = WinHttpReceiveResponse(hRequest, NULL);
    if (!recv) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return NULL; }
    if (status) {
        DWORD scode = 0, scode_size = sizeof(scode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &scode, &scode_size, NULL);
        *status = (int)scode;
    }
    DWORD total = 0, cap = 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return NULL; }
    DWORD read = 0;
    while (WinHttpReadData(hRequest, buf + total, cap - total - 1, &read)) {
        if (read == 0) break;
        total += read;
        if (total + 1024 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return NULL; }
            buf = nb;
        }
    }
    buf[total] = 0;
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return buf;
}

static char *ReadFileWithSize(const char *path, DWORD *size_out) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { if (size_out) *size_out = 0; return NULL; }
    DWORD size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE) { CloseHandle(h); if (size_out) *size_out = 0; return NULL; }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { CloseHandle(h); if (size_out) *size_out = 0; return NULL; }
    DWORD read = 0;
    if (!ReadFile(h, buf, size, &read, NULL)) { free(buf); CloseHandle(h); if (size_out) *size_out = 0; return NULL; }
    buf[read] = 0;
    CloseHandle(h);
    if (size_out) *size_out = read;
    return buf;
}

char *FileRead(const char *path) {
    DWORD size = 0;
    return ReadFileWithSize(path, &size);
}

int FileWrite(const char *path, const char *content) {
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD written = 0;
    DWORD len = (DWORD)strlen(content);
    BOOL ok = WriteFile(h, content, len, &written, NULL);
    CloseHandle(h);
    return ok ? 0 : -1;
}

int FileExists(const char *path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
}

char *GetDir(const char *path) {
    if (!path || !*path) return NULL;
    const char *p = path + strlen(path) - 1;
    while (p >= path && *p != '\\' && *p != '/') p--;
    if (p < path) return NULL;
    size_t len = (size_t)(p - path);
    if (len == 0) return StrDup(".");
    char *r = (char *)malloc(len + 1);
    if (!r) return NULL;
    memcpy(r, path, len);
    r[len] = 0;
    return r;
}

int MakeDir(const char *path) {
    if (!path || !*path) return -1;
    char tmp[1024];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);
    for (size_t i = 0; tmp[i]; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            tmp[i] = 0;
            if (tmp[0] && strlen(tmp) > 0) {
                CreateDirectoryA(tmp, NULL);
            }
            tmp[i] = '\\';
        }
    }
    if (CreateDirectoryA(tmp, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    return -1;
}

char **ListDirFiles(const char *path, const char *ext, int *count) {
    *count = 0;
    if (!path) return NULL;
    char pattern[1024];
    if (ext && ext[0]) {
        snprintf(pattern, sizeof(pattern), "%s\\*%s", path, ext);
    } else {
        snprintf(pattern, sizeof(pattern), "%s\\*", path);
    }
    int cap = 64;
    char **files = (char **)malloc((size_t)cap * sizeof(char *));
    if (!files) return NULL;
    int n = 0;
    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(pattern, &ffd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (n >= cap) { cap *= 2; char **nf = (char **)realloc(files, (size_t)cap * sizeof(char *)); if (!nf) break; files = nf; }
            files[n] = StrDup(ffd.cFileName);
            if (files[n]) n++;
        } while (FindNextFileA(h, &ffd));
        FindClose(h);
    }
    if (n == 0) { free(files); return NULL; }
    *count = n;
    return files;
}

char *PathJoin(const char *a, const char *b) {
    if (!a && !b) return NULL;
    if (!a) return StrDup(b);
    if (!b) return StrDup(a);
    size_t la = strlen(a), lb = strlen(b);
    char *r = (char *)malloc(la + lb + 2);
    if (!r) return NULL;
    memcpy(r, a, la);
    if (la > 0 && a[la - 1] != '\\' && a[la - 1] != '/') r[la++] = '\\';
    memcpy(r + la, b, lb + 1);
    return r;
}

int CopyFileSimple(const char *src, const char *dst) {
    return CopyFileA(src, dst, FALSE) ? 0 : -1;
}

int ExtractZip(const char *archive, const char *dest) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "tar -xf \"%s\" -C \"%s\"", archive, dest);
    return (system(cmd) == 0) ? 0 : -1;
}

int ExtractRar(const char *archive, const char *dest, const char *password) {
    char cmd[4096];
    if (password && password[0]) {
        snprintf(cmd, sizeof(cmd), "unrar x -p\"%s\" -y \"%s\" \"%s\"\\", password, archive, dest);
    } else {
        snprintf(cmd, sizeof(cmd), "unrar x -p- -y \"%s\" \"%s\"\\", archive, dest);
    }
    return (system(cmd) == 0) ? 0 : -1;
}

int Extract7z(const char *archive, const char *dest) {
    const char *paths[] = {
        "C:\\Program Files\\7-Zip\\7z.exe",
        "C:\\Program Files (x86)\\7-Zip\\7z.exe",
        NULL
    };
    char tool[MAX_PATH] = {0};
    int found = 0;
    for (int i = 0; paths[i]; i++) {
        if (GetFileAttributesA(paths[i]) != INVALID_FILE_ATTRIBUTES) {
            strcpy(tool, paths[i]);
            found = 1;
            break;
        }
    }
    char cmd[4096];
    if (found) {
        snprintf(cmd, sizeof(cmd), "\"%s\" x -y \"%s\" -o\"%s\"", tool, archive, dest);
    } else {
        snprintf(cmd, sizeof(cmd), "7z x -y \"%s\" -o\"%s\"", archive, dest);
    }
    return (system(cmd) == 0) ? 0 : -1;
}

int ExtractArchive(const char *archive, const char *dest, const char *password) {
    const char *ext = strrchr(archive, '.');
    if (!ext) return -1;
    if (StrCaseCmp(ext, ".zip") == 0) return ExtractZip(archive, dest);
    if (StrCaseCmp(ext, ".rar") == 0) return ExtractRar(archive, dest, password);
    if (StrCaseCmp(ext, ".7z") == 0) return Extract7z(archive, dest);
    return -1;
}

char *RegReadStr(HKEY hive, const char *subkey, const char *name) {
    HKEY hKey;
    if (RegOpenKeyExA(hive, subkey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return NULL;
    DWORD type = 0, size = 0;
    if (RegQueryValueExA(hKey, name, NULL, &type, NULL, &size) != ERROR_SUCCESS || size == 0) {
        RegCloseKey(hKey);
        return NULL;
    }
    char *val = (char *)malloc((size_t)size + 1);
    if (!val) { RegCloseKey(hKey); return NULL; }
    if (RegQueryValueExA(hKey, name, NULL, &type, (LPBYTE)val, &size) != ERROR_SUCCESS) {
        free(val); RegCloseKey(hKey); return NULL;
    }
    val[size] = 0;
    RegCloseKey(hKey);
    return val;
}

int RegWriteStr(HKEY hive, const char *subkey, const char *name, const char *value) {
    HKEY hKey;
    DWORD disp = 0;
    if (RegCreateKeyExA(hive, subkey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, &disp) != ERROR_SUCCESS)
        return -1;
    DWORD len = (DWORD)strlen(value);
    LONG ret = RegSetValueExA(hKey, name, 0, REG_SZ, (const BYTE *)value, len + 1);
    RegCloseKey(hKey);
    return (ret == ERROR_SUCCESS) ? 0 : -1;
}

int RunProcess(const char *exe, const char *args) {
    char cmdline[4096];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" %s", exe, args ? args : "");
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 0;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)ec;
}

static int VersionCmp(const char *v1, const char *v2) {
    while (*v1 || *v2) {
        int n1 = 0, n2 = 0;
        while (*v1 && IsDigit(*v1)) { n1 = n1 * 10 + (*v1 - '0'); v1++; }
        while (*v2 && IsDigit(*v2)) { n2 = n2 * 10 + (*v2 - '0'); v2++; }
        if (n1 != n2) return n1 - n2;
        if (*v1 == '.') v1++;
        if (*v2 == '.') v2++;
    }
    return 0;
}

char *CheckUpdate(const char *current_version) {
    int sc = 0;
    char *resp = HttpReq("GET", "https://raw.githubusercontent.com/tttaaahhhaaa/SteamToolsLua/master/latest_version.txt", NULL, NULL, &sc, 10000);
    if (!resp || sc != 200) {
        free(resp);
        return StrDup("{\"has_update\":false}");
    }
    char *start = resp;
    while (*start && IsSpace(*start)) start++;
    if (start != resp) memmove(resp, start, strlen(start) + 1);
    char *end = resp + strlen(resp) - 1;
    while (end >= resp && IsSpace(*end)) { *end = 0; end--; }
    int cmp = VersionCmp(resp, current_version);
    if (cmp > 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "{\"has_update\":true,\"latest\":\"%s\"}", resp);
        free(resp);
        return StrDup(buf);
    }
    free(resp);
    return StrDup("{\"has_update\":false}");
}

int StartUpdate(const char *download_url) {
    int sc = 0;
    char *data = HttpReq("GET", download_url, NULL, NULL, &sc, 60000);
    if (!data || sc != 200) { free(data); return -1; }
    const char *fname = strrchr(download_url, '/');
    if (!fname) fname = download_url; else fname++;
    wchar_t exe_path[1024];
    swprintf(exe_path, 1024, L"%ls\\%hs", g_exe_dir ? g_exe_dir : L".", fname);
    char *exe_path_u = WideToUtf8(exe_path);
    if (!exe_path_u) { free(data); return -1; }
    if (FileWrite(exe_path_u, data) != 0) { free(data); free(exe_path_u); return -1; }
    free(data);
    wchar_t info_path[1024];
    swprintf(info_path, 1024, L"%ls\\.update_info.txt", g_exe_dir ? g_exe_dir : L".");
    char *info_path_u = WideToUtf8(info_path);
    if (info_path_u) {
        char info[2048];
        snprintf(info, sizeof(info), "%s\n%s\n", fname, download_url);
        FileWrite(info_path_u, info);
        free(info_path_u);
    }
    free(exe_path_u);
    wchar_t wcmd[4096];
    swprintf(wcmd, 4096, L"\"%s\" --updated", exe_path);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    CreateProcessW(NULL, wcmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    ExitProcess(0);
    return 0;
}

static char *GetSearchHistoryPath(void) {
    if (!g_exe_dir) return NULL;
    wchar_t wpath[1024];
    swprintf(wpath, 1024, L"%ls\\search_history.json", g_exe_dir);
    return WideToUtf8(wpath);
}

char **GetSearchHistory(int *count) {
    *count = 0;
    char *path = GetSearchHistoryPath();
    if (!path) return NULL;
    char *content = FileRead(path);
    free(path);
    if (!content) return NULL;
    const char *p = content;
    while (*p && IsSpace(*p)) p++;
    if (*p != '[') { free(content); return NULL; }
    p++;
    int cap = 16, n = 0;
    char **arr = (char **)malloc((size_t)cap * sizeof(char *));
    if (!arr) { free(content); return NULL; }
    while (*p && *p != ']') {
        while (*p && IsSpace(*p) && *p != ']') p++;
        if (*p == ']') break;
        if (*p == ',') { p++; continue; }
        if (*p == '"') {
            p++;
            const char *start = p;
            while (*p && !(*p == '"' && *(p - 1) != '\\')) p++;
            if (*p == '"') {
                char *val = JsonUnescape(start, (size_t)(p - start));
                if (val) {
                    if (n >= cap) { cap *= 2; char **na = (char **)realloc(arr, (size_t)cap * sizeof(char *)); if (!na) break; arr = na; }
                    arr[n++] = val;
                }
                p++;
            }
        } else {
            p++;
        }
    }
    free(content);
    if (n == 0) { free(arr); return NULL; }
    *count = n;
    return arr;
}

int AddSearchHistory(const char *term) {
    if (!term || !*term) return -1;
    int count = 0;
    char **existing = GetSearchHistory(&count);
    int start_idx = 0;
    if (count >= MAX_SEARCH_HISTORY) start_idx = count - MAX_SEARCH_HISTORY + 1;
    size_t cap = 4096;
    char *json = (char *)malloc(cap);
    if (!json) { if (existing) { for (int i = 0; i < count; i++) free(existing[i]); free(existing); } return -1; }
    size_t j = 0;
    json[j++] = '[';
    int first = 1;
    for (int i = start_idx; i < count; i++) {
        if (existing && existing[i]) {
            if (!first) { json[j++] = ','; if (j + 1 >= cap) { cap *= 2; json = (char *)realloc(json, cap); if (!json) { for (int k = 0; k < count; k++) free(existing[k]); free(existing); return -1; } } }
            first = 0;
            size_t klen = 0;
            char *ek = JsonEscape(existing[i], &klen);
            if (ek) {
                while (j + klen + 1 >= cap) { cap *= 2; json = (char *)realloc(json, cap); if (!json) { free(ek); for (int k = 0; k < count; k++) free(existing[k]); free(existing); return -1; } }
                memcpy(json + j, ek, klen); j += klen; free(ek);
            }
        }
    }
    if (!first) { json[j++] = ','; if (j + 1 >= cap) { cap *= 2; json = (char *)realloc(json, cap); if (!json) { for (int k = 0; k < count; k++) free(existing[k]); free(existing); return -1; } } }
    size_t tlen = 0;
    char *et = JsonEscape(term, &tlen);
    if (et) {
        while (j + tlen + 1 >= cap) { cap *= 2; json = (char *)realloc(json, cap); if (!json) { free(et); for (int k = 0; k < count; k++) free(existing[k]); free(existing); return -1; } }
        memcpy(json + j, et, tlen); j += tlen; free(et);
    }
    json[j++] = ']';
    json[j] = 0;
    if (existing) { for (int i = 0; i < count; i++) free(existing[i]); free(existing); }
    char *path = GetSearchHistoryPath();
    if (!path) { free(json); return -1; }
    int r = FileWrite(path, json);
    free(json);
    free(path);
    return r;
}

static const char *GetMimeType(const char *ext) {
    if (!ext) return "application/octet-stream";
    if (StrCaseCmp(ext, ".html") == 0 || StrCaseCmp(ext, ".htm") == 0) return "text/html";
    if (StrCaseCmp(ext, ".js") == 0) return "application/javascript";
    if (StrCaseCmp(ext, ".css") == 0) return "text/css";
    if (StrCaseCmp(ext, ".png") == 0) return "image/png";
    if (StrCaseCmp(ext, ".ico") == 0) return "image/x-icon";
    if (StrCaseCmp(ext, ".svg") == 0) return "image/svg+xml";
    if (StrCaseCmp(ext, ".json") == 0) return "application/json";
    if (StrCaseCmp(ext, ".txt") == 0) return "text/plain";
    if (StrCaseCmp(ext, ".xml") == 0) return "application/xml";
    return "application/octet-stream";
}

static int SendAll(SOCKET s, const char *data, int len, int timeout_ms) {
    (void)timeout_ms;
    int total = 0;
    while (total < len) {
        int n = send(s, data + total, len - total, 0);
        if (n == SOCKET_ERROR) return -1;
        total += n;
    }
    return total;
}

static void SendResponse(SOCKET client, int status, const char *status_text,
                         const char *content_type, const char *data, DWORD data_len,
                         int is_binary) {
    (void)is_binary;
    if (!data_len) data_len = data ? (DWORD)strlen(data) : 0;
    char header[1024];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status, status_text, content_type, (unsigned long)data_len);
    send(client, header, n, 0);
    if (data && data_len > 0) {
        send(client, data, (int)data_len, 0);
    }
}

static int RecvLine(SOCKET s, char *buf, int cap) {
    int total = 0;
    while (total < cap - 1) {
        char c;
        int n = recv(s, &c, 1, 0);
        if (n <= 0) return -1;
        if (c == '\r') continue;
        if (c == '\n') { buf[total] = 0; return total; }
        buf[total++] = c;
    }
    buf[total] = 0;
    return total;
}

static char *RecvUntil(SOCKET s, char *buf, int cap, const char *delim) {
    int total = 0;
    int dlen = (int)strlen(delim);
    while (total < cap - 1) {
        char c;
        int n = recv(s, &c, 1, 0);
        if (n <= 0) break;
        buf[total++] = c;
        buf[total] = 0;
        if (total >= dlen && strcmp(buf + total - dlen, delim) == 0) break;
    }
    buf[total] = 0;
    return buf;
}

static int RecvAll(SOCKET s, char *buf, int len) {
    int total = 0;
    while (total < len) {
        int n = recv(s, buf + total, len - total, 0);
        if (n <= 0) return -1;
        total += n;
    }
    return total;
}

static void HandleClient(SOCKET client) {
    char buf[HTTP_BUF_SIZE];
    if (RecvLine(client, buf, sizeof(buf)) < 0) return;
    char method[64], path[4096];
    if (sscanf(buf, "%63s %4095s", method, path) < 2) return;
    int is_post = (strcmp(method, "POST") == 0);
    int is_get = (strcmp(method, "GET") == 0);
    char *body = NULL;
    int content_length = 0;
    if (is_post) {
        RecvUntil(client, buf, sizeof(buf), "\r\n\r\n");
        const char *cl = strstr(buf, "Content-Length:");
        if (!cl) cl = strstr(buf, "content-length:");
        if (cl) {
            cl += 15;
            while (*cl && IsSpace(*cl)) cl++;
            content_length = 0;
            while (*cl && IsDigit(*cl)) { content_length = content_length * 10 + (*cl - '0'); cl++; }
        }
        if (content_length > 0 && content_length < HTTP_BUF_SIZE) {
            body = (char *)malloc((size_t)content_length + 1);
            if (body) {
                if (RecvAll(client, body, content_length) == content_length) {
                    body[content_length] = 0;
                } else {
                    free(body);
                    body = NULL;
                }
            }
        }
    } else {
        RecvUntil(client, buf, sizeof(buf), "\r\n\r\n");
    }
    char *qmark = strchr(path, '?');
    if (qmark) *qmark = 0;
    if (strstr(path, "..")) { SendResponse(client, 403, "Forbidden", "text/plain", "Forbidden", 0, 0); free(body); return; }
    if (is_post && strncmp(path, "/api/", 5) == 0) {
        const char *cmd = path + 5;
        char *resp = DispatchCommand(cmd, body);
        if (!resp) resp = StrDup("{\"error\":\"unknown_command\"}");
        SendResponse(client, 200, "OK", "application/json", resp, 0, 0);
        free(resp);
    } else if (!is_post && is_get) {
        if (!g_exe_dir) { SendResponse(client, 500, "Internal Server Error", "text/plain", "Server not initialized", 0, 0); free(body); return; }
        char fpath[4096];
        char *d = WideToUtf8(g_exe_dir);
        if (!d) { SendResponse(client, 500, "Internal Server Error", "text/plain", "Path error", 0, 0); free(body); return; }
        int is_root = (path[0] == '/' && !path[1]);
        if (is_root) {
            snprintf(fpath, sizeof(fpath), "%s\\web\\index.html", d);
        } else {
            snprintf(fpath, sizeof(fpath), "%s\\web\\%s", d, path + 1);
        }
        free(d);
        const char *ext = strrchr(fpath, '.');
        const char *mime = GetMimeType(ext);
        DWORD fsize = 0;
        char *fdata = ReadFileWithSize(fpath, &fsize);
        if (fdata) {
            SendResponse(client, 200, "OK", mime, fdata, fsize, 1);
            free(fdata);
        } else {
            SendResponse(client, 404, "Not Found", "text/plain", "Not Found", 0, 0);
        }
    } else {
        SendResponse(client, 404, "Not Found", "text/plain", "Not Found", 0, 0);
    }
    free(body);
}

static DWORD WINAPI HttpServerThread(LPVOID param) {
    SOCKET s = (SOCKET)(intptr_t)param;
    while (!g_http_stop) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET client = accept(s, (struct sockaddr *)&client_addr, &client_len);
        if (client == INVALID_SOCKET) {
            if (g_http_stop) break;
            continue;
        }
        HandleClient(client);
        closesocket(client);
    }
    return 0;
}

void InitUtils(const wchar_t *exe_dir) {
    if (g_exe_dir) free(g_exe_dir);
    if (exe_dir) {
        size_t len = wcslen(exe_dir) + 1;
        g_exe_dir = (wchar_t *)malloc(len * sizeof(wchar_t));
        if (g_exe_dir) wcscpy(g_exe_dir, exe_dir);
    } else {
        g_exe_dir = NULL;
    }
}

void StartHttpServer(const wchar_t *exe_dir) {
    InitUtils(exe_dir);
    if (g_http_thread) return;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { WSACleanup(); return; }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = 0;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s); WSACleanup(); return;
    }
    if (listen(s, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(s); WSACleanup(); return;
    }
    int addrlen = sizeof(addr);
    if (getsockname(s, (struct sockaddr *)&addr, &addrlen) == 0) {
        g_http_port = ntohs(addr.sin_port);
    }
    g_http_stop = 0;
    g_listen_socket = s;
    DWORD tid;
    g_http_thread = CreateThread(NULL, 0, HttpServerThread, (void*)(intptr_t)s, 0, &tid);
}

void StopHttpServer(void) {
    if (!g_http_thread) return;
    g_http_stop = 1;
    if (g_listen_socket != INVALID_SOCKET) {
        shutdown(g_listen_socket, SD_BOTH);
        closesocket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET;
    }
    WaitForSingleObject(g_http_thread, 5000);
    CloseHandle(g_http_thread);
    g_http_thread = NULL;
    WSACleanup();
    g_http_port = 0;
}
