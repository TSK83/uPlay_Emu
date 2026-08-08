// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Master Server Entry Point
// ═══════════════════════════════════════════════════════════════════════
//  Express.js server with JSON body parsing, CORS headers, and route
//  mounting. Configurable via environment variables.
// ═══════════════════════════════════════════════════════════════════════

const express = require('express');
const app = express();

const PORT = parseInt(process.env.PORT, 10) || 3000;
const HOST = process.env.HOST || '0.0.0.0';

// ── Middleware ─────────────────────────────────────────────────────────
app.use(express.json({ limit: '1mb' }));

// CORS — allow requests from any origin (game client doesn't set Origin header)
app.use((req, res, next) => {
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
    res.header('Access-Control-Allow-Headers', 'Content-Type, Authorization');
    if (req.method === 'OPTIONS') {
        return res.sendStatus(204);
    }
    next();
});

// Request logging
app.use((req, res, next) => {
    const start = Date.now();
    res.on('finish', () => {
        const duration = Date.now() - start;
        console.log(`[${new Date().toISOString()}] ${req.method} ${req.url} → ${res.statusCode} (${duration}ms)`);
    });
    next();
});

// ── Routes ────────────────────────────────────────────────────────────
const serversRouter = require('./routes/servers');
app.use('/servers', serversRouter);

// Health check
app.get('/health', (req, res) => {
    const store = require('./store');
    res.json({
        status:         'healthy',
        uptime_seconds: Math.floor(process.uptime()),
        server_count:   store.count(),
        version:        '1.0.0',
    });
});

// API info
app.get('/', (req, res) => {
    res.json({
        name:        'ACU Custom Client — Master Server',
        version:     '1.0.0',
        description: 'Custom multiplayer lobby server for Assassin\'s Creed Unity',
        endpoints: {
            'GET /health':                 'Health check',
            'POST /servers':               'Register a new lobby',
            'GET /servers':                'List lobbies (cursor-based pagination)',
            'GET /servers/:id':            'Get server details',
            'PUT /servers/:id':            'Update server data',
            'PUT /servers/:id/heartbeat':  'Heartbeat ping',
            'DELETE /servers/:id':         'Deregister lobby',
            'GET /servers/join/:code':     'Lookup by join code',
        }
    });
});

// 404 handler
app.use((req, res) => {
    res.status(404).json({
        error: 'not_found',
        message: `Route ${req.method} ${req.url} not found`
    });
});

// Error handler
app.use((err, req, res, next) => {
    console.error(`[ERROR] ${err.message}`);
    console.error(err.stack);
    res.status(500).json({
        error: 'internal_error',
        message: 'An unexpected error occurred'
    });
});

// ── Start Server ──────────────────────────────────────────────────────
app.listen(PORT, HOST, () => {
    console.log('');
    console.log('  ╔═══════════════════════════════════════════════════════╗');
    console.log('  ║     ACU Custom Client — Master Server v1.0.0         ║');
    console.log('  ╚═══════════════════════════════════════════════════════╝');
    console.log('');
    console.log(`  Listening on http://${HOST}:${PORT}`);
    console.log('');
    console.log('  Endpoints:');
    console.log('    POST   /servers              Register lobby');
    console.log('    GET    /servers              List lobbies');
    console.log('    GET    /servers/join/:code   Lookup by join code');
    console.log('    PUT    /servers/:id/heartbeat  Heartbeat');
    console.log('    DELETE /servers/:id          Remove lobby');
    console.log('');
});

module.exports = app;
