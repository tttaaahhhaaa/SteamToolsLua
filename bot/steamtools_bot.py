import discord
from discord import app_commands
from discord.ext import commands
import hashlib
import hmac
import json
import os
import time
import threading
import logging
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
from pathlib import Path

# ===== CONFIG (env vars or defaults) =====
BOT_TOKEN = os.environ.get("DISCORD_BOT_TOKEN", "")
if not BOT_TOKEN:
    log.error("DISCORD_BOT_TOKEN environment variable not set!")
    exit(1)
SECRET_KEY = os.environ.get("STL_SECRET", "stl_secret_2024_xK9mP2vL5nR7qW3j")
DOWNLOAD_URL = os.environ.get("STL_DOWNLOAD_URL", "https://github.com/tttaaahhhaaa/SteamToolsLua/releases/latest")
CODES_DIR = Path(__file__).parent / "codes"
CODES_DIR.mkdir(exist_ok=True)
VALIDITY_HOURS = 24
API_PORT = int(os.environ.get("API_PORT", "8899"))

logging.basicConfig(level=logging.INFO, format="[%(asctime)s] %(message)s", datefmt="%H:%M:%S")
log = logging.getLogger("bot")

# ===== CODE GENERATION =====
def generate_code(user_id: int, username: str) -> str:
    expiry = int(time.time()) + (VALIDITY_HOURS * 3600)
    payload = f"{user_id}:{expiry}"
    sig = hmac.new(SECRET_KEY.encode(), payload.encode(), hashlib.sha256).hexdigest()[:16]
    code = f"STL-{user_id:X}-{expiry:X}-{sig}"
    codes_file = CODES_DIR / "active_codes.json"
    codes = {}
    if codes_file.exists():
        try: codes = json.loads(codes_file.read_text("utf-8"))
        except: pass
    codes[str(user_id)] = {
        "username": username,
        "code": code,
        "expiry": expiry,
        "created": int(time.time())
    }
    codes_file.write_text(json.dumps(codes, indent=2, ensure_ascii=False), encoding="utf-8")
    log.info(f"Code generated for {username} ({user_id})")
    return code

def validate_code(code: str) -> dict:
    try:
        parts = code.split("-")
        if len(parts) != 4 or parts[0] != "STL":
            return {"valid": False, "reason": "Invalid format"}
        user_id = int(parts[1], 16)
        expiry = int(parts[2], 16)
        sig = parts[3]
        payload = f"{user_id}:{expiry}"
        expected_sig = hmac.new(SECRET_KEY.encode(), payload.encode(), hashlib.sha256).hexdigest()[:16]
        if not hmac.compare_digest(sig, expected_sig):
            return {"valid": False, "reason": "Invalid signature"}
        now = int(time.time())
        remaining = expiry - now
        if remaining <= 0:
            return {"valid": False, "reason": "Expired", "user_id": user_id, "remaining": 0}
        return {"valid": True, "user_id": user_id, "expiry": expiry, "remaining": remaining}
    except:
        return {"valid": False, "reason": "Parse error"}

# ===== HTTP API =====
class APIHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/validate":
            params = parse_qs(parsed.query)
            code = params.get("code", [""])[0]
            if not code:
                self.send_json(400, {"error": "Missing code parameter"})
                return
            result = validate_code(code)
            self.send_json(200, result)
        elif parsed.path == "/api/health":
            self.send_json(200, {"status": "ok", "version": "4.0.0", "uptime": int(time.time())})
        elif parsed.path == "/api/stats":
            codes_file = CODES_DIR / "active_codes.json"
            if codes_file.exists():
                try:
                    codes = json.loads(codes_file.read_text("utf-8"))
                    now = int(time.time())
                    active = sum(1 for c in codes.values() if c.get("expiry", 0) > now)
                    self.send_json(200, {"active": active, "total": len(codes)})
                except:
                    self.send_json(200, {"active": 0, "total": 0})
            else:
                self.send_json(200, {"active": 0, "total": 0})
        else:
            self.send_json(404, {"error": "Not found"})

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/validate":
            length = int(self.headers.get("Content-Length", 0))
            body = json.loads(self.rfile.read(length)) if length > 0 else {}
            code = body.get("code", "")
            if not code:
                self.send_json(400, {"error": "Missing code"})
                return
            result = validate_code(code)
            self.send_json(200, result)
        else:
            self.send_json(404, {"error": "Not found"})

    def send_json(self, status, data):
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())

    def log_message(self, format, *args):
        pass

