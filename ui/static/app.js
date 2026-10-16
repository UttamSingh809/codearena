/* ============================================================
 * CodeArena UI — shared JavaScript helpers
 *
 * Handles JWT storage, fetch wrappers, and small rendering
 * utilities. Every page includes this file and calls the
 * appropriate helpers on load.
 * ============================================================ */

'use strict';

// ------------------------------------------------------------------
// Configuration
// ------------------------------------------------------------------

// The API base. If the UI is served from the same origin as the API,
// leave this empty. Otherwise, set it to the API's full URL.
const API_BASE = window.location.origin.includes(':3000')
    ? 'http://localhost:8000'
    : '';

const TOKEN_KEY = 'codearena_token';
const USER_KEY = 'codearena_user';


// ------------------------------------------------------------------
// Token / user storage
// ------------------------------------------------------------------

const Auth = {
    saveToken(token, user) {
        localStorage.setItem(TOKEN_KEY, token);
        if (user) localStorage.setItem(USER_KEY, JSON.stringify(user));
    },

    getToken() {
        return localStorage.getItem(TOKEN_KEY);
    },

    getUser() {
        const raw = localStorage.getItem(USER_KEY);
        try { return raw ? JSON.parse(raw) : null; }
        catch { return null; }
    },

    clear() {
        localStorage.removeItem(TOKEN_KEY);
        localStorage.removeItem(USER_KEY);
    },

    isLoggedIn() {
        return !!this.getToken();
    },
};


// ------------------------------------------------------------------
// Fetch wrappers
// ------------------------------------------------------------------

class ApiError extends Error {
    constructor(status, detail) {
        super(typeof detail === 'string' ? detail : JSON.stringify(detail));
        this.status = status;
        this.detail = detail;
    }
}

async function apiFetch(path, options = {}) {
    const headers = Object.assign(
        { 'Content-Type': 'application/json' },
        options.headers || {},
    );

    const token = Auth.getToken();
    if (token) headers['Authorization'] = `Bearer ${token}`;

    const url = API_BASE + path;

    let resp;
    try {
        resp = await fetch(url, Object.assign({}, options, { headers }));
    } catch (e) {
        throw new ApiError(0, `Network error: ${e.message}`);
    }

    // Parse JSON if there's a body. Some endpoints return no content.
    let body = null;
    const ct = resp.headers.get('content-type') || '';
    if (ct.includes('application/json')) {
        try { body = await resp.json(); } catch { /* ignore */ }
    }

    if (!resp.ok) {
        // FastAPI error format: { detail: ... }
        const detail = body && body.detail ? body.detail : resp.statusText;
        throw new ApiError(resp.status, detail);
    }

    return body;
}


// ------------------------------------------------------------------
// Higher-level API calls
// ------------------------------------------------------------------

const Api = {
    async register(username, email, password) {
        return apiFetch('/api/auth/register', {
            method: 'POST',
            body: JSON.stringify({ username, email, password }),
        });
    },

    async login(email, password) {
        return apiFetch('/api/auth/login', {
            method: 'POST',
            body: JSON.stringify({ email, password }),
        });
    },

    async me() {
        return apiFetch('/api/auth/me');
    },

    async listProblems() {
        return apiFetch('/api/problems');
    },

    async getProblem(id) {
        return apiFetch(`/api/problems/${id}`);
    },

    async submit(problemId, language, sourceCode) {
        return apiFetch('/api/submissions', {
            method: 'POST',
            body: JSON.stringify({
                problem_id: problemId,
                language,
                source_code: sourceCode,
            }),
        });
    },

    async getSubmission(id) {
        return apiFetch(`/api/submissions/${id}`);
    },

    async listSubmissions(limit = 20, offset = 0) {
        return apiFetch(`/api/submissions?limit=${limit}&offset=${offset}`);
    },

    async leaderboard(limit = 50, offset = 0) {
        return apiFetch(`/api/leaderboard?limit=${limit}&offset=${offset}`);
    },
};


// ------------------------------------------------------------------
// Rendering helpers
// ------------------------------------------------------------------

function el(tag, attrs = {}, children = []) {
    const node = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
        if (k === 'class') node.className = v;
        else if (k === 'text') node.textContent = v;
        else if (k.startsWith('on')) node.addEventListener(k.slice(2), v);
        else node.setAttribute(k, v);
    }
    for (const child of [].concat(children)) {
        if (typeof child === 'string') node.appendChild(document.createTextNode(child));
        else if (child) node.appendChild(child);
    }
    return node;
}

function escapeHtml(s) {
    const div = document.createElement('div');
    div.textContent = s == null ? '' : String(s);
    return div.innerHTML;
}

function verdictBadge(v) {
    if (!v) return el('span', { class: 'verdict PENDING', text: 'PENDING' });
    return el('span', { class: `verdict ${v}`, text: v });
}

function showError(container, message) {
    container.innerHTML = '';
    container.appendChild(el('div', { class: 'error', text: message }));
}

function clearError(container) {
    container.innerHTML = '';
}

function formatDate(iso) {
    if (!iso) return '—';
    const d = new Date(iso);
    return d.toLocaleString();
}


// ------------------------------------------------------------------
// Header rendering
// ------------------------------------------------------------------

function renderHeader(activePage) {
    const user = Auth.getUser();
    const header = document.createElement('header');

    const title = el('h1', { text: 'CodeArena' });

    const nav = el('nav');
    const pages = [
        { href: 'problems.html', label: 'Problems', key: 'problems' },
        { href: 'history.html', label: 'My Submissions', key: 'history' },
        { href: 'leaderboard.html', label: 'Leaderboard', key: 'leaderboard' },
    ];
    for (const p of pages) {
        const a = el('a', { href: p.href, text: p.label });
        if (activePage === p.key) a.classList.add('active');
        nav.appendChild(a);
    }

    header.appendChild(title);
    header.appendChild(nav);

    if (user) {
        const info = el('div', { class: 'user-info' });
        info.appendChild(el('span', { text: user.username }));
        const btn = el('button', {
            text: 'Logout',
            onclick: () => { Auth.clear(); location.href = 'index.html'; },
        });
        info.appendChild(btn);
        header.appendChild(info);
    }

    return header;
}


// ------------------------------------------------------------------
// Page setup
// ------------------------------------------------------------------

/**
 * Call this at the top of every authenticated page. It:
 *   - Redirects to login if not authenticated
 *   - Renders the header
 *   - Returns the user object
 */
function requireAuth(activePage) {
    if (!Auth.isLoggedIn()) {
        location.href = 'index.html';
        return null;
    }
    document.body.insertBefore(renderHeader(activePage), document.body.firstChild);
    return Auth.getUser();
}


// ------------------------------------------------------------------
// Status polling helper
// ------------------------------------------------------------------

/**
 * Poll a URL every `intervalMs` until the callback returns true.
 * Returns a stop function.
 */
function poll(url, callback, intervalMs = 2000) {
    let stopped = false;
    let timer = null;

    async function tick() {
        if (stopped) return;
        try {
            const data = await apiFetch(url);
            if (callback(data) === true) {
                stopped = true;
                return;
            }
        } catch (e) {
            console.error('poll error:', e);
        }
        if (!stopped) timer = setTimeout(tick, intervalMs);
    }

    tick();
    return () => { stopped = true; if (timer) clearTimeout(timer); };
}