// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Master Server: /servers Routes
// ═══════════════════════════════════════════════════════════════════════
//  RESTful API (REST Maturity Level 2) for lobby management:
//    POST   /servers              — Register a new lobby
//    GET    /servers              — List lobbies (cursor-based pagination)
//    GET    /servers/:id          — Get specific server details
//    PUT    /servers/:id          — Update server data
//    PUT    /servers/:id/heartbeat — Heartbeat ping
//    DELETE /servers/:id          — Deregister lobby
//    GET    /servers/join/:code   — Lookup by 6-character join code
// ═══════════════════════════════════════════════════════════════════════

const express = require('express');
const router = express.Router();
const store = require('../store');
const { validateServerCreate, validateServerUpdate } = require('../middleware/validation');

// ── POST /servers — Register new lobby ────────────────────────────────
router.post('/', validateServerCreate, (req, res) => {
    // Capture host IP from the request if not provided in body
    const data = { ...req.body };
    if (!data.host_ip) {
        data.host_ip = req.ip || req.connection?.remoteAddress || '';
    }

    const entry = store.create(data);

    res.status(201).json({
        id:              entry.id,
        join_code:       entry.join_code,
        host_name:       entry.host_name,
        max_players:     entry.max_players,
        current_players: entry.current_players,
        game_id:         entry.game_id,
        map_name:        entry.map_name,
        created_at:      entry.created_at,
    });
});

// ── GET /servers — List lobbies with cursor-based pagination ──────────
router.get('/', (req, res) => {
    const cursor = req.query.cursor || null;
    const limit  = Math.min(Math.max(parseInt(req.query.limit, 10) || 20, 1), 100);

    const { servers, next_cursor } = store.list(cursor, limit);

    res.json({
        servers: servers.map(s => ({
            id:              s.id,
            host_name:       s.host_name,
            join_code:       s.join_code,
            game_id:         s.game_id,
            map_name:        s.map_name,
            current_players: s.current_players,
            max_players:     s.max_players,
            host_ip:         s.host_ip,
            host_port:       s.host_port,
            ping_ms:         s.ping_ms,
            is_dedicated:    s.is_dedicated,
            created_at:      s.created_at,
        })),
        next_cursor,
        total_count: store.count(),
    });
});

// ── GET /servers/join/:code — Lookup by join code ─────────────────────
// IMPORTANT: This route MUST be defined before /servers/:id to avoid
// "join" being interpreted as a server ID.
router.get('/join/:code', (req, res) => {
    const code = req.params.code.toUpperCase();
    const entry = store.getByJoinCode(code);

    if (!entry) {
        return res.status(404).json({
            error: 'not_found',
            message: `No server found with join code "${code}"`
        });
    }

    res.json({
        id:              entry.id,
        host_name:       entry.host_name,
        join_code:       entry.join_code,
        game_id:         entry.game_id,
        map_name:        entry.map_name,
        current_players: entry.current_players,
        max_players:     entry.max_players,
        host_ip:         entry.host_ip,
        host_port:       entry.host_port,
        ping_ms:         entry.ping_ms,
        is_dedicated:    entry.is_dedicated,
    });
});

// ── GET /servers/:id — Get specific server ────────────────────────────
router.get('/:id', (req, res) => {
    const entry = store.get(req.params.id);

    if (!entry) {
        return res.status(404).json({
            error: 'not_found',
            message: `Server "${req.params.id}" not found`
        });
    }

    res.json(entry);
});

// ── PUT /servers/:id — Update server data ─────────────────────────────
router.put('/:id', validateServerUpdate, (req, res) => {
    const updated = store.update(req.params.id, req.body);

    if (!updated) {
        return res.status(404).json({
            error: 'not_found',
            message: `Server "${req.params.id}" not found`
        });
    }

    res.json(updated);
});

// ── PUT /servers/:id/heartbeat — Heartbeat ping ──────────────────────
router.put('/:id/heartbeat', (req, res) => {
    const success = store.heartbeat(req.params.id);

    if (!success) {
        return res.status(404).json({
            error: 'not_found',
            message: `Server "${req.params.id}" not found (may have been pruned)`
        });
    }

    res.json({
        status: 'ok',
        server_id: req.params.id,
        timestamp: Date.now(),
    });
});

// ── DELETE /servers/:id — Deregister lobby ────────────────────────────
router.delete('/:id', (req, res) => {
    const removed = store.remove(req.params.id);

    if (!removed) {
        return res.status(404).json({
            error: 'not_found',
            message: `Server "${req.params.id}" not found`
        });
    }

    res.status(204).send();
});

module.exports = router;
