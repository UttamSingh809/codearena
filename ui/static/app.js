'use strict';

// ============================================================
// CodeArena UI — shared helpers
// ============================================================

const API_BASE = window.location.origin.includes(':3000') ?
    'http://localhost:8000' :
    '';

const TOKEN_KEY = 'codearena_token';
const USER_KEY = 'codearena_user';

// ------------------------------------------------------------------
// Toast notifications
// ------------------------------------------------------------------

function toast(message, type = 'info', durationMs = 4000) {
    let container = document.getElementById('toast-container');
    if (!container) {
        container = document.createElement('div');
        container.id = 'toast-container';
        document.body.appendChild(container);
    }
    const t = document.createElement('div');
    t.className = `toast ${type}`;
    t.textContent = message;
    container.appendChild(t);

    setTimeout(() => {
        t.style.transition = 'opacity 0.2s, transform 0.2s';
        t.style.opacity = '0';
        t.style.transform = 'translateX(120%)';
        setTimeout(() => t.remove(), 200);
    }, durationMs);
}

// ------------------------------------------------------------------
// Auth storage
// ------------------------------------------------------------------

const Auth = {
    saveToken(token, user) {
        localStorage.setItem(TOKEN_KEY, token);
        if (user) localStorage.setItem(USER_KEY, JSON.stringify(user));
    },
    getToken() { return localStorage.getItem(TOKEN_KEY); },
    getUser() {
        const raw = localStorage.getItem(USER_KEY);
        try { return raw ? JSON.parse(raw) : null; } catch { return null; }
    },
    clear() {
        localStorage.removeItem(TOKEN_KEY);
        localStorage.removeItem(USER_KEY);
    },
    isLoggedIn() { return !!this.getToken(); },
    requireLogin() {
        if (!this.isLoggedIn()) {
            location.href = 'index.html';
            throw new Error('not logged in');
        }
    },
    logout() {
        this.clear();
        location.href = 'index.html';
    },
};

// ------------------------------------------------------------------
// API fetch
// ------------------------------------------------------------------

class ApiError extends Error {
    constructor(status, detail) {
        super(typeof detail === 'string' ? detail : JSON.stringify(detail));
        this.status = status;
        this.detail = detail;
    }
}

async function apiFetch(path, options = {}) {
    const headers = Object.assign({ 'Content-Type': 'application/json' },
        options.headers || {},
    );

    const token = Auth.getToken();
    if (token) headers['Authorization'] = `Bearer ${token}`;

    let resp;
    try {
        resp = await fetch(API_BASE + path, Object.assign({}, options, { headers }));
    } catch (e) {
        throw new ApiError(0, `Network error: ${e.message}`);
    }

    let body = null;
    const ct = resp.headers.get('content-type') || '';
    if (ct.includes('application/json')) {
        try { body = await resp.json(); } catch { /* ignore */ }
    }

    if (!resp.ok) {
        const detail = body && body.detail ? body.detail : resp.statusText;
        throw new ApiError(resp.status, detail);
    }

    return body;
}

const Api = {
    register: (u, e, p) => apiFetch('/api/auth/register', { method: 'POST', body: JSON.stringify({ username: u, email: e, password: p }) }),
    login: (e, p) => apiFetch('/api/auth/login', { method: 'POST', body: JSON.stringify({ email: e, password: p }) }),
    me: () => apiFetch('/api/auth/me'),
    listProblems: () => apiFetch('/api/problems'),
    getProblem: (id) => apiFetch(`/api/problems/${id}`),
    submit: (pid, l, code) => apiFetch('/api/submissions', {
        method: 'POST',
        body: JSON.stringify({ problem_id: pid, language: l, source_code: code }),
    }),
    getSubmission: (id) => apiFetch(`/api/submissions/${id}`),
    listSubmissions: (limit = 50, offset = 0) =>
        apiFetch(`/api/submissions?limit=${limit}&offset=${offset}`),
    leaderboard: (limit = 50, offset = 0) =>
        apiFetch(`/api/leaderboard?limit=${limit}&offset=${offset}`),
    getStreak: () => apiFetch('/api/stats/streak'),
};

