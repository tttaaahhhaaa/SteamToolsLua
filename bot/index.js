const { Client, GatewayIntentBits, SlashCommandBuilder, REST, Routes, PermissionFlagsBits } = require('discord.js');
const crypto = require('crypto');

// ===== CONFIG =====
const BOT_TOKEN = process.env.DISCORD_BOT_TOKEN || '';
const SECRET_KEY = process.env.STL_SECRET || 'stl_secret_2024_xK9mP2vL5nR7qW3j';
const DOWNLOAD_URL = process.env.STL_DOWNLOAD_URL || 'https://github.com/tttaaahhhaaa/SteamToolsLua/releases/latest';
const VALIDITY_HOURS = 24;

if (!BOT_TOKEN) {
    console.error('DISCORD_BOT_TOKEN not set!');
    process.exit(1);
}

// ===== CODE GENERATION =====
function generateCode(userId, username) {
    const expiry = Math.floor(Date.now() / 1000) + (VALIDITY_HOURS * 3600);
    const payload = `${userId}:${expiry}`;
    const sig = crypto.createHmac('sha256', SECRET_KEY).update(payload).digest('hex').slice(0, 16);
    const code = `STL-${userId.toString(16).toUpperCase()}-${expiry.toString(16).toUpperCase()}-${sig}`;
    console.log(`Code generated for ${username} (${userId})`);
    return { code, expiry };
}

function validateCode(code) {
    try {
        const parts = code.split('-');
        if (parts.length !== 4 || parts[0] !== 'STL') return { valid: false, reason: 'Invalid format' };
        const userId = parseInt(parts[1], 16);
        const expiry = parseInt(parts[2], 16);
        const sig = parts[3];
        const payload = `${userId}:${expiry}`;
        const expectedSig = crypto.createHmac('sha256', SECRET_KEY).update(payload).digest('hex').slice(0, 16);
        if (sig !== expectedSig) return { valid: false, reason: 'Invalid signature' };
        const now = Math.floor(Date.now() / 1000);
        if (now > expiry) return { valid: false, reason: 'Expired', userId, remaining: 0 };
        return { valid: true, userId, expiry, remaining: expiry - now };
    } catch {
        return { valid: false, reason: 'Parse error' };
    }
}

// ===== BOT =====
const client = new Client({
    intents: [GatewayIntentBits.Guilds]
});

const commands = [
    new SlashCommandBuilder()
        .setName('code')
        .setDescription('Get a 24-hour activation code for SteamToolsLua'),
    new SlashCommandBuilder()
        .setName('download')
        .setDescription('Download SteamToolsLua'),
    new SlashCommandBuilder()
        .setName('check')
        .setDescription('Check code statistics (admin only)')
        .setDefaultMemberPermissions(PermissionFlagsBits.Administrator),
    new SlashCommandBuilder()
        .setName('revoke')
        .setDescription('Revoke a user code (admin only)')
        .setDefaultMemberPermissions(PermissionFlagsBits.Administrator)
        .addUserOption(option => option.setName('user').setDescription('User to revoke').setRequired(true))
].map(cmd => cmd.toJSON());

async function registerCommands() {
    const rest = new REST({ version: '10' }).setToken(BOT_TOKEN);
    try {
        console.log('Registering slash commands...');
        await rest.put(Routes.applicationCommands(client.user.id), { body: commands });
        console.log('Commands registered!');
    } catch (err) {
        console.error('Command registration failed:', err);
    }
}

client.once('ready', async () => {
    console.log(`Bot logged in as ${client.user.tag}`);
    await registerCommands();
});

client.on('interactionCreate', async interaction => {
    if (!interaction.isChatInputCommand()) return;

    if (interaction.commandName === 'code') {
        const userId = interaction.user.id;
        const username = interaction.user.username;
        const { code, expiry } = generateCode(userId, username);
        const embed = {
            title: 'SteamToolsLua Activation Code',
            description: `Your **24-hour** activation code:\n\`\`\`\n${code}\n\`\`\``,
            color: 0x48bb78,
            fields: [
                { name: 'Valid for', value: '24 hours from now', inline: true },
                { name: 'How to use', value: 'Open SteamToolsLua -> Enter code when prompted', inline: false }
            ],
            footer: { text: 'Code expires after 24 hours. Get a new one with /code' }
        };
        await interaction.reply({ embeds: [embed], ephemeral: true });
    }

    else if (interaction.commandName === 'download') {
        const embed = {
            title: 'SteamToolsLua Download',
            description: `[Click here to download the latest version](${DOWNLOAD_URL})`,
            color: 0x4299e1,
            footer: { text: 'SteamToolsLua v4.0.0 - All-in-One Injector' }
        };
        await interaction.reply({ embeds: [embed], ephemeral: true });
    }

    else if (interaction.commandName === 'check') {
        const embed = {
            title: 'Code Statistics',
            color: 0x4299e1,
            fields: [
                { name: 'Status', value: 'Bot is running', inline: true }
            ]
        };
        await interaction.reply({ embeds: [embed], ephemeral: true });
    }

    else if (interaction.commandName === 'revoke') {
        const targetUser = interaction.options.getUser('user');
        await interaction.reply({
            content: `Code revoke requested for ${targetUser.username}. Codes are validated locally - expired codes auto-invalidate.`,
            ephemeral: true
        });
    }
});

client.login(BOT_TOKEN);
