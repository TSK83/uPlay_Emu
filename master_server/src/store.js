// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Master Server: In-Memory Store
// ═══════════════════════════════════════════════════════════════════════
//  Uses a Map<id, ServerEntry> for O(1) lookups and insertion-order
//  iteration (required for cursor-based pagination). Heartbeat tracking
//  uses setTimeout-based pruning with 1.5x interval tolerance.
// ═══════════════════════════════════════════════════════════════════════

const { v4: uuidv4 } = require('uuid');
const { customAlphabet } = require('nanoid');

// Generate 6-character uppercase alphanumeric join codes
const generateJoinCode = customAlphabet('ABCDEFGHJKLMNPQRSTUVWXYZ23456789', 6);

class ServerStore {
    constructor() {
        /** @type {Map<string, Object>} */
        this.servers = new Map();

        /** @type {Map<string, NodeJS.Timeout>} */
        this.pruneTimers = new Map();

        /** @type {Map<string, string>} join_code -> server_id */
        this.joinCodeIndex = new Map();
    }

    /**
     * Register a new server/lobby.
     * @param {Object} data - Server registration data
     * @param {string} data.host_name - Display name for the server
     * @param {number} data.max_players - Maximum player capacity
     * @param {number} data.current_players - Current number of players
     * @param {string} data.game_id - Game version identifier
     * @param {string} data.host_socket_id - Unique socket ID for the host
     * @param {number} data.heartbeat_interval - Heartbeat interval in ms
     * @param {string} [data.map_name] - Current map name
     * @param {string} [data.host_ip] - Host's public IP
     * @param {boolean} [data.is_dedicated] - Whether this is a dedicated server
     * @returns {Object} The created server entry
     */
    create(data) {
        const id = uuidv4();
        const join_code = generateJoinCode();
        const now = Date.now();

        const entry = {
            id,
            join_code,
            host_name:          data.host_name,
            max_players:        data.max_players,
            current_players:    data.current_players || 0,
            game_id:            data.game_id,
            host_socket_id:     data.host_socket_id,
            heartbeat_interval: data.heartbeat_interval,
            map_name:           data.map_name || 'Unknown',
            host_ip:            data.host_ip || '',
            host_port:          data.host_port || 3074,
            is_dedicated:       data.is_dedicated || false,
            ping_ms:            0,
            created_at:         now,
            last_heartbeat:     now,
        };

        this.servers.set(id, entry);
        this.joinCodeIndex.set(join_code, id);

        // Start the prune timer
        this._startPruneTimer(id, entry.heartbeat_interval);

        console.log(`[Store] Server registered: ${entry.host_name} (${id}) code=${join_code}`);
        return entry;
    }

    /**
     * Get a server by ID.
     * @param {string} id
     * @returns {Object|undefined}
     */
    get(id) {
        return this.servers.get(id);
    }

    /**
     * Get a server by join code.
     * @param {string} code
     * @returns {Object|undefined}
     */
    getByJoinCode(code) {
        const id = this.joinCodeIndex.get(code.toUpperCase());
        if (!id) return undefined;
        return this.servers.get(id);
    }

    /**
     * List servers with cursor-based pagination.
     * @param {string|null} cursor - Server ID to start after (null = from beginning)
     * @param {number} limit - Maximum number of entries to return
     * @returns {{ servers: Object[], next_cursor: string|null }}
     */
    list(cursor = null, limit = 20) {
        const result = [];
        let foundCursor = cursor === null;

        for (const [id, entry] of this.servers) {
            if (!foundCursor) {
                if (id === cursor) {
                    foundCursor = true;
                }
                continue;
            }

            result.push(entry);
            if (result.length >= limit) {
                break;
            }
        }

        const next_cursor = result.length >= limit
            ? result[result.length - 1].id
            : null;

        return { servers: result, next_cursor };
    }

    /**
     * Update heartbeat timestamp for a server.
     * @param {string} id
     * @returns {boolean} true if server exists and was updated
     */
    heartbeat(id) {
        const entry = this.servers.get(id);
        if (!entry) return false;

        entry.last_heartbeat = Date.now();

        // Reset prune timer
        this._clearPruneTimer(id);
        this._startPruneTimer(id, entry.heartbeat_interval);

        return true;
    }

    /**
     * Update server data (e.g., current_players, map_name).
     * @param {string} id
     * @param {Object} updates - Fields to update
     * @returns {Object|undefined} Updated entry or undefined if not found
     */
    update(id, updates) {
        const entry = this.servers.get(id);
        if (!entry) return undefined;

        // Only allow updating specific fields
        const allowedFields = ['current_players', 'map_name', 'host_ip', 'host_port', 'ping_ms'];
        for (const field of allowedFields) {
            if (updates[field] !== undefined) {
                entry[field] = updates[field];
            }
        }

        return entry;
    }

    /**
     * Remove a server.
     * @param {string} id
     * @returns {boolean}
     */
    remove(id) {
        const entry = this.servers.get(id);
        if (!entry) return false;

        this.joinCodeIndex.delete(entry.join_code);
        this._clearPruneTimer(id);
        this.servers.delete(id);

        console.log(`[Store] Server removed: ${entry.host_name} (${id})`);
        return true;
    }

    /**
     * Get total number of registered servers.
     * @returns {number}
     */
    count() {
        return this.servers.size;
    }

    // ── Prune Timer Management ────────────────────────────────────────

    _startPruneTimer(id, heartbeatInterval) {
        // Prune aggressively: if heartbeat is missed (1.5x interval tolerance)
        const timeout = Math.max(heartbeatInterval * 1.5, 10000); // minimum 10s

        const timer = setTimeout(() => {
            const entry = this.servers.get(id);
            if (entry) {
                console.log(`[Store] PRUNE: Server "${entry.host_name}" (${id}) missed heartbeat`);
                this.remove(id);
            }
        }, timeout);

        // Don't prevent Node.js from exiting
        timer.unref();

        this.pruneTimers.set(id, timer);
    }

    _clearPruneTimer(id) {
        const timer = this.pruneTimers.get(id);
        if (timer) {
            clearTimeout(timer);
            this.pruneTimers.delete(id);
        }
    }

    /**
     * Clear all servers and timers (for testing).
     */
    clear() {
        for (const [id] of this.pruneTimers) {
            this._clearPruneTimer(id);
        }
        this.servers.clear();
        this.joinCodeIndex.clear();
    }
}

module.exports = new ServerStore();
