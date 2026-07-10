import requests
token = 'YOUR_GITHUB_TOKEN'
headers = {'Authorization': f'token {token}', 'Accept': 'application/vnd.github.v3+json'}
r = requests.get('https://api.github.com/repos/tttaaahhhaaa/SteamToolsLua/releases/tags/v1.0.4', headers=headers)
rel = r.json()
for asset in rel.get('assets', []):
    if asset['name'] == 'SteamToolsLua.exe':
        aid = asset['id']
        rd = requests.delete(f'https://api.github.com/repos/tttaaahhhaaa/SteamToolsLua/releases/assets/{aid}', headers=headers)
        print(f'Deleted asset {aid}: {rd.status_code}')
url = f'https://uploads.github.com/repos/tttaaahhhaaa/SteamToolsLua/releases/{rel["id"]}/assets?name=SteamToolsLua.exe'
with open(r'C:\Users\Taha\Desktop\SteamToolsLua_Repo\dist\SteamToolsLua.exe', 'rb') as f:
    r2 = requests.post(url, headers={**headers, 'Content-Type': 'application/octet-stream'}, data=f)
print(f'Upload: {r2.status_code} {r2.json().get("name", "")}')