// ------------------------------------------------------------------
// DOM helpers
// ------------------------------------------------------------------

function el(tag, attrs = {}, children = []) {
    const node = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
        if (v == null || v === false) continue;
        if (k === 'class') node.className = v;
        else if (k === 'text') node.textContent = v;
        else if (k === 'html') node.innerHTML = v;
        else if (k === 'style' && typeof v === 'object') Object.assign(node.style, v);
        else if (k.startsWith('on') && typeof v === 'function') {
            node.addEventListener(k.slice(2).toLowerCase(), v);
        } else node.setAttribute(k, v);
    }
    for (const child of[].concat(children)) {
        if (child == null || child === false) continue;
        if (typeof child === 'string') node.appendChild(document.createTextNode(child));
        else node.appendChild(child);
    }
    return node;
}

function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }

// ------------------------------------------------------------------
// Formatting
// ------------------------------------------------------------------

function formatDate(iso) {
    if (!iso) return '—';
    const d = new Date(iso);
    const now = new Date();
    const diffMs = now - d;
    const diffSec = Math.floor(diffMs / 1000);

    if (diffSec < 60) return 'just now';
    if (diffSec < 3600) return `${Math.floor(diffSec / 60)}m ago`;
    if (diffSec < 86400) return `${Math.floor(diffSec / 3600)}h ago`;
    if (diffSec < 86400 * 7) return `${Math.floor(diffSec / 86400)}d ago`;

    return d.toLocaleDateString(undefined, {
        year: 'numeric',
        month: 'short',
        day: 'numeric',
    });
}

function formatNumber(n) {
    if (n == null) return '—';
    return Number(n).toLocaleString();
}

function verdictBadge(verdict) {
    if (!verdict) return el('span', { class: 'badge PENDING', text: 'Pending' });
    return el('span', { class: `badge ${verdict}`, text: verdict });
}

function difficultyBadge(d) {
    return el('span', { class: `difficulty ${d}`, text: d });
}

// ------------------------------------------------------------------
// Header
// ------------------------------------------------------------------

function renderHeader(activePage) {
    const user = Auth.getUser();
    const header = el('header', { class: 'app-header' });

    const brand = el('a', { href: 'dashboard.html', class: 'brand', text: 'CodeArena' });
    header.appendChild(brand);

    const nav = el('nav');
    const pages = [
        { href: 'dashboard.html', label: 'Dashboard', key: 'dashboard' },
        { href: 'problems.html', label: 'Problems', key: 'problems' },
        { href: 'history.html', label: 'Submissions', key: 'history' },
        { href: 'leaderboard.html', label: 'Leaderboard', key: 'leaderboard' },
    ];
    for (const p of pages) {
        const a = el('a', { href: p.href, text: p.label });
        if (activePage === p.key) a.classList.add('active');
        nav.appendChild(a);
    }
    header.appendChild(nav);

    if (user) {
        const menu = el('div', { class: 'user-menu' });

        // NEW: Streak Badge
        const streakContainer = el('div', {
            id: 'header-streak',
            class: 'streak-badge hidden',
            title: 'Daily streak'
        });
        menu.appendChild(streakContainer);

        // Fetch streak asynchronously and update
        Api.getStreak().then(data => {
            if (data && data.current_streak > 0) {
                streakContainer.textContent = `🔥 ${data.current_streak}`;
                streakContainer.classList.remove('hidden');
            }
        }).catch(() => { /* ignore errors, streak is optional */ });

        const initial = (user.username || '?').charAt(0).toUpperCase();
        menu.appendChild(el('div', { class: 'avatar', text: initial }));
        menu.appendChild(el('span', { text: user.username }));

        const logout = el('button', {
            class: 'btn-logout',
            text: 'Logout',
            onclick: () => Auth.logout(),
        });
        menu.appendChild(logout);
        header.appendChild(menu);
    }

    return header;
}

