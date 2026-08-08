// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Master Server: Integration Tests
// ═══════════════════════════════════════════════════════════════════════
//  Uses Node.js built-in test runner (--test flag, Node 18+).
//  Tests: CRUD operations, cursor pagination, join codes, heartbeat,
//         strict validation, and pruning.
// ═══════════════════════════════════════════════════════════════════════

const { describe, it, before, after, beforeEach } = require('node:test');
const assert = require('node:assert/strict');
const http = require('http');

const app = require('../index');
const store = require('../store');

let server;
let baseUrl;

// ── Helper: HTTP Request ──────────────────────────────────────────────
function request(method, path, body = null) {
    return new Promise((resolve, reject) => {
        const url = new URL(path, baseUrl);
        const options = {
            method,
            hostname: url.hostname,
            port: url.port,
            path: url.pathname + url.search,
            headers: { 'Content-Type': 'application/json' },
        };

        const req = http.request(options, (res) => {
            let data = '';
            res.on('data', chunk => data += chunk);
            res.on('end', () => {
                let parsed = null;
                try { parsed = JSON.parse(data); } catch { parsed = data; }
                resolve({ status: res.statusCode, body: parsed, raw: data });
            });
        });

        req.on('error', reject);
        if (body) req.write(JSON.stringify(body));
        req.end();
    });
}

// ── Valid server payload ──────────────────────────────────────────────
function validPayload(overrides = {}) {
    return {
        host_socket_id: 'test_socket_001',
        max_players: 4,
        heartbeat_interval: 30000,
        game_id: 'ACU_v1.5.0',
        host_name: 'Test Server',
        current_players: 1,
        map_name: 'Paris_FreeRoam',
        ...overrides,
    };
}

// ── Setup / Teardown ──────────────────────────────────────────────────
before((_, done) => {
    // Start test server on random port
    server = app.listen(0, '127.0.0.1', () => {
        const addr = server.address();
        baseUrl = `http://127.0.0.1:${addr.port}`;
        console.log(`Test server on ${baseUrl}`);
        done();
    });
});

after((_, done) => {
    server.close(done);
});

beforeEach(() => {
    store.clear();
});

// ═══════════════════════════════════════════════════════════════════════
//  Test Suites
// ═══════════════════════════════════════════════════════════════════════

describe('Health Check', () => {
    it('GET /health returns healthy status', async () => {
        const res = await request('GET', '/health');
        assert.equal(res.status, 200);
        assert.equal(res.body.status, 'healthy');
        assert.equal(typeof res.body.uptime_seconds, 'number');
        assert.equal(typeof res.body.server_count, 'number');
    });
});

describe('POST /servers', () => {
    it('creates a server with valid payload', async () => {
        const res = await request('POST', '/servers', validPayload());
        assert.equal(res.status, 201);
        assert.ok(res.body.id);
        assert.ok(res.body.join_code);
        assert.equal(res.body.join_code.length, 6);
        assert.equal(res.body.host_name, 'Test Server');
        assert.equal(res.body.max_players, 4);
    });

    it('rejects missing required fields', async () => {
        const res = await request('POST', '/servers', { host_name: 'Incomplete' });
        assert.equal(res.status, 400);
        assert.equal(res.body.error, 'missing_field');
    });

    it('rejects unknown fields (conservative API)', async () => {
        const payload = { ...validPayload(), unknown_field: 'sneaky' };
        const res = await request('POST', '/servers', payload);
        assert.equal(res.status, 400);
        assert.equal(res.body.error, 'unknown_field');
    });

    it('rejects camelCase field names', async () => {
        const res = await request('POST', '/servers', {
            hostSocketId: 'test',
            max_players: 4,
            heartbeat_interval: 30000,
            game_id: 'ACU',
        });
        assert.equal(res.status, 400);
        assert.equal(res.body.error, 'unknown_field');
    });

    it('rejects invalid max_players range', async () => {
        const res = await request('POST', '/servers', validPayload({ max_players: 999 }));
        assert.equal(res.status, 400);
        assert.equal(res.body.error, 'invalid_value');
    });

    it('rejects invalid heartbeat_interval', async () => {
        const res = await request('POST', '/servers', validPayload({ heartbeat_interval: 100 }));
        assert.equal(res.status, 400);
        assert.equal(res.body.error, 'invalid_value');
    });
});