def run_api():
    server = HTTPServer(("0.0.0.0", API_PORT), APIHandler)
    log.info(f"API listening on port {API_PORT}")
    server.serve_forever()

# ===== DISCORD BOT =====
intents = discord.Intents.default()
intents.message_content = True
bot = commands.Bot(command_prefix="!", intents=intents)

@bot.event
async def on_ready():
    log.info(f"Bot logged in as {bot.user} (ID: {bot.user.id})")
    try:
        synced = await bot.tree.sync()
        log.info(f"Synced {len(synced)} command(s)")
    except Exception as e:
        log.error(f"Sync failed: {e}")

@bot.tree.command(name="code", description="Get a 24-hour activation code for SteamToolsLua")
async def code_command(interaction: discord.Interaction):
    user_id = interaction.user.id
    username = interaction.user.name
    code = generate_code(user_id, username)
    embed = discord.Embed(
        title="SteamToolsLua Activation Code",
        description=f"Your **24-hour** activation code:\n```\n{code}\n```",
        color=discord.Color.green()
    )
    embed.add_field(name="Valid for", value="24 hours from now", inline=True)
    embed.add_field(name="How to use", value="Open SteamToolsLua -> Enter code when prompted", inline=False)
    embed.set_footer(text="Code expires after 24 hours. Get a new one with /code")
    await interaction.response.send_message(embed=embed, ephemeral=True)

@bot.tree.command(name="download", description="Download SteamToolsLua")
async def download_command(interaction: discord.Interaction):
    embed = discord.Embed(
        title="SteamToolsLua Download",
        description=f"[Click here to download the latest version]({DOWNLOAD_URL})",
        color=discord.Color.blue()
    )
    embed.set_footer(text="SteamToolsLua v4.0.0 - All-in-One Injector")
    await interaction.response.send_message(embed=embed, ephemeral=True)

@bot.tree.command(name="check", description="Check code statistics (admin only)")
@app_commands.checks.has_permissions(administrator=True)
async def check_command(interaction: discord.Interaction):
    codes_file = CODES_DIR / "active_codes.json"
    if not codes_file.exists():
        await interaction.response.send_message("No active codes.", ephemeral=True)
        return
    codes = json.loads(codes_file.read_text("utf-8"))
    now = int(time.time())
    active = sum(1 for c in codes.values() if c.get("expiry", 0) > now)
    expired = len(codes) - active
    embed = discord.Embed(title="Code Statistics", color=discord.Color.blue())
    embed.add_field(name="Active codes", value=str(active), inline=True)
    embed.add_field(name="Expired codes", value=str(expired), inline=True)
    embed.add_field(name="Total users", value=str(len(codes)), inline=True)
    await interaction.response.send_message(embed=embed, ephemeral=True)

@bot.tree.command(name="revoke", description="Revoke a user's code (admin only)")
@app_commands.checks.has_permissions(administrator=True)
async def revoke_command(interaction: discord.Interaction, user: discord.Member):
    codes_file = CODES_DIR / "active_codes.json"
    if not codes_file.exists():
        await interaction.response.send_message("No active codes.", ephemeral=True)
        return
    codes = json.loads(codes_file.read_text("utf-8"))
    uid = str(user.id)
    if uid in codes:
        del codes[uid]
        codes_file.write_text(json.dumps(codes, indent=2, ensure_ascii=False), encoding="utf-8")
        await interaction.response.send_message(f"Revoked code for {user.name}", ephemeral=True)
    else:
        await interaction.response.send_message(f"No code found for {user.name}", ephemeral=True)

# ===== START =====
if __name__ == "__main__":
    api_thread = threading.Thread(target=run_api, daemon=True)
    api_thread.start()
    log.info("Starting Discord bot...")
    bot.run(BOT_TOKEN, log_handler=None)
