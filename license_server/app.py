import os, json, base64, requests
from flask import Flask, render_template, jsonify, send_file
from io import BytesIO

app = Flask(__name__)
TOKEN = os.environ.get('GH_TOKEN')
if not TOKEN:
    raise RuntimeError('GH_TOKEN env variable not set')
API_URL = 'https://api.github.com/repos/tttaaahhhaaa/SteamToolsLua/contents/licenses.json?ref=secret-data'
PUT_URL = 'https://api.github.com/repos/tttaaahhhaaa/SteamToolsLua/contents/licenses.json'
HEADERS = {'Authorization': f'Bearer {TOKEN}', 'Accept': 'application/vnd.github.v3+json'}
DOWNLOAD_URL = 'https://github.com/tttaaahhhaaa/SteamToolsLua/releases/download/v1.7.9/SteamToolsLua_v1.7.9.exe'

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/getcode')
def getcode():
    try:
        resp = requests.get(API_URL, headers=HEADERS, timeout=10)
        if resp.status_code != 200:
            return jsonify({'error': 'Cannot connect to server'}), 500
        data = resp.json()
        sha = data['sha']
        codes = json.loads(base64.b64decode(data['content']).decode('utf-8'))
        for code, val in codes['codes'].items():
            if val is None:
                codes['codes'][code] = 'issued'
                new_b64 = base64.b64encode(json.dumps(codes, indent=2).encode()).decode()
                put = requests.put(PUT_URL, headers=HEADERS,
                    json={'message': f'Issue {code}', 'content': new_b64, 'sha': sha, 'branch': 'secret-data'}, timeout=10)
                if put.status_code in (200, 201):
                    return jsonify({'code': code})
                else:
                    return jsonify({'error': 'Failed to issue code'}), 500
        return jsonify({'error': 'No codes left'}), 404
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/download')
def download():
    return jsonify({'url': DOWNLOAD_URL})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