describe('GET /servers', () => {
    it('returns empty list when no servers registered', async () => {
        const res = await request('GET', '/servers');
        assert.equal(res.status, 200);
        assert.deepEqual(res.body.servers, []);
        assert.equal(res.body.total_count, 0);
        assert.equal(res.body.next_cursor, null);
    });

    it('returns registered servers', async () => {
        await request('POST', '/servers', validPayload({ host_name: 'Server A' }));
        await request('POST', '/servers', validPayload({ host_name: 'Server B' }));

        const res = await request('GET', '/servers');
        assert.equal(res.status, 200);
        assert.equal(res.body.servers.length, 2);
        assert.equal(res.body.total_count, 2);
    });

    it('supports cursor-based pagination', async () => {
        // Create 5 servers
        for (let i = 0; i < 5; i++) {
            await request('POST', '/servers', validPayload({ host_name: `Server ${i}` }));
        }

        // Page 1: limit=2
        const page1 = await request('GET', '/servers?limit=2');
        assert.equal(page1.body.servers.length, 2);
        assert.ok(page1.body.next_cursor);
        assert.equal(page1.body.total_count, 5);

        // Page 2: use cursor from page 1
        const page2 = await request('GET', `/servers?cursor=${page1.body.next_cursor}&limit=2`);
        assert.equal(page2.body.servers.length, 2);
        assert.ok(page2.body.next_cursor);

        // Page 3: last page
        const page3 = await request('GET', `/servers?cursor=${page2.body.next_cursor}&limit=2`);
        assert.equal(page3.body.servers.length, 1);
        assert.equal(page3.body.next_cursor, null);

        // Verify no duplicates across pages
        const allIds = [
            ...page1.body.servers.map(s => s.id),
            ...page2.body.servers.map(s => s.id),
            ...page3.body.servers.map(s => s.id),
        ];
        assert.equal(new Set(allIds).size, 5);
    });
});

describe('GET /servers/:id', () => {
    it('returns server by ID', async () => {
        const created = await request('POST', '/servers', validPayload());
        const res = await request('GET', `/servers/${created.body.id}`);
        assert.equal(res.status, 200);
        assert.equal(res.body.id, created.body.id);
    });

    it('returns 404 for non-existent ID', async () => {
        const res = await request('GET', '/servers/non-existent-id');
        assert.equal(res.status, 404);
    });
});

describe('GET /servers/join/:code', () => {
    it('finds server by join code', async () => {
        const created = await request('POST', '/servers', validPayload());
        const code = created.body.join_code;

        const res = await request('GET', `/servers/join/${code}`);
        assert.equal(res.status, 200);
        assert.equal(res.body.join_code, code);
        assert.equal(res.body.id, created.body.id);
    });

    it('is case-insensitive', async () => {
        const created = await request('POST', '/servers', validPayload());
        const code = created.body.join_code.toLowerCase();

        const res = await request('GET', `/servers/join/${code}`);
        assert.equal(res.status, 200);
    });

    it('returns 404 for invalid code', async () => {
        const res = await request('GET', '/servers/join/ZZZZZZ');
        assert.equal(res.status, 404);
    });
});

describe('PUT /servers/:id/heartbeat', () => {
    it('acknowledges heartbeat for existing server', async () => {
        const created = await request('POST', '/servers', validPayload());
        const res = await request('PUT', `/servers/${created.body.id}/heartbeat`);
        assert.equal(res.status, 200);
        assert.equal(res.body.status, 'ok');
    });

    it('returns 404 for non-existent server', async () => {
        const res = await request('PUT', '/servers/fake-id/heartbeat');
        assert.equal(res.status, 404);
    });
});

describe('PUT /servers/:id', () => {
    it('updates allowed fields', async () => {
        const created = await request('POST', '/servers', validPayload());
        const res = await request('PUT', `/servers/${created.body.id}`, {
            current_players: 3,
            map_name: 'Paris_Catacombs',
        });
        assert.equal(res.status, 200);
        assert.equal(res.body.current_players, 3);
        assert.equal(res.body.map_name, 'Paris_Catacombs');
    });

    it('rejects unknown fields', async () => {
        const created = await request('POST', '/servers', validPayload());
        const res = await request('PUT', `/servers/${created.body.id}`, {
            hack_field: 'injection',
        });
        assert.equal(res.status, 400);
        assert.equal(res.body.error, 'unknown_field');
    });
});

describe('DELETE /servers/:id', () => {
    it('removes an existing server', async () => {
        const created = await request('POST', '/servers', validPayload());
        const res = await request('DELETE', `/servers/${created.body.id}`);
        assert.equal(res.status, 204);

        // Verify it's gone
        const check = await request('GET', `/servers/${created.body.id}`);
        assert.equal(check.status, 404);
    });

    it('join code is invalidated after deletion', async () => {
        const created = await request('POST', '/servers', validPayload());
        const code = created.body.join_code;
        await request('DELETE', `/servers/${created.body.id}`);

        const res = await request('GET', `/servers/join/${code}`);
        assert.equal(res.status, 404);
    });

    it('returns 404 for non-existent server', async () => {
        const res = await request('DELETE', '/servers/non-existent');
        assert.equal(res.status, 404);
    });
});

describe('Heartbeat Pruning', () => {
    it('prunes server after missed heartbeat (short interval)', async () => {
        // Create server with very short heartbeat interval (5s)
        const created = await request('POST', '/servers', validPayload({
            heartbeat_interval: 5000,
        }));

        // Server should exist
        let check = await request('GET', `/servers/${created.body.id}`);
        assert.equal(check.status, 200);

        // Wait for prune (5s * 1.5 = 7.5s, but minimum is 10s)
        await new Promise(resolve => setTimeout(resolve, 11000));

        // Server should be pruned
        check = await request('GET', `/servers/${created.body.id}`);
        assert.equal(check.status, 404);
    });
});