function requireAuth(activePage) {
    if (!Auth.isLoggedIn()) {
        location.href = 'index.html';
        return false;
    }
    document.body.insertBefore(renderHeader(activePage), document.body.firstChild);
    return true;
}

// ------------------------------------------------------------------
// Polling
// ------------------------------------------------------------------

function poll(fn, intervalMs = 2000) {
    let stopped = false;
    let timer = null;
    async function tick() {
        if (stopped) return;
        let cont = false;
        try { cont = await fn(); } catch (e) { console.error('poll error', e); }
        if (!stopped && cont !== true) timer = setTimeout(tick, intervalMs);
        else stopped = true;
    }
    tick();
    return () => { stopped = true; if (timer) clearTimeout(timer); };
}

// ------------------------------------------------------------------
// Skeleton loaders
// ------------------------------------------------------------------
//
// Each function returns a DOM subtree that mimics the shape of the
// content that's about to load. Callers append it to their container.

const Skeleton = {

    // ---- Base shapes ----

    line(width = 'long') {
        return el('div', { class: `skeleton skeleton-line ${width}` });
    },

    title() {
        return el('div', { class: 'skeleton skeleton-title' });
    },

    subtitle() {
        return el('div', { class: 'skeleton skeleton-subtitle' });
    },

    circle(size = '') {
        return el('div', { class: `skeleton skeleton-circle ${size}` });
    },

    badge() {
        return el('div', { class: 'skeleton skeleton-badge' });
    },

    cell(width = '') {
        return el('div', { class: `skeleton skeleton-cell ${width}` });
    },

    // ---- Composite layouts ----

    /**
     * A row of stat cards, matching #stats-grid on the dashboard.
     * @param {number} count - number of cards to show
     */
    statGrid(count = 4) {
        const grid = el('div', { class: 'card-grid' });
        for (let i = 0; i < count; i++) {
            grid.appendChild(el('div', { class: 'skeleton-stat' }, [
                el('div', { class: 'skeleton skeleton-line short' }),
                el('div', { class: 'skeleton skeleton-title' }),
                el('div', { class: 'skeleton skeleton-line medium' }),
            ]));
        }
        return grid;
    },

    /**
     * A table skeleton with a header row and `rows` body rows.
     * @param {number} cols - number of columns
     * @param {number} rows - number of data rows
     */
    table(cols = 5, rows = 6) {
        const wrap = el('div', { class: 'skeleton-table' });
        wrap.style.setProperty('--cols', cols);

        // Header row
        const header = el('div', { class: 'skeleton-table-row header' });
        for (let i = 0; i < cols; i++) {
            header.appendChild(el('div', { class: 'skeleton skeleton-cell sm' }));
        }
        wrap.appendChild(header);

        // Body rows
        for (let r = 0; r < rows; r++) {
            const row = el('div', { class: 'skeleton-table-row' });
            for (let c = 0; c < cols; c++) {
                // Vary widths so it looks organic
                const widthClass = ['sm', 'lg', 'md', 'md', 'sm'][c % 5];
                row.appendChild(el('div', {
                    class: `skeleton skeleton-cell ${widthClass}`,
                }));
            }
            wrap.appendChild(row);
        }
        return wrap;
    },

    /**
     * Skeleton for the problem page: header, metadata, statement
     * sections, and the editor panel.
     */
    problemPage() {
        const wrap = el('div');

        // Header
        const header = el('div', { class: 'skeleton-problem-header' }, [
            el('div', { class: 'skeleton skeleton-title', style: { width: '45%', height: '28px' } }),
            el('div', { class: 'skeleton-problem-meta' }, [
                el('div', { class: 'skeleton skeleton-badge' }),
                el('div', { class: 'skeleton skeleton-badge', style: { width: '80px' } }),
                el('div', { class: 'skeleton skeleton-badge', style: { width: '80px' } }),
            ]),
        ]);
        wrap.appendChild(header);

        // Two-column split: statement + editor skeleton
        const split = el('div', { class: 'split' });

        // Left: statement sections
        const left = el('div', { class: 'pane' });
        for (let i = 0; i < 3; i++) {
            const section = el('div', { class: 'skeleton-problem-section' }, [
                el('div', { class: 'skeleton skeleton-line short', style: { height: '10px' } }),
                el('div', { class: 'skeleton skeleton-line long' }),
                el('div', { class: 'skeleton skeleton-line long' }),
                el('div', { class: 'skeleton skeleton-line medium' }),
            ]);
            left.appendChild(section);
        }
        split.appendChild(left);

        // Right: editor skeleton
        const right = el('div', { class: 'pane' });
        const editor = el('div', { class: 'skeleton-editor' }, [
            el('div', { style: { display: 'flex', gap: '10px', alignItems: 'center' } }, [
                el('div', { class: 'skeleton skeleton-badge', style: { width: '80px' } }),
                el('div', { class: 'skeleton skeleton-badge', style: { width: '60px' } }),
                el('div', { class: 'skeleton skeleton-badge', style: { width: '60px', marginLeft: 'auto' } }),
            ]),
            el('div', { class: 'skeleton-editor-body' }),
        ]);
        right.appendChild(editor);
        split.appendChild(right);

        wrap.appendChild(split);
        return wrap;
    },

    /**
     * Skeleton for the submission page: verdict hero + details + code.
     */
    submissionPage() {
        const wrap = el('div');

        // Hero
        wrap.appendChild(el('div', { class: 'skeleton-verdict-hero' }, [
            el('div', { class: 'left' }, [
                el('div', { class: 'skeleton skeleton-line short', style: { height: '10px' } }),
                el('div', { class: 'skeleton skeleton-title', style: { height: '32px', width: '70%' } }),
            ]),
            el('div', { class: 'metrics' }, [
                el('div', {}, [
                    el('div', { class: 'skeleton skeleton-line short', style: { height: '10px', width: '50px' } }),
                    el('div', { class: 'skeleton skeleton-subtitle' }),
                ]),
                el('div', {}, [
                    el('div', { class: 'skeleton skeleton-line short', style: { height: '10px', width: '50px' } }),
                    el('div', { class: 'skeleton skeleton-subtitle' }),
                ]),
                el('div', {}, [
                    el('div', { class: 'skeleton skeleton-line short', style: { height: '10px', width: '50px' } }),
                    el('div', { class: 'skeleton skeleton-subtitle' }),
                ]),
            ]),
        ]));

        // Details card
        const details = el('div', { class: 'card mb-24', style: { padding: '20px' } });
        for (let i = 0; i < 4; i++) {
            details.appendChild(el('div', {
                style: { display: 'flex', gap: '24px', marginBottom: '12px' },
            }, [
                el('div', { class: 'skeleton skeleton-line short', style: { height: '12px', width: '120px' } }),
                el('div', { class: 'skeleton skeleton-line long', style: { height: '12px', flex: 1 } }),
            ]));
        }
        wrap.appendChild(details);

        // Source code card
        const source = el('div', { class: 'card', style: { padding: '20px' } }, [
            el('div', { class: 'skeleton skeleton-line short', style: { height: '14px', width: '100px', marginBottom: '16px' } }),
            el('div', { class: 'skeleton-editor-body', style: { height: '220px' } }),
        ]);
        wrap.appendChild(source);

        return wrap;
    },

    /**
     * Skeleton for the leaderboard: header + ranked table.
     */
    leaderboard() {
        const wrap = el('div');
        // Header
        wrap.appendChild(el('div', { style: { marginBottom: '24px' } }, [
            el('div', { class: 'skeleton skeleton-title', style: { width: '200px' } }),
            el('div', { class: 'skeleton skeleton-line medium', style: { height: '12px', marginTop: '8px' } }),
        ]));
        // Table
        wrap.appendChild(this.table(5, 10));
        return wrap;
    },
};


