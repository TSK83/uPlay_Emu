// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Master Server: Validation Middleware
// ═══════════════════════════════════════════════════════════════════════
//  Strict JSON schema validation — rejects payloads with unknown fields
//  (conservative API design) and validates required fields + types.
// ═══════════════════════════════════════════════════════════════════════

/**
 * Validate server registration payload.
 * Required fields: host_name, max_players, heartbeat_interval, game_id, host_socket_id
 * All property names must be snake_case.
 */
function validateServerCreate(req, res, next) {
    const body = req.body;

    if (!body || typeof body !== 'object') {
        return res.status(400).json({
            error: 'invalid_request',
            message: 'Request body must be a JSON object'
        });
    }

    // ── Required Fields ───────────────────────────────────────────────
    const requiredFields = {
        host_socket_id:     'string',
        max_players:        'number',
        heartbeat_interval: 'number',
        game_id:            'string',
    };

    for (const [field, type] of Object.entries(requiredFields)) {
        if (body[field] === undefined || body[field] === null) {
            return res.status(400).json({
                error: 'missing_field',
                message: `Required field "${field}" is missing`,
                field
            });
        }
        if (typeof body[field] !== type) {
            return res.status(400).json({
                error: 'invalid_type',
                message: `Field "${field}" must be of type ${type}, got ${typeof body[field]}`,
                field
            });
        }
    }

    // ── Optional Fields ───────────────────────────────────────────────
    const allowedFields = new Set([
        'host_socket_id', 'max_players', 'heartbeat_interval', 'game_id',
        'host_name', 'current_players', 'map_name', 'host_ip', 'host_port',
        'is_dedicated'
    ]);

    // Reject unknown fields (conservative API design)
    for (const key of Object.keys(body)) {
        if (!allowedFields.has(key)) {
            return res.status(400).json({
                error: 'unknown_field',
                message: `Unknown field "${key}" — strict schema validation rejects undefined fields`,
                field: key
            });
        }
    }

    // ── Value Validation ──────────────────────────────────────────────
    if (body.max_players < 1 || body.max_players > 64) {
        return res.status(400).json({
            error: 'invalid_value',
            message: 'max_players must be between 1 and 64',
            field: 'max_players'
        });
    }

    if (body.heartbeat_interval < 5000 || body.heartbeat_interval > 300000) {
        return res.status(400).json({
            error: 'invalid_value',
            message: 'heartbeat_interval must be between 5000 and 300000 ms',
            field: 'heartbeat_interval'
        });
    }

    if (body.current_players !== undefined && typeof body.current_players !== 'number') {
        return res.status(400).json({
            error: 'invalid_type',
            message: 'current_players must be a number',
            field: 'current_players'
        });
    }

    // ── Snake_case Enforcement ────────────────────────────────────────
    const camelCaseRegex = /[A-Z]/;
    for (const key of Object.keys(body)) {
        if (camelCaseRegex.test(key)) {
            return res.status(400).json({
                error: 'naming_violation',
                message: `Field "${key}" violates snake_case naming convention`,
                field: key
            });
        }
    }

    next();
}

/**
 * Validate server update payload.
 */
function validateServerUpdate(req, res, next) {
    const body = req.body;

    if (!body || typeof body !== 'object') {
        return res.status(400).json({
            error: 'invalid_request',
            message: 'Request body must be a JSON object'
        });
    }

    const allowedFields = new Set([
        'current_players', 'map_name', 'host_ip', 'host_port', 'ping_ms'
    ]);

    for (const key of Object.keys(body)) {
        if (!allowedFields.has(key)) {
            return res.status(400).json({
                error: 'unknown_field',
                message: `Unknown field "${key}" — only [${[...allowedFields].join(', ')}] can be updated`,
                field: key
            });
        }
    }

    next();
}

module.exports = { validateServerCreate, validateServerUpdate };
