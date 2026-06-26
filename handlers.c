#include "handlers.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <shlobj.h>

char g_exe_dir_global[MAX_PATH] = {0};

void SetExeDir(const wchar_t *d)
{
    WideCharToMultiByte(CP_UTF8, 0, d, -1, g_exe_dir_global, MAX_PATH, NULL, NULL);
}

// All utils functions declared via utils.h

// Helper: GET request wrapper for HttpReq (6-param)
static char *http_get(const char *url, const char *headers) {
    int status;
    return HttpReq("GET", url, headers, NULL, &status, 15000);
}

static char* my_strdup(const char* s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* c = (char*)malloc(len);
    if (c) memcpy(c, s, len);
    return c;
}

static char* json_escape(const char* s)
{
    if (!s) return NULL;
    size_t len = strlen(s), needed = len + 1;
    size_t i;
    for (i = 0; i < len; i++) {
        char c = s[i];
        if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t' || c == '\b' || c == '\f')
            needed++;
    }
    char* r = (char*)malloc(needed);
    if (!r) return NULL;
    size_t j = 0;
    for (i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
            case '"': r[j++] = '\\'; r[j++] = '"'; break;
            case '\\': r[j++] = '\\'; r[j++] = '\\'; break;
            case '\n': r[j++] = '\\'; r[j++] = 'n'; break;
            case '\r': r[j++] = '\\'; r[j++] = 'r'; break;
            case '\t': r[j++] = '\\'; r[j++] = 't'; break;
            case '\b': r[j++] = '\\'; r[j++] = 'b'; break;
            case '\f': r[j++] = '\\'; r[j++] = 'f'; break;
            default: r[j++] = c; break;
        }
    }
    r[j] = '\0';
    return r;
}

static char* build_json_string(const char* s)
{
    if (!s) return my_strdup("null");
    char* esc = json_escape(s);
    if (!esc) return NULL;
    size_t slen = strlen(esc) + 3;
    char* r = (char*)malloc(slen);
    if (r) snprintf(r, slen, "\"%s\"", esc);
    free(esc);
    return r;
}

static char* url_encode_simple(const char* s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char* r = (char*)malloc(len * 3 + 1);
    if (!r) return NULL;
    size_t j = 0;
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            r[j++] = c;
        } else if (c == ' ') {
            r[j++] = '%'; r[j++] = '2'; r[j++] = '0';
        } else {
            r[j++] = '%'; r[j++] = hex[c >> 4]; r[j++] = hex[c & 0x0F];
        }
    }
    r[j] = '\0';
    return r;
}

static char* read_file(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* content = (char*)malloc((size_t)size + 1);
    if (!content) { fclose(f); return NULL; }
    fread(content, 1, (size_t)size, f);
    content[size] = '\0';
    fclose(f);
    return content;
}

static int write_file(const char* path, const char* content)
{
    FILE* f = fopen(path, "w");
    if (!f) return 0;
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    return written == len;
}

static int ensure_directory(const char* path)
{
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static char* extract_json_str_raw(const char* json, const char* key)
{
    if (!json || !key) return NULL;
    char search[512];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (*p != ':') return NULL;
    p++;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (!*p) return NULL;
    if (*p == '"') {
        p++;
        const char* start = p;
        size_t len = 0;
        while (*p && *p != '"') {
            if (*p == '\\') { p++; if (*p) p++; len++; }
            else { p++; len++; }
        }
        char* val = (char*)malloc(len + 1);
        if (!val) return NULL;
        size_t j = 0;
        p = start;
        while (*p && *p != '"') {
            if (*p == '\\') { p++; if (*p) { val[j++] = *p; p++; } }
            else { val[j++] = *p; p++; }
        }
        val[j] = '\0';
        return val;
    }
    if (*p == '{' || *p == '[') {
        char open = *p;
        char close = (open == '{') ? '}' : ']';
        int depth = 1;
        const char* start = p;
        p++;
        while (*p && depth > 0) {
            if (*p == '\\' && *(p+1)) { p += 2; continue; }
            if (*p == '"') { p++; while (*p && !(*p == '"' && *(p-1) != '\\')) p++; if (*p) p++; continue; }
            if (*p == open) depth++;
            if (*p == close) depth--;
            if (depth > 0) p++;
        }
        if (depth == 0) p++;
        size_t len = (size_t)(p - start);
        char* val = (char*)malloc(len + 1);
        if (!val) return NULL;
        memcpy(val, start, len);
        val[len] = '\0';
        return val;
    }
    {
        const char* start = p;
        while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
        size_t len = (size_t)(p - start);
        char* val = (char*)malloc(len + 1);
        if (!val) return NULL;
        memcpy(val, start, len);
        val[len] = '\0';
        return val;
    }
}

static char* extract_html_between(const char* start, const char* begin_marker, const char* end_marker)
{
    if (!start || !begin_marker || !end_marker) return NULL;
    const char* b = strstr(start, begin_marker);
    if (!b) return NULL;
    b += strlen(begin_marker);
    while (*b && (*b == ' ' || *b == '\t' || *b == '\n' || *b == '\r')) b++;
    const char* e = strstr(b, end_marker);
    if (!e) return NULL;
    size_t len = (size_t)(e - b);
    char* r = (char*)malloc(len + 1);
    if (!r) return NULL;
    memcpy(r, b, len);
    r[len] = '\0';
    return r;
}

static char* extract_attr_value(const char* html, const char* attr)
{
    if (!html || !attr) return NULL;
    char search[256];
    snprintf(search, sizeof(search), "%s=\"", attr);
    const char* p = strstr(html, search);
    if (!p) {
        snprintf(search, sizeof(search), "%s='", attr);
        p = strstr(html, search);
        if (!p) return NULL;
        p += strlen(search);
        const char* end = strchr(p, '\'');
        if (!end) return NULL;
        size_t len = (size_t)(end - p);
        char* r = (char*)malloc(len + 1);
        if (!r) return NULL;
        memcpy(r, p, len);
        r[len] = '\0';
        return r;
    }
    p += strlen(search);
    const char* end = strchr(p, '"');
    if (!end) return NULL;
    size_t len = (size_t)(end - p);
    char* r = (char*)malloc(len + 1);
    if (!r) return NULL;
    memcpy(r, p, len);
    r[len] = '\0';
    return r;
}

static int has_multiplayer_tag(const char* tag)
{
    if (!tag) return 0;
    const char* lower = tag;
    if (strstr(lower, "multiplayer") || strstr(lower, "co-op") || strstr(lower, "multi") ||
        strstr(lower, "pvp") || strstr(lower, "online") || strstr(lower, "coop") ||
        strstr(lower, "massively") || strstr(lower, "battle"))
        return 1;
    return 0;
}

static void trim_trailing(char* s)
{
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
}

char *DispatchCommand(const char *cmd, const char *args)
{
    if (!cmd) return NULL;

    /* ---------- 1. get_config_path ---------- */
    if (strcmp(cmd, "get_config_path") == 0) {
        return build_json_string(g_exe_dir_global);
    }

    /* ---------- 2. open_url ---------- */
    if (strcmp(cmd, "open_url") == 0) {
        if (!args) return build_json_string("error: no args");
        char* url = NULL;
        char* tmp = extract_json_str_raw(args, "url");
        if (tmp) { url = tmp; }
        if (url) {
            ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
            free(url);
        }
        return build_json_string("ok");
    }

    /* ---------- 3. get_settings ---------- */
    if (strcmp(cmd, "get_settings") == 0) {
        char path[MAX_PATH];
        const char* appdata = getenv("APPDATA");
        if (!appdata) return my_strdup("{}");
        snprintf(path, sizeof(path), "%s\\SteamToolsLua\\settings.json", appdata);
        char* content = read_file(path);
        if (!content) return my_strdup("{}");
        return content;
    }

    /* ---------- 4. save_settings ---------- */
    if (strcmp(cmd, "save_settings") == 0) {
        if (!args) return build_json_string("error: no args");
        char* settings = extract_json_str_raw(args, "settings");
        if (!settings) return build_json_string("error: no settings key");
        const char* appdata = getenv("APPDATA");
        if (!appdata) { free(settings); return build_json_string("error: no appdata"); }
        char dir[MAX_PATH], path[MAX_PATH];
        snprintf(dir, sizeof(dir), "%s\\SteamToolsLua", appdata);
        snprintf(path, sizeof(path), "%s\\settings.json", dir);
        ensure_directory(dir);
        int ok = write_file(path, settings);
        free(settings);
        return build_json_string(ok ? "ok" : "error: write failed");
    }

    /* ---------- 5. search_online_fix ---------- */
    if (strcmp(cmd, "search_online_fix") == 0) {
        if (!args) return my_strdup("[]");
        char* query = extract_json_str_raw(args, "query");
        char* type = extract_json_str_raw(args, "type");
        if (!query) { free(type); return my_strdup("[]"); }
        char url[2048];
        if (type && strcmp(type, "appid") == 0) {
            snprintf(url, sizeof(url), "https://online-fix.me/games/?app_id=%s", query);
        } else {
            char* encoded = url_encode_simple(query);
            snprintf(url, sizeof(url), "https://online-fix.me/games/?title=%s", encoded ? encoded : query);
            free(encoded);
        }
        free(type);
        free(query);

        char* html = http_get(url, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\nAccept: text/html,application/xhtml+xml");
        if (!html) {
            ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
            return my_strdup("[]");
        }

        size_t html_len = strlen(html);
        if (html_len > 100000) html[100000] = '\0';

        char* result = (char*)malloc(2);
        if (!result) { free(html); return my_strdup("[]"); }
        result[0] = '[';
        result[1] = '\0';
        size_t result_cap = 2;
        int first = 1;
        const char* pos = html;
        const char* block_markers[] = {"game-block", "game-item", "post-card", "game_card"};
        int found = 0;

        while (1) {
            const char* block_start = NULL;
            const char* used_marker = NULL;
            for (int mi = 0; mi < 4; mi++) {
                char search[64];
                snprintf(search, sizeof(search), "class=\"%s\"", block_markers[mi]);
                const char* bp = strstr(pos, search);
                if (bp && (!block_start || bp < block_start)) {
                    block_start = bp;
                    used_marker = block_markers[mi];
                }
            }
            if (!block_start) break;

            while (block_start > html && *block_start != '<') block_start--;
            if (*block_start != '<') { pos = block_start + 1; continue; }

            int depth = 1;
            const char* p = block_start + 1;
            while (*p && depth > 0) {
                if (*p == '<') {
                    if (*(p+1) == '/') {
                        const char* te = strchr(p, '>');
                        if (te && (te - p < 20)) { depth--; if (depth == 0) { p = te + 1; break; } }
                    } else if (*(p+1) != '!' && *(p+1) != '?') {
                        const char* te = strchr(p, '>');
                        if (te && (te - p < 50) && *(te-1) != '/') depth++;
                    }
                }
                p++;
            }
            size_t blen = (size_t)(p - block_start);
            char* block = (char*)malloc(blen + 1);
            if (!block) break;
            memcpy(block, block_start, blen);
            block[blen] = '\0';

            char* title = NULL;
            char* app_id = NULL;
            char* game_url = NULL;
            char* image = NULL;
            char* date = NULL;
            char* size_str = NULL;

            char* h = strstr(block, "<h2");
            if (!h) h = strstr(block, "<h3");
            if (h) {
                const char* tstart = strchr(h, '>');
                if (tstart) {
                    tstart++;
                    const char* tend = strstr(tstart, "</h");
                    if (!tend) tend = strstr(tstart, "</a>");
                    if (tend) {
                        size_t tl = (size_t)(tend - tstart);
                        title = (char*)malloc(tl + 1);
                        if (title) { memcpy(title, tstart, tl); title[tl] = '\0'; trim_trailing(title); }
                    }
                }
            }
            if (!title) title = extract_html_between(block, "<a", ">");
            if (title) {
                char* alt_title = extract_attr_value(title, "title");
                if (alt_title) { free(title); title = alt_title; }
            }
            if (!title) {
                char* a_tag = extract_html_between(block, "<a", "</a>");
                if (a_tag) {
                    const char* gt = strchr(a_tag, '>');
                    if (gt) {
                        gt++;
                        size_t tl = strlen(gt);
                        title = (char*)malloc(tl + 1);
                        if (title) { memcpy(title, gt, tl + 1); trim_trailing(title); }
                    }
                    free(a_tag);
                }
            }

            app_id = extract_attr_value(block, "data-app-id");
            if (!app_id) app_id = extract_attr_value(block, "data-id");
            if (!app_id) app_id = extract_attr_value(block, "data-appid");

            char* tmp_url = extract_attr_value(block, "href");
            if (tmp_url) {
                if (strncmp(tmp_url, "http", 4) != 0 && tmp_url[0] == '/') {
                    size_t ulen = strlen(tmp_url) + 28;
                    game_url = (char*)malloc(ulen);
                    if (game_url) snprintf(game_url, ulen, "https://online-fix.me%s", tmp_url);
                } else {
                    game_url = my_strdup(tmp_url);
                }
                free(tmp_url);
            }

            image = extract_attr_value(block, "src");

            char* date_label = strstr(block, "date");
            if (!date_label) date_label = strstr(block, "Date");
            if (date_label) {
                const char* dstart = date_label;
                while (*dstart && *dstart != '>') dstart++;
                if (*dstart == '>') {
                    dstart++;
                    const char* dend = strchr(dstart, '<');
                    if (dend) {
                        size_t dl = (size_t)(dend - dstart);
                        date = (char*)malloc(dl + 1);
                        if (date) { memcpy(date, dstart, dl); date[dl] = '\0'; trim_trailing(date); }
                    }
                }
            }

            size_str = extract_html_between(block, "Size:", "<");
            if (!size_str) size_str = extract_html_between(block, "size:", "<");

            if (title && game_url) {
                found = 1;
                size_t needed = strlen(title) + (app_id ? strlen(app_id) : 0) + (game_url ? strlen(game_url) : 0) + (image ? strlen(image) : 0) + (date ? strlen(date) : 0) + (size_str ? strlen(size_str) : 0) + 500;
                char* entry = (char*)malloc(needed);
                if (entry) {
                    char* e_title = json_escape(title);
                    char* e_game_url = json_escape(game_url ? game_url : "");
                    char* e_image = json_escape(image ? image : "");
                    char* e_date = json_escape(date ? date : "");
                    char* e_size = json_escape(size_str ? size_str : "");
                    char* e_appid = json_escape(app_id ? app_id : "");
                    snprintf(entry, needed,
                        "%s{\"title\":%s,\"app_id\":%s,\"url\":%s,\"image\":%s,\"date\":%s,\"size\":%s}",
                        first ? "" : ",",
                        e_title ? e_title : "\"\"",
                        e_appid ? e_appid : "\"\"",
                        e_game_url ? e_game_url : "\"\"",
                        e_image ? e_image : "\"\"",
                        e_date ? e_date : "\"\"",
                        e_size ? e_size : "\"\"");
                    free(e_title); free(e_game_url); free(e_image); free(e_date); free(e_size); free(e_appid);
                    first = 0;
                    size_t elen = strlen(entry);
                    size_t newcap = result_cap + elen + 1;
                    char* newres = (char*)realloc(result, newcap);
                    if (newres) { result = newres; result_cap = newcap; strcat(result, entry); }
                    free(entry);
                }
            }

            free(title); free(app_id); free(game_url); free(image); free(date); free(size_str);
            free(block);
            pos = p;
        }

        free(html);

        size_t clen = strlen(result) + 2;
        char* final_res = (char*)realloc(result, clen);
        if (!final_res) { free(result); return my_strdup("[]"); }
        result = final_res;
        strcat(result, "]");
        if (!found) {
            free(result);
            return my_strdup("[]");
        }
        return result;
    }

    /* ---------- 6. get_game_urls ---------- */
    if (strcmp(cmd, "get_game_urls") == 0) {
        if (!args) return my_strdup("{\"open_browser\":true,\"url\":\"\"}");
        char* page_url = extract_json_str_raw(args, "url");
        char* app_id = extract_json_str_raw(args, "app_id");
        if (!page_url) { free(app_id); return my_strdup("{\"open_browser\":true,\"url\":\"\"}"); }

        char* html = http_get(page_url, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\nAccept: text/html,application/xhtml+xml");
        if (!html) {
            size_t ulen = strlen(page_url) + 100;
            char* open_resp = (char*)malloc(ulen);
            if (open_resp) {
                char* e_url = json_escape(page_url);
                snprintf(open_resp, ulen, "{\"open_browser\":true,\"url\":%s}", e_url ? e_url : "\"\"");
                free(e_url);
            }
            free(page_url); free(app_id);
            ShellExecuteA(NULL, "open", page_url, NULL, NULL, SW_SHOWNORMAL);
            return open_resp ? open_resp : my_strdup("{\"open_browser\":true,\"url\":\"\"}");
        }

        const char* prefixes[] = {
            "https://uploads.online-fix.me:",
            "https://hosters.online-fix.me:",
            "https://drive.online-fix.me:",
            "https://torrents.online-fix.me:",
            "http://uploads.online-fix.me:",
            "http://hosters.online-fix.me:",
            "http://drive.online-fix.me:",
            "http://torrents.online-fix.me:"
        };
        int url_count = 0;
        char** urls = NULL;
        int urls_cap = 0;

        const char* scan = html;
        while (*scan) {
            const char* found_url = NULL;
            int plen = 0;
            for (int pi = 0; pi < 8; pi++) {
                const char* fp = strstr(scan, prefixes[pi]);
                if (fp && (!found_url || fp < found_url)) {
                    found_url = fp;
                    plen = (int)strlen(prefixes[pi]);
                }
            }
            if (!found_url) break;

            const char* start_url = found_url;
            const char* end_url = start_url;
            while (*end_url && *end_url != '"' && *end_url != '\'' && *end_url != '>' && *end_url != '&' && *end_url != ' ' && *end_url != '<' && *end_url != '\t' && *end_url != '\n' && *end_url != '\r')
                end_url++;

            if (end_url > start_url + plen + 1) {
                size_t ul = (size_t)(end_url - start_url);
                char* u = (char*)malloc(ul + 1);
                if (u) {
                    memcpy(u, start_url, ul);
                    u[ul] = '\0';
                    if (url_count >= urls_cap) {
                        urls_cap = urls_cap ? urls_cap * 2 : 16;
                        char** new_urls = (char**)realloc(urls, urls_cap * sizeof(char*));
                        if (new_urls) { urls = new_urls; } else { free(u); break; }
                    }
                    urls[url_count++] = u;
                }
            }
            scan = end_url;
        }

        free(html);

        int has_php_session = (strstr(page_url, "PHPSESSID") != NULL);

        if (url_count == 0) {
            size_t ulen = strlen(page_url) + 100;
            char* open_resp = (char*)malloc(ulen);
            if (open_resp) {
                char* e_url = json_escape(page_url);
                snprintf(open_resp, ulen, "{\"open_browser\":true,\"url\":%s}", e_url ? e_url : "\"\"");
                free(e_url);
            }
            free(page_url); free(app_id);
            ShellExecuteA(NULL, "open", page_url, NULL, NULL, SW_SHOWNORMAL);
            return open_resp ? open_resp : my_strdup("{\"open_browser\":true,\"url\":\"\"}");
        }

        size_t buf_size = 256;
        for (int i = 0; i < url_count; i++) buf_size += strlen(urls[i]) + 8;
        char* result = (char*)malloc(buf_size);
        if (!result) {
            for (int i = 0; i < url_count; i++) free(urls[i]);
            free(urls); free(page_url); free(app_id);
            return my_strdup("{\"urls\":[],\"has_php_session\":false}");
        }
        char* ptr = result;
        ptr += snprintf(ptr, buf_size - (size_t)(ptr - result), "{\"urls\":[");
        for (int i = 0; i < url_count; i++) {
            char* eu = json_escape(urls[i]);
            ptr += snprintf(ptr, buf_size - (size_t)(ptr - result), "%s%s", i > 0 ? "," : "", eu ? eu : "\"\"");
            free(eu);
            free(urls[i]);
        }
        free(urls);
        ptr += snprintf(ptr, buf_size - (size_t)(ptr - result), "],\"has_php_session\":%s}", has_php_session ? "true" : "false");
        free(page_url);
        free(app_id);
        return result;
    }

    /* ---------- 7. get_steam_app_details ---------- */
    if (strcmp(cmd, "get_steam_app_details") == 0) {
        if (!args) return my_strdup("{}");
        char* app_id = extract_json_str_raw(args, "app_id");
        if (!app_id) return my_strdup("{}");
        char url[1024];
        snprintf(url, sizeof(url), "https://store.steampowered.com/api/appdetails?appids=%s", app_id);
        char* json = http_get(url, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        if (!json) { free(app_id); return my_strdup("{}"); }

        char* data = extract_json_str_raw(json, app_id);
        if (!data) { free(json); free(app_id); return my_strdup("{}"); }

        char* success = extract_json_str_raw(data, "success");
        if (!success || strcmp(success, "true") != 0) {
            free(success); free(data); free(json); free(app_id);
            return my_strdup("{}");
        }
        free(success);

        char* game_data = extract_json_str_raw(data, "data");
        if (!game_data) { free(data); free(json); free(app_id); return my_strdup("{}"); }

        char* name = extract_json_str_raw(game_data, "name");
        char* header_image = extract_json_str_raw(game_data, "header_image");
        char* short_desc = extract_json_str_raw(game_data, "short_description");

        char* developers_raw = extract_json_str_raw(game_data, "developers");
        char* publishers_raw = extract_json_str_raw(game_data, "publishers");
        char* genres_raw = extract_json_str_raw(game_data, "genres");

        char* price_overview = extract_json_str_raw(game_data, "price_overview");
        char* price_str = NULL;
        if (price_overview) {
            price_str = extract_json_str_raw(price_overview, "final_formatted");
            if (!price_str) price_str = extract_json_str_raw(price_overview, "initial_formatted");
            free(price_overview);
        }

        size_t bufsize = strlen(game_data) + 500;
        char* result = (char*)malloc(bufsize);
        if (!result) {
            free(name); free(header_image); free(short_desc);
            free(developers_raw); free(publishers_raw); free(genres_raw); free(price_str);
            free(game_data); free(data); free(json); free(app_id);
            return my_strdup("{}");
        }

        char* e_name = json_escape(name ? name : "");
        char* e_image = json_escape(header_image ? header_image : "");
        char* e_desc = json_escape(short_desc ? short_desc : "");
        char* e_price = json_escape(price_str ? price_str : "");

        snprintf(result, bufsize,
            "{"
            "\"name\":%s,"
            "\"developers\":%s,"
            "\"publishers\":%s,"
            "\"genres\":%s,"
            "\"price\":%s,"
            "\"header_image\":%s,"
            "\"description\":%s"
            "}",
            e_name ? e_name : "null",
            developers_raw ? developers_raw : "[]",
            publishers_raw ? publishers_raw : "[]",
            genres_raw ? genres_raw : "[]",
            e_price ? e_price : "\"\"",
            e_image ? e_image : "\"\"",
            e_desc ? e_desc : "\"\"");

        free(e_name); free(e_image); free(e_desc); free(e_price);
        free(name); free(header_image); free(short_desc);
        free(developers_raw); free(publishers_raw); free(genres_raw); free(price_str);
        free(game_data); free(data); free(json); free(app_id);
        return result;
    }

    /* ---------- 8. get_multiplayer_info ---------- */
    if (strcmp(cmd, "get_multiplayer_info") == 0) {
        if (!args) return my_strdup("{\"is_multiplayer\":false,\"tags\":[]}");
        char* app_id = extract_json_str_raw(args, "app_id");
        if (!app_id) return my_strdup("{\"is_multiplayer\":false,\"tags\":[]}");
        char url[1024];
        snprintf(url, sizeof(url), "https://steamspy.com/api.php?request=appdetails&appid=%s", app_id);
        char* json = http_get(url, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        if (!json) { free(app_id); return my_strdup("{\"is_multiplayer\":false,\"tags\":[]}"); }

        char* tags_raw = extract_json_str_raw(json, "tags");
        free(json);

        int is_mp = 0;
        if (tags_raw) {
            const char* tp = tags_raw;
            while (*tp) {
                if (*tp == '"') {
                    tp++;
                    char tag_buf[256];
                    int ti = 0;
                    while (*tp && *tp != '"' && ti < 254) {
                        if (*tp == '\\') { tp++; if (*tp) tag_buf[ti++] = *tp++; }
                        else tag_buf[ti++] = *tp++;
                    }
                    tag_buf[ti] = '\0';
                    if (has_multiplayer_tag(tag_buf)) { is_mp = 1; break; }
                } else tp++;
            }
        }

        size_t result_len = strlen(tags_raw ? tags_raw : "[]") + 100;
        char* result = (char*)malloc(result_len);
        if (!result) { free(tags_raw); free(app_id); return my_strdup("{\"is_multiplayer\":false,\"tags\":[]}"); }
        snprintf(result, result_len, "{\"is_multiplayer\":%s,\"tags\":%s}", is_mp ? "true" : "false", tags_raw ? tags_raw : "[]");
        free(tags_raw);
        free(app_id);
        return result;
    }

    /* ---------- 9. scan_library ---------- */
    if (strcmp(cmd, "scan_library") == 0) {
        if (!args) return my_strdup("[]");
        char* scan_path = extract_json_str_raw(args, "path");
        if (!scan_path) return my_strdup("[]");
        char search_path[MAX_PATH];
        snprintf(search_path, sizeof(search_path), "%s\\*.zip", scan_path);

        WIN32_FIND_DATAA ffd;
        HANDLE hFind = FindFirstFileA(search_path, &ffd);
        if (hFind == INVALID_HANDLE_VALUE) { free(scan_path); return my_strdup("[]"); }

        int first = 1;
        size_t cap = 4096;
        char* result = (char*)malloc(cap);
        if (!result) { FindClose(hFind); free(scan_path); return my_strdup("[]"); }
        result[0] = '[';
        result[1] = '\0';
        size_t len = 1;

        do {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char file_path[MAX_PATH];
            snprintf(file_path, sizeof(file_path), "%s\\%s", scan_path, ffd.cFileName);

            char lua_name[256] = {0};
            FILE* f = fopen(file_path, "rb");
            if (f) {
                unsigned char buf[1024];
                size_t nread = fread(buf, 1, sizeof(buf), f);
                fclose(f);
                if (nread > 30) {
                    for (size_t i = 0; i < nread - 30; i++) {
                        if (buf[i] == 0x50 && buf[i+1] == 0x4B && buf[i+2] == 0x01 && buf[i+3] == 0x02) {
                            size_t fnlen = buf[i+28] | (buf[i+29] << 8);
                            size_t fname_pos = i + 46;
                            if (fname_pos + fnlen <= nread) {
                                size_t cp = fname_pos;
                                size_t lua_idx = 0;
                                while (cp < fname_pos + fnlen && lua_idx < 250) {
                                    lua_name[lua_idx++] = (char)buf[cp++];
                                }
                                lua_name[lua_idx] = '\0';
                            }
                            break;
                        }
                        if (buf[i] == 0x50 && buf[i+1] == 0x4B && buf[i+2] == 0x03 && buf[i+3] == 0x04) {
                            size_t fnlen_pos = i + 26;
                            size_t fnlen = buf[fnlen_pos] | (buf[fnlen_pos+1] << 8);
                            size_t fname_pos = fnlen_pos + 4;
                            if (fname_pos + fnlen <= nread) {
                                size_t cp = fname_pos;
                                size_t lua_idx = 0;
                                while (cp < fname_pos + fnlen && lua_idx < 250) {
                                    lua_name[lua_idx++] = (char)buf[cp++];
                                }
                                lua_name[lua_idx] = '\0';
                            }
                            break;
                        }
                    }
                }
            }

            char* e_title = json_escape(ffd.cFileName);
            char* e_lua = json_escape(lua_name);
            char* e_path = json_escape(file_path);

            char date_str[64] = {0};
            SYSTEMTIME st;
            if (FileTimeToSystemTime(&ffd.ftLastWriteTime, &st)) {
                snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d %02d:%02d",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
            }
            char* e_date = json_escape(date_str);

            size_t needed = strlen(e_title) + strlen(e_lua) + strlen(e_path) + strlen(e_date) + 200;
            char* entry = (char*)malloc(needed);
            if (entry) {
                snprintf(entry, needed,
                    "%s{\"title\":%s,\"appid\":%s,\"path\":%s,\"date\":%s}",
                    first ? "" : ",", e_title ? e_title : "\"\"", e_lua ? e_lua : "\"\"",
                    e_path ? e_path : "\"\"", e_date ? e_date : "\"\"");
                size_t elen = strlen(entry);
                if (len + elen + 2 > cap) {
                    cap = (len + elen + 2) * 2;
                    char* nr = (char*)realloc(result, cap);
                    if (!nr) { free(entry); free(e_title); free(e_lua); free(e_path); free(e_date); free(result); FindClose(hFind); free(scan_path); return my_strdup("[]"); }
                    result = nr;
                }
                memcpy(result + len, entry, elen + 1);
                len += elen;
                first = 0;
                free(entry);
            }
            free(e_title); free(e_lua); free(e_path); free(e_date);
        } while (FindNextFileA(hFind, &ffd) != 0);
        FindClose(hFind);
        free(scan_path);

        if (len + 2 > cap) {
            char* nr = (char*)realloc(result, cap + 2);
            if (!nr) { free(result); return my_strdup("[]"); }
            result = nr;
        }
        result[len] = ']';
        result[len+1] = '\0';
        return result;
    }

    /* ---------- 10. scan_depots ---------- */
    if (strcmp(cmd, "scan_depots") == 0) {
        if (!args) return my_strdup("[]");
        char* steam_path = extract_json_str_raw(args, "steam_path");
        if (!steam_path) return my_strdup("[]");

        char common_path[MAX_PATH];
        snprintf(common_path, sizeof(common_path), "%s\\steamapps\\common", steam_path);
        char search_path[MAX_PATH];
        snprintf(search_path, sizeof(search_path), "%s\\*", common_path);

        int first = 1;
        size_t cap = 4096;
        char* result = (char*)malloc(cap);
        if (!result) { free(steam_path); return my_strdup("[]"); }
        result[0] = '[';
        result[1] = '\0';
        size_t len = 1;

        WIN32_FIND_DATAA ffd;
        HANDLE hFind = FindFirstFileA(search_path, &ffd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) continue;

                char* e_name = json_escape(ffd.cFileName);
                char dir_path[MAX_PATH];
                snprintf(dir_path, sizeof(dir_path), "%s\\%s", common_path, ffd.cFileName);
                char* e_path = json_escape(dir_path);

                size_t needed = strlen(e_name) + strlen(e_path) + 200;
                char* entry = (char*)malloc(needed);
                if (entry) {
                    snprintf(entry, needed, "%s{\"appid\":\"\",\"name\":%s,\"path\":%s}", first ? "" : ",", e_name ? e_name : "\"\"", e_path ? e_path : "\"\"");
                    size_t elen = strlen(entry);
                    if (len + elen + 2 > cap) {
                        cap = (len + elen + 2) * 2;
                        char* nr = (char*)realloc(result, cap);
                        if (!nr) { free(entry); free(e_name); free(e_path); free(result); FindClose(hFind); free(steam_path); return my_strdup("[]"); }
                        result = nr;
                    }
                    memcpy(result + len, entry, elen + 1);
                    len += elen;
                    first = 0;
                    free(entry);
                }
                free(e_name); free(e_path);
            } while (FindNextFileA(hFind, &ffd) != 0);
            FindClose(hFind);
        }

        char vdf_path[MAX_PATH];
        snprintf(vdf_path, sizeof(vdf_path), "%s\\steamapps\\libraryfolders.vdf", steam_path);
        char* vdf = read_file(vdf_path);
        if (vdf) {
            const char* vp = vdf;
            while (*vp) {
                if (strncmp(vp, "\"path\"", 6) == 0) {
                    vp += 6;
                    while (*vp && *vp != '"') vp++;
                    if (*vp == '"') {
                        vp++;
                        const char* pstart = vp;
                        while (*vp && !(*vp == '"' && *(vp-1) != '\\')) vp++;
                        if (*vp == '"') {
                            size_t pl = (size_t)(vp - pstart);
                            char* lib_path = (char*)malloc(pl + 1);
                            if (lib_path) {
                                memcpy(lib_path, pstart, pl);
                                lib_path[pl] = '\0';
                                char lib_common[MAX_PATH];
                                snprintf(lib_common, sizeof(lib_common), "%s\\steamapps\\common", lib_path);
                                char lib_search[MAX_PATH];
                                snprintf(lib_search, sizeof(lib_search), "%s\\*", lib_common);
                                WIN32_FIND_DATAA lfd;
                                HANDLE hLib = FindFirstFileA(lib_search, &lfd);
                                if (hLib != INVALID_HANDLE_VALUE) {
                                    do {
                                        if (!(lfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                                        if (strcmp(lfd.cFileName, ".") == 0 || strcmp(lfd.cFileName, "..") == 0) continue;
                                        char* e_name = json_escape(lfd.cFileName);
                                        char ldir_path[MAX_PATH];
                                        snprintf(ldir_path, sizeof(ldir_path), "%s\\%s", lib_common, lfd.cFileName);
                                        char* e_path = json_escape(ldir_path);
                                        size_t needed = strlen(e_name) + strlen(e_path) + 200;
                                        char* entry = (char*)malloc(needed);
                                        if (entry) {
                                            snprintf(entry, needed, "%s{\"appid\":\"\",\"name\":%s,\"path\":%s}", first ? "" : ",", e_name ? e_name : "\"\"", e_path ? e_path : "\"\"");
                                            size_t elen = strlen(entry);
                                            if (len + elen + 2 > cap) {
                                                cap = (len + elen + 2) * 2;
                                                char* nr = (char*)realloc(result, cap);
                                                if (!nr) { free(entry); free(e_name); free(e_path); free(lib_path); free(vdf); free(result); FindClose(hLib); free(steam_path); return my_strdup("[]"); }
                                                result = nr;
                                            }
                                            memcpy(result + len, entry, elen + 1);
                                            len += elen;
                                            first = 0;
                                            free(entry);
                                        }
                                        free(e_name); free(e_path);
                                    } while (FindNextFileA(hLib, &lfd) != 0);
                                    FindClose(hLib);
                                }
                                free(lib_path);
                            }
                        }
                    }
                } else vp++;
            }
            free(vdf);
        }

        if (len + 2 > cap) {
            char* nr = (char*)realloc(result, cap + 2);
            if (!nr) { free(result); free(steam_path); return my_strdup("[]"); }
            result = nr;
        }
        result[len] = ']';
        result[len+1] = '\0';
        free(steam_path);
        return result;
    }

    /* ---------- 11. scan_steamapps ---------- */
    if (strcmp(cmd, "scan_steamapps") == 0) {
        if (!args) return my_strdup("[]");
        char* steam_path = extract_json_str_raw(args, "steam_path");
        if (!steam_path) return my_strdup("[]");

        char acf_dir[MAX_PATH];
        snprintf(acf_dir, sizeof(acf_dir), "%s\\steamapps", steam_path);
        char search_path[MAX_PATH];
        snprintf(search_path, sizeof(search_path), "%s\\*.acf", acf_dir);

        int first = 1;
        size_t cap = 8192;
        char* result = (char*)malloc(cap);
        if (!result) { free(steam_path); return my_strdup("[]"); }
        result[0] = '[';
        result[1] = '\0';
        size_t len = 1;

        WIN32_FIND_DATAA ffd;
        HANDLE hFind = FindFirstFileA(search_path, &ffd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                char acf_path[MAX_PATH];
                snprintf(acf_path, sizeof(acf_path), "%s\\%s", acf_dir, ffd.cFileName);
                char* acf = read_file(acf_path);
                if (!acf) continue;
                char appid[64] = {0}, name[256] = {0}, installdir[256] = {0};

                const char* ap = strstr(acf, "\"appid\"");
                if (ap) {
                    ap += 7; while (*ap && *ap != '"') ap++;
                    if (*ap == '"') { ap++; int ai = 0; while (*ap && *ap != '"' && ai < 62) appid[ai++] = *ap++; appid[ai] = '\0'; }
                }
                const char* np = strstr(acf, "\"name\"");
                if (np) {
                    np += 6; while (*np && *np != '"') np++;
                    if (*np == '"') { np++; int ni = 0; while (*np && *np != '"' && ni < 254) { if (*np == '\\' && *(np+1)) np++; name[ni++] = *np++; } name[ni] = '\0'; }
                }
                const char* ip = strstr(acf, "\"installdir\"");
                if (ip) {
                    ip += 11; while (*ip && *ip != '"') ip++;
                    if (*ip == '"') { ip++; int ii = 0; while (*ip && *ip != '"' && ii < 254) { if (*ip == '\\' && *(ip+1)) ip++; installdir[ii++] = *ip++; } installdir[ii] = '\0'; }
                }

                char* e_appid = json_escape(appid);
                char* e_name = json_escape(name);
                char* e_installdir = json_escape(installdir);

                size_t needed = strlen(e_appid) + strlen(e_name) + strlen(e_installdir) + 200;
                char* entry = (char*)malloc(needed);
                if (entry) {
                    snprintf(entry, needed, "%s{\"appid\":%s,\"name\":%s,\"installdir\":%s}",
                        first ? "" : ",", e_appid ? e_appid : "\"\"", e_name ? e_name : "\"\"", e_installdir ? e_installdir : "\"\"");
                    size_t elen = strlen(entry);
                    if (len + elen + 2 > cap) {
                        cap = (len + elen + 2) * 2;
                        char* nr = (char*)realloc(result, cap);
                        if (!nr) { free(entry); free(e_appid); free(e_name); free(e_installdir); free(acf); free(result); FindClose(hFind); free(steam_path); return my_strdup("[]"); }
                        result = nr;
                    }
                    memcpy(result + len, entry, elen + 1);
                    len += elen;
                    first = 0;
                    free(entry);
                }
                free(e_appid); free(e_name); free(e_installdir);
                free(acf);
            } while (FindNextFileA(hFind, &ffd) != 0);
            FindClose(hFind);
        }

        if (len + 2 > cap) {
            char* nr = (char*)realloc(result, cap + 2);
            if (!nr) { free(result); free(steam_path); return my_strdup("[]"); }
            result = nr;
        }
        result[len] = ']';
        result[len+1] = '\0';
        free(steam_path);
        return result;
    }

    /* ---------- 12. translate ---------- */
    if (strcmp(cmd, "translate") == 0) {
        if (!args) return my_strdup("{\"translated\":\"\",\"provider\":\"\"}");
        char* text = extract_json_str_raw(args, "text");
        char* source = extract_json_str_raw(args, "source");
        char* target = extract_json_str_raw(args, "target");
        char* provider = extract_json_str_raw(args, "provider");
        if (!text) { free(source); free(target); free(provider); return my_strdup("{\"translated\":\"\",\"provider\":\"\"}"); }
        if (!source) source = my_strdup("auto");
        if (!target) target = my_strdup("en");

        char* translated = NULL;
        char* used_provider = my_strdup(provider ? provider : "google");

        if (!provider || strcmp(provider, "google") == 0) {
            char* encoded = url_encode_simple(text);
            if (!encoded) { free(text); free(source); free(target); free(provider); free(used_provider); return my_strdup("{\"translated\":\"\",\"provider\":\"\"}"); }
            char url[8192];
            snprintf(url, sizeof(url), "https://translate.googleapis.com/translate_a/single?client=gtx&sl=%s&tl=%s&dt=t&q=%s", source, target, encoded);
            free(encoded);
            char* resp = http_get(url, "User-Agent: Mozilla/5.0");
            if (resp) {
                if (resp[0] == '[') {
                    const char* rp = resp + 1;
                    if (*rp == '[') {
                        rp++;
                        if (*rp == '[') {
                            rp++;
                            if (*rp == '"') {
                                rp++;
                                const char* tend = rp;
                                while (*tend && !(*tend == '"' && *(tend-1) != '\\')) tend++;
                                if (*tend == '"') {
                                    size_t tl = (size_t)(tend - rp);
                                    translated = (char*)malloc(tl + 1);
                                    if (translated) {
                                        size_t ti = 0;
                                        const char* tp = rp;
                                        while (tp < tend && ti < tl) {
                                            if (*tp == '\\' && *(tp+1)) { tp++; if (*tp == 'n') translated[ti++] = '\n'; else if (*tp == 't') translated[ti++] = '\t'; else translated[ti++] = *tp; tp++; }
                                            else translated[ti++] = *tp++;
                                        }
                                        translated[ti] = '\0';
                                    }
                                }
                            }
                        }
                    }
                }
                free(resp);
            }
        } else {
            char key_path[256];
            snprintf(key_path, sizeof(key_path), "SOFTWARE\\SteamTools\\key_%s", provider);
            HKEY hKey;
            char api_key[256] = {0};
            DWORD key_size = sizeof(api_key);
            if (RegOpenKeyExA(HKEY_CURRENT_USER, key_path, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                RegQueryValueExA(hKey, "", NULL, NULL, (LPBYTE)api_key, &key_size);
                RegCloseKey(hKey);
            }
            if (api_key[0]) {
                free(used_provider);
                used_provider = my_strdup(provider);
            }
            translated = my_strdup("");
        }

        if (!translated) translated = my_strdup("");

        char* e_translated = json_escape(translated);
        char* e_provider = json_escape(used_provider ? used_provider : provider ? provider : "google");

        size_t rlen = strlen(e_translated) + strlen(e_provider) + 100;
        char* result = (char*)malloc(rlen);
        if (result) snprintf(result, rlen, "{\"translated\":%s,\"provider\":%s}", e_translated ? e_translated : "\"\"", e_provider ? e_provider : "\"\"");
        else result = my_strdup("{\"translated\":\"\",\"provider\":\"\"}");

        free(e_translated); free(e_provider);
        free(translated); free(used_provider);
        free(text); free(source); free(target); free(provider);
        return result;
    }

    /* ---------- 13. launch_cloudredirect ---------- */
    if (strcmp(cmd, "launch_cloudredirect") == 0) {
        if (!args) return my_strdup("{\"started\":false,\"pid\":0}");
        char* mode = extract_json_str_raw(args, "mode");
        char* cmd_args = extract_json_str_raw(args, "args");
        char exe_path[MAX_PATH];
        snprintf(exe_path, sizeof(exe_path), "%s\\CloudRedirect.exe", g_exe_dir_global);

        int started = RunProcess(exe_path, cmd_args ? cmd_args : "");

        size_t rlen = strlen(exe_path) + 100;
        char* result = (char*)malloc(rlen);
        if (result) snprintf(result, rlen, "{\"started\":%s}", started == 0 ? "true" : "false");
        else result = my_strdup("{\"started\":false,\"pid\":0}");

        free(mode); free(cmd_args);
        return result;
    }

    /* ---------- 14. check_update ---------- */
    if (strcmp(cmd, "check_update") == 0) {
        char* update_json = CheckUpdate("1.9.0");
        if (!update_json) return my_strdup("{}");
        return update_json;
    }

    /* ---------- 15. start_update ---------- */
    if (strcmp(cmd, "start_update") == 0) {
        if (!args) return my_strdup("{\"started\":false}");
        char* url = extract_json_str_raw(args, "url");
        int started = StartUpdate(url);
        free(url);
        return my_strdup(started ? "{\"started\":true}" : "{\"started\":false}");
    }

    /* ---------- 16. get_search_history ---------- */
    if (strcmp(cmd, "get_search_history") == 0) {
        int count = 0;
        char** history = GetSearchHistory(&count);
        if (!history || count == 0) {
            if (history) free(history);
            return my_strdup("[]");
        }
        size_t cap = 4096;
        char* result = (char*)malloc(cap);
        if (!result) { for (int i = 0; i < count; i++) free(history[i]); free(history); return my_strdup("[]"); }
        result[0] = '[';
        result[1] = '\0';
        size_t len = 1;
        for (int i = 0; i < count; i++) {
            char* e = json_escape(history[i]);
            size_t needed = strlen(e ? e : "") + 10;
            if (len + needed + 2 > cap) {
                cap = (len + needed + 2) * 2;
                char* nr = (char*)realloc(result, cap);
                if (!nr) {
                    for (int j = i; j < count; j++) free(history[j]);
                    free(history); free(result);
                    return my_strdup("[]");
                }
                result = nr;
            }
            len += snprintf(result + len, cap - len, "%s%s", i > 0 ? "," : "", e ? e : "\"\"");
            free(e);
            free(history[i]);
        }
        free(history);
        if (len + 2 > cap) {
            char* nr = (char*)realloc(result, cap + 2);
            if (!nr) { free(result); return my_strdup("[]"); }
            result = nr;
        }
        result[len] = ']';
        result[len+1] = '\0';
        return result;
    }

    /* ---------- 17. add_search_history ---------- */
    if (strcmp(cmd, "add_search_history") == 0) {
        if (!args) return build_json_string("ok");
        char* term = extract_json_str_raw(args, "term");
        if (term) { AddSearchHistory(term); free(term); }
        return build_json_string("ok");
    }

    /* ---------- 18. extract_archive ---------- */
    if (strcmp(cmd, "extract_archive") == 0) {
        if (!args) return my_strdup("{\"extracted\":false,\"path\":\"\"}");
        char* archive_path = extract_json_str_raw(args, "path");
        char* password = extract_json_str_raw(args, "password");
        if (!archive_path) return my_strdup("{\"extracted\":false,\"path\":\"\"}");

        char out_path[MAX_PATH] = {0};
        snprintf(out_path, MAX_PATH, "%s", archive_path);
        char *dot = strrchr(out_path, '.');
        if (dot) *dot = '\0';
        MakeDir(out_path);
        int ok = ExtractArchive(archive_path, out_path, password) == 0;
        char* e_path = json_escape(out_path);
        size_t rlen = strlen(e_path ? e_path : "") + 100;
        char* result = (char*)malloc(rlen);
        if (result) snprintf(result, rlen, "{\"extracted\":%s,\"path\":%s}", ok ? "true" : "false", e_path ? e_path : "\"\"");
        else result = my_strdup("{\"extracted\":false,\"path\":\"\"}");
        free(e_path);
        free(archive_path);
        free(password);
        return result;
    }

    /* ---------- 19. pick_folder ---------- */
    if (strcmp(cmd, "pick_folder") == 0) {
        char path[MAX_PATH] = {0};
        BROWSEINFOA bi;
        memset(&bi, 0, sizeof(bi));
        bi.lpszTitle = "Select a folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
        if (pidl) {
            SHGetPathFromIDListA(pidl, path);
            IMalloc* pMalloc = NULL;
            if (SUCCEEDED(SHGetMalloc(&pMalloc))) {
                pMalloc->lpVtbl->Free(pMalloc, pidl);
                pMalloc->lpVtbl->Release(pMalloc);
            }
        }
        return build_json_string(path);
    }

    /* ---------- 20. get_scan_status ---------- */
    if (strcmp(cmd, "get_scan_status") == 0) {
        return my_strdup("{\"scanning\":false}");
    }

    /* ---------- 21. scan_games (combined) ---------- */
    if (strcmp(cmd, "scan_games") == 0) {
        char *result = (char*)malloc(32768);
        if (!result) return my_strdup("{\"error\":\"oom\"}");
        char *p = result;
        int n = 0;
        n = snprintf(p, 32768, "{\"installed\":[");
        p += n;
        // Scan steamapps/common
        char common[MAX_PATH];
        snprintf(common, MAX_PATH, "%s\\..\\steamapps\\common", g_exe_dir_global);
        WIN32_FIND_DATAA ffd;
        char path[MAX_PATH];
        snprintf(path, MAX_PATH, "%s\\*", common);
        HANDLE hf = FindFirstFileA(path, &ffd);
        int first = 1;
        if (hf != INVALID_HANDLE_VALUE) {
            do {
                if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) continue;
                    if (!first) { n = snprintf(p, 32768 - (p - result), ","); p += n; }
                    first = 0;
                    char *en = json_escape(ffd.cFileName);
                    n = snprintf(p, 32768 - (p - result), "{\"name\":%s,\"appid\":\"0\",\"path\":\"%s\\%s\"}", en ? en : "\"\"", common, ffd.cFileName);
                    p += n;
                    free(en);
                }
            } while (FindNextFileA(hf, &ffd));
            FindClose(hf);
        }
        n = snprintf(p, 32768 - (p - result), "],\"steam_path\":\"%s\"}", g_exe_dir_global);
        p += n;
        return result;
    }

    /* ---------- 22. launch_game ---------- */
    if (strcmp(cmd, "launch_game") == 0) {
        if (!args) return my_strdup("false");
        char url[128];
        snprintf(url, sizeof(url), "steam://rungameid/%s", args);
        ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
        return my_strdup("true");
    }

    /* ---------- 23. open_folder ---------- */
    if (strcmp(cmd, "open_folder") == 0) {
        if (!args) return my_strdup("false");
        ShellExecuteA(NULL, "open", args, NULL, NULL, SW_SHOWNORMAL);
        return my_strdup("true");
    }

    /* ---------- 24. verify_games_batch ---------- */
    if (strcmp(cmd, "verify_games_batch") == 0) {
        if (!args || *args != '[') return my_strdup("{}");
        char *result = (char*)malloc(65536);
        if (!result) return my_strdup("{}");
        char *p = result, *end = result + 65536;
        p += snprintf(p, end - p, "{");
        // Parse array ["name1","name2",...]
        char *copy = my_strdup(args);
        char *token = copy;
        int first = 1;
        while (token && *token) {
            // Skip to string start
            while (*token && *token != '"') token++;
            if (!*token) break;
            token++;
            char *str_end = token;
            while (*str_end && *str_end != '"') str_end++;
            if (!*str_end) break;
            *str_end = '\0';
            // Now token is the name
            if (!first) { p += snprintf(p, end - p, ","); }
            first = 0;
            char *en = json_escape(token);
            // For each game, check via steamapi (simplified: return true)
            p += snprintf(p, end - p, "%s:true", en ? en : "\"\"");
            free(en);
            token = str_end + 1;
        }
        p += snprintf(p, end - p, "}");
        free(copy);
        return result;
    }

    /* ---------- 25. ai_translate ---------- */
    if (strcmp(cmd, "ai_translate") == 0) {
        if (!args) return my_strdup("{\"translated\":\"\",\"provider\":\"\"}");
        // Use Google Translate
        char *encoded = url_encode_simple(args);
        if (!encoded) return my_strdup("{\"translated\":\"\",\"provider\":\"\"}");
        char url[8192];
        snprintf(url, sizeof(url), "https://translate.googleapis.com/translate_a/single?client=gtx&sl=auto&tl=tr&dt=t&q=%s", encoded);
        free(encoded);
        char *resp = http_get(url, "User-Agent: Mozilla/5.0");
        if (resp && resp[0] == '[' && resp[1] == '[') {
            // Parse: [[["translated","original",...],...],...]
            char *start = resp + 2;
            if (*start == '[') start++;
            if (*start == '"') {
                start++;
                char *t_end = start;
                while (*t_end && *t_end != '"') {
                    if (*t_end == '\\' && *(t_end+1)) t_end++;
                    t_end++;
                }
                char saved = *t_end; *t_end = '\0';
                char *esc = json_escape(start);
                char *r = (char*)malloc(strlen(esc) + 100);
                if (r) snprintf(r, strlen(esc) + 100, "{\"translated\":%s,\"provider\":\"google\"}", esc ? esc : "\"\"");
                free(esc); *t_end = saved; free(resp);
                return r ? r : my_strdup("{\"translated\":\"\",\"provider\":\"\"}");
            }
            free(resp);
        } else {
            free(resp);
        }
        return my_strdup("{\"translated\":\"\",\"provider\":\"\"}");
    }

    /* ---------- 26. restart_steam ---------- */
    if (strcmp(cmd, "restart_steam") == 0) {
        // Kill steam
        system("taskkill /f /im steam.exe 2>nul");
        // Wait and restart
        Sleep(3000);
        ShellExecuteA(NULL, "open", "steam://", NULL, NULL, SW_SHOWNORMAL);
        return my_strdup("{\"ok\":true}");
    }

    /* ---------- 27. get_ai_providers ---------- */
    if (strcmp(cmd, "get_ai_providers") == 0) {
        return my_strdup("[{\"id\":\"google\",\"label\":\"Google Translate\"},{\"id\":\"openai\",\"label\":\"OpenAI\"},{\"id\":\"deepseek\",\"label\":\"DeepSeek\"},{\"id\":\"claude\",\"label\":\"Claude\"}]");
    }

    /* ---------- 28. get_preferred_provider ---------- */
    if (strcmp(cmd, "get_preferred_provider") == 0) {
        char *val = RegReadStr(HKEY_CURRENT_USER, "Software\\SteamTools", "preferred_provider");
        if (!val || !*val) { free(val); return build_json_string("google"); }
        char *r = build_json_string(val);
        free(val);
        return r;
    }

    /* ---------- 29. get_ai_key ---------- */
    if (strcmp(cmd, "get_ai_key") == 0) {
        if (!args) return build_json_string("");
        char subkey[256];
        snprintf(subkey, sizeof(subkey), "Software\\SteamTools\\key_%s", args);
        char *val = RegReadStr(HKEY_CURRENT_USER, subkey, "");
        if (!val) return build_json_string("");
        char *r = build_json_string(val);
        free(val);
        return r;
    }

    /* ---------- 30. save_ai_key ---------- */
    if (strcmp(cmd, "save_ai_key") == 0) {
        if (!args) return build_json_string("ok");
        char *providerId = GetJsonStr(args, "providerId");
        char *key = GetJsonStr(args, "key");
        if (providerId && key) {
            char subkey[256];
            snprintf(subkey, sizeof(subkey), "Software\\SteamTools\\key_%s", providerId);
            RegWriteStr(HKEY_CURRENT_USER, subkey, "", key);
        }
        free(providerId); free(key);
        return build_json_string("ok");
    }

    /* ---------- 31. set_preferred_provider ---------- */
    if (strcmp(cmd, "set_preferred_provider") == 0) {
        if (args) RegWriteStr(HKEY_CURRENT_USER, "Software\\SteamTools", "preferred_provider", args);
        return build_json_string("ok");
    }

    return NULL;
}