// ------------------------------------------------------------------
// Loading state helpers (updated to use skeletons)
// ------------------------------------------------------------------

/**
 * Legacy helper — kept for backwards compatibility. If the caller
 * specifies a skeleton type, renders it; otherwise falls back to a
 * simple spinner.
 *
 * @param {HTMLElement} container
 * @param {string} [type] - 'table' | 'stat-grid' | 'problem' |
 *                          'submission' | 'leaderboard' | undefined
 * @param {object} [opts] - type-specific options
 */
function loadingState(container, type, opts = {}) {
    clear(container);

    if (type === 'table') {
        container.appendChild(Skeleton.table(opts.cols || 5, opts.rows || 6));
        return;
    }
    if (type === 'stat-grid') {
        container.appendChild(Skeleton.statGrid(opts.count || 4));
        return;
    }
    if (type === 'problem') {
        container.appendChild(Skeleton.problemPage());
        return;
    }
    if (type === 'submission') {
        container.appendChild(Skeleton.submissionPage());
        return;
    }
    if (type === 'leaderboard') {
        container.appendChild(Skeleton.leaderboard());
        return;
    }

    // Fallback: generic spinner block
    container.appendChild(el('div', { class: 'loading-block' }, [
        el('span', { class: 'spinner' }),
        el('span', { text: 'Loading…' }),
    ]));
}

