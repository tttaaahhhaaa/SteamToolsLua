#ifndef UTILS_H
#define UTILS_H

#include <windows.h>
#include <wchar.h>



// HTTP server (fallback for when WebView2 is unavailable)
extern int g_http_port;
void StartHttpServer(const wchar_t *exe_dir);
void StopHttpServer(void);
void InitUtils(const wchar_t *exe_dir);

// String conversion
char *WideToUtf8(const wchar_t *w);
wchar_t *Utf8ToWide(const char *u);

// JSON helpers (simple, no full parser)
char *GetJsonStr(const char *json, const char *key);   // returns str value (malloc'd)
int GetJsonInt(const char *json, const char *key);      // returns int value
char *GetJsonRaw(const char *json, const char *key);   // returns raw JSON substring (malloc'd)
char *JsonStr(const char *value);                       // JSON-escaped string with quotes
char *JsonInt(int value);                                // JSON int as string
char *JsonObj(const char **keys, const char **vals, int n); // build JSON object
char *JsonArr(const char **items, int n);               // build JSON array

// WinHTTP client
char *HttpReq(const char *method, const char *url, const char *headers, const char *body, int *status, int timeout_ms);

// File operations
char *FileRead(const char *path);
int FileWrite(const char *path, const char *content);
int FileExists(const char *path);
char *GetDir(const char *path);
int MakeDir(const char *path);
char **ListDirFiles(const char *path, const char *ext, int *count); // files with given extension
char *PathJoin(const char *a, const char *b);
int CopyFileSimple(const char *src, const char *dst);
int ExtractArchive(const char *archive, const char *dest, const char *password);
int ExtractZip(const char *archive, const char *dest);
int ExtractRar(const char *archive, const char *dest, const char *password);
int Extract7z(const char *archive, const char *dest);

// Registry helpers
char *RegReadStr(HKEY hive, const char *subkey, const char *name);
int RegWriteStr(HKEY hive, const char *subkey, const char *name, const char *value);

// Process
int RunProcess(const char *exe, const char *args);

// Auto update
char *CheckUpdate(const char *current_version);
int StartUpdate(const char *download_url);

// Search history
char **GetSearchHistory(int *count);
int AddSearchHistory(const char *term);

#endif
