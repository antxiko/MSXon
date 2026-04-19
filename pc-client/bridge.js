'use strict';

// =============================================================================
// bridge.js — TCP↔WebSocket bridge for MSXon PC Client
//
// Usage:
//   node bridge.js [server_ip] [server_port]
//   → Open http://localhost:8080 in your browser
// =============================================================================

const net       = require('net');
const http      = require('http');
const fs        = require('fs');
const path      = require('path');
const { WebSocketServer } = require('ws');

const BRIDGE_PORT = 8080;
const SERVER_IP   = process.argv[2] || '127.0.0.1';
const SERVER_PORT = parseInt(process.argv[3]) || 9876;

// ── HTTP server (serves index.html) ─────────────────────────────
const htmlPath = path.join(__dirname, 'index.html');

const httpServer = http.createServer((req, res) => {
  fs.readFile(htmlPath, (err, data) => {
    if (err) { res.writeHead(500); res.end('Error'); return; }
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end(data);
  });
});

// ── WebSocket server ────────────────────────────────────────────
const wss = new WebSocketServer({ server: httpServer });

wss.on('connection', (ws) => {
  console.log('[WS] Client connected — opening TCP to game server...');

  const tcp = net.createConnection({ host: SERVER_IP, port: SERVER_PORT }, () => {
    console.log(`[TCP] Connected to ${SERVER_IP}:${SERVER_PORT}`);
  });

  // TCP → WebSocket
  tcp.on('data', (chunk) => {
    console.log(`[TCP→WS] ${[...chunk].map(b => b.toString(16).padStart(2,'0')).join(' ')}`);
    if (ws.readyState === 1) ws.send(chunk);
  });

  tcp.on('error', (err) => {
    console.error(`[TCP] Error: ${err.message}`);
    ws.close();
  });

  tcp.on('close', () => {
    console.log('[TCP] Disconnected');
    ws.close();
  });

  // WebSocket → TCP
  ws.on('message', (data) => {
    const buf = Buffer.from(data);
    console.log(`[WS→TCP] ${[...buf].map(b => b.toString(16).padStart(2,'0')).join(' ')}`);
    if (!tcp.destroyed) tcp.write(buf);
  });

  ws.on('close', () => {
    console.log('[WS] Client disconnected');
    tcp.destroy();
  });

  ws.on('error', () => {
    tcp.destroy();
  });
});

// ── Start ───────────────────────────────────────────────────────
httpServer.listen(BRIDGE_PORT, () => {
  console.log(`\nMSXon — PC Client Bridge`);
  console.log(`━━━━━━━━━━━━━━━━━━━━━━━━━━━━━`);
  console.log(`Game server:  ${SERVER_IP}:${SERVER_PORT}`);
  console.log(`Open browser: http://localhost:${BRIDGE_PORT}\n`);
});