function emptyState(container, title, subtitle) {
    clear(container);
    container.appendChild(el('div', { class: 'empty' }, [
        el('h3', { text: title }),
        subtitle && el('p', { text: subtitle }),
    ]));
}
// ------------------------------------------------------------------
// Verdict display helpers
// ------------------------------------------------------------------

const VERDICT_INFO = {
    AC: { label: 'Accepted', color: 'var(--success)' },
    WA: { label: 'Wrong Answer', color: 'var(--danger)' },
    TLE: { label: 'Time Limit Exceeded', color: 'var(--warning)' },
    MLE: { label: 'Memory Limit Exceeded', color: 'var(--warning)' },
    OLE: { label: 'Output Limit Exceeded', color: 'var(--warning)' },
    RE: { label: 'Runtime Error', color: 'var(--danger)' },
    CE: { label: 'Compilation Error', color: 'var(--danger)' },
    PE: { label: 'Presentation Error', color: 'var(--warning)' },
    FAILED: { label: 'Grading Failed', color: 'var(--danger)' },
    PENDING: { label: 'Pending', color: 'var(--fg-muted)' },
    PROCESSING: { label: 'Running', color: 'var(--accent)' },
};

function verdictLabel(v) {
    return (VERDICT_INFO[v] || { label: v }).label;
}

// ------------------------------------------------------------------
// Celebrations
// ------------------------------------------------------------------

function celebrateAC() {
    if (typeof confetti === 'undefined') return; // CDN blocked or not loaded

    // Fire three bursts with slight delays for a more dramatic effect
    const duration = 1500;
    const animationEnd = Date.now() + duration;
    const defaults = { startVelocity: 30, spread: 360, ticks: 60, zIndex: 9999 };

    function randomInRange(min, max) {
        return Math.random() * (max - min) + min;
    }

    const interval = setInterval(function() {
        const timeLeft = animationEnd - Date.now();

        if (timeLeft <= 0) {
            return clearInterval(interval);
        }

        const particleCount = 50 * (timeLeft / duration);

        // Fire from two sides
        confetti(Object.assign({}, defaults, {
            particleCount,
            origin: { x: randomInRange(0.1, 0.3), y: Math.random() - 0.2 }
        }));
        confetti(Object.assign({}, defaults, {
            particleCount,
            origin: { x: randomInRange(0.7, 0.9), y: Math.random() - 0.2 }
        }));
    }, 250);
}