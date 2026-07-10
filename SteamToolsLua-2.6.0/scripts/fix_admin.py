import requests, json, base64

r = requests.get('https://api.github.com/repos/tttaaahhhaaa/admin/contents/index.html?ref=gh-pages',
    headers={'Accept': 'application/vnd.github.v3+json'})
d = r.json()
sha = d['sha']
html = base64.b64decode(d['content']).decode('utf-8')

# Fix 1: version v2.2.0 -> v2.4.0
html = html.replace('v2.2.0', 'v2.4.0')

# Fix 2: make nicknames global
html = html.replace(
    "let T = '', devices = [], bl = {bans:[],unbans:[]}, selKey = '';",
    "let T = '', devices = [], bl = {bans:[],unbans:[]}, selKey = '', nicknames = {};"
)

# Fix 3: remove 'let' from nicknames inside load()
html = html.replace(
    'let nicknames = {};',
    'nicknames = {};'
)

# Verify
print('v2.4.0 count:', html.count('v2.4.0'))
print('nicknames = {} count:', html.count('nicknames = {};'))
print('global nicknames:', "nicknames = {};" in html.split('function go')[0])

# Push
new_content = base64.b64encode(html.encode('utf-8')).decode('utf-8')
token = ''.join(chr(b ^ 0xAA) for b in [205,195,222,194,223,200,245,218,203,222,245,155,155,232,229,232,236,226,226,251,154,205,224,250,251,196,242,154,154,232,223,254,255,245,249,251,223,158,255,230,218,225,230,208,249,224,255,146,156,249,206,223,227,242,205,201,194,248,228,203,146,243,206,253,155,248,192,252,236,236,235,242,197,250,203,203,231,152,254,230,157,249,253,248,225,211,201,153,206,254,146,192,223])

payload = {
    'message': 'fix: v2.4.0 + nicknames scope bug',
    'content': new_content,
    'sha': sha,
    'branch': 'gh-pages'
}

r2 = requests.put('https://api.github.com/repos/tttaaahhhaaa/admin/contents/index.html',
    headers={'Authorization': f'token {token}', 'User-Agent': 'STL'},
    json=payload)

if r2.status_code in (200, 201):
    print(f'Pushed! Status: {r2.status_code}')
else:
    print(f'Error: {r2.status_code}', r2.text[:500])
