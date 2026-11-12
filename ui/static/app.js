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
        container.setAttribute('aria-live', 'polite');
        container.setAttribute('aria-atomic', 'false');
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
// Theme management
// ------------------------------------------------------------------

const THEME_KEY = 'codearena_theme';
const THEME_LIGHT = 'light';
const THEME_DARK = 'dark';

/**
 * Return the currently applied theme ('light' or 'dark').
 */
function currentTheme() {
    return document.documentElement.dataset.theme || THEME_LIGHT;
}

/**
 * Apply a theme and persist it.
 */
function setTheme(theme) {
    if (theme !== THEME_LIGHT && theme !== THEME_DARK) {
        theme = THEME_LIGHT;
    }
    document.documentElement.dataset.theme = theme;
    try {
        localStorage.setItem(THEME_KEY, theme);
    } catch (err) {
        // localStorage can fail in private browsing — fall back to memory
        console.warn('Could not persist theme:', err);
    }
    // Update meta theme-color if present (used by mobile browsers)
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) {
        meta.setAttribute('content', theme === THEME_DARK ? '#1a1114' : '#FBEDE8');
    }
}

/**
 * Apply the saved theme immediately at load time.
 * Called automatically when this script runs.
 *
 * Priority:
 *   1. Explicit user choice (localStorage)
 *   2. OS preference (prefers-color-scheme)
 *   3. Fallback to light
 */
(function applyInitialTheme() {
    let theme = null;

    try {
        theme = localStorage.getItem(THEME_KEY);
    } catch (err) {
        // Private browsing or disabled storage
    }

    if (theme !== THEME_LIGHT && theme !== THEME_DARK) {
        if (window.matchMedia) {
            theme = window.matchMedia('(prefers-color-scheme: dark)').matches
                ? THEME_DARK
                : THEME_LIGHT;
        } else {
            theme = THEME_LIGHT;
        }
    }

    document.documentElement.dataset.theme = theme;

    // If the OS theme changes and the user has NOT set an explicit
    // preference, follow the OS.
    if (window.matchMedia) {
        const mq = window.matchMedia('(prefers-color-scheme: dark)');
        const onChange = (e) => {
            let stored = null;
            try { stored = localStorage.getItem(THEME_KEY); } catch (_) { }
            if (!stored) {
                document.documentElement.dataset.theme =
                    e.matches ? THEME_DARK : THEME_LIGHT;
            }
        };
        if (mq.addEventListener) mq.addEventListener('change', onChange);
        else if (mq.addListener) mq.addListener(onChange);
    }
})();

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
    constructor(status, detail, code = null) {
        super(typeof detail === 'string' ? detail : JSON.stringify(detail));
        this.name = 'ApiError';
        this.status = status;
        this.detail = detail;
        this.code = code;
    }

    get isNetwork() { return this.status === 0; }
    get isAuth() { return this.status === 401; }
    get isForbidden() { return this.status === 403; }
    get isNotFound() { return this.status === 404; }
    get isConflict() { return this.status === 409; }
    get isValidation() { return this.status === 422; }
    get isServer() { return this.status >= 500; }

    friendlyMessage() {
        if (this.isNetwork) return 'Cannot reach the server. Check your connection.';
        if (this.isAuth) return 'Your session has expired. Please log in again.';
        if (this.isForbidden) return "You don't have permission to do that.";
        if (this.isNotFound) return 'That resource no longer exists.';
        if (this.isConflict) return 'That action conflicts with existing data.';
        if (this.isValidation) return 'Some fields are invalid. Please review them.';
        if (this.isServer) return 'The server had a problem. Try again in a moment.';
        return this.message || 'Something went wrong.';
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
        if (e.name === 'AbortError') throw e;
        throw new ApiError(0, `Network error: ${e.message}`);
    }

    let body = null;
    const ct = resp.headers.get('content-type') || '';
    if (ct.includes('application/json')) {
        try { body = await resp.json(); } catch { /* ignore */ }
    }

    if (!resp.ok) {
        if (resp.status === 401) {
            const hadSession = !!token;
            Auth.clear();
            if (hadSession && !location.pathname.endsWith('index.html')) {
                location.href = 'index.html?expired=1';
                throw new ApiError(401, 'Session expired');
            }
        }
        const detail = body && body.detail ? body.detail : resp.statusText;
        throw new ApiError(resp.status, detail);
    }

    return body;
}

async function apiFetchWithRetry(path, options = {}, maxRetries = 2) {
    let lastError;
    for (let attempt = 0; attempt <= maxRetries; attempt++) {
        try {
            return await apiFetch(path, options);
        } catch (err) {
            lastError = err;
            if (!(err instanceof ApiError) || !err.isNetwork) throw err;
            if ((options.method || 'GET').toUpperCase() !== 'GET') throw err;
            if (attempt < maxRetries) {
                await new Promise(resolve => setTimeout(resolve, 1000 * (attempt + 1)));
            }
        }
    }
    throw lastError;
}

function friendlyErrorMessage(err, fallback = 'Something went wrong.') {
    return err instanceof ApiError ? err.friendlyMessage() : (err.message || fallback);
}

const Api = {
    register: (u, e, p) => apiFetch('/api/auth/register', { method: 'POST', body: JSON.stringify({ username: u, email: e, password: p }) }),
    login: (e, p) => apiFetch('/api/auth/login', { method: 'POST', body: JSON.stringify({ email: e, password: p }) }),
    me: () => apiFetchWithRetry('/api/auth/me'),
    listProblems: () => apiFetchWithRetry('/api/problems'),
    getProblem: (id) => apiFetchWithRetry(`/api/problems/${id}`),
    submit: (pid, l, code) => apiFetch('/api/submissions', {
        method: 'POST',
        body: JSON.stringify({ problem_id: pid, language: l, source_code: code }),
    }),
    getSubmission: (id) => apiFetchWithRetry(`/api/submissions/${id}`),
    listSubmissions: (limit = 50, offset = 0) =>
        apiFetchWithRetry(`/api/submissions?limit=${limit}&offset=${offset}`),
    leaderboard: (limit = 50, offset = 0) =>
        apiFetchWithRetry(`/api/leaderboard?limit=${limit}&offset=${offset}`),
    getStreak: () => apiFetchWithRetry('/api/stats/streak'),
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

function showFieldError(inputEl, message) {
    const field = inputEl.closest('.field');
    if (!field) return;
    let error = field.querySelector('.field-error');
    if (!error) {
        error = document.createElement('span');
        error.className = 'field-error';
        error.id = `${inputEl.id}-error`;
        error.setAttribute('aria-live', 'polite');
        field.appendChild(error);
    }
    error.textContent = message;
    inputEl.classList.add('invalid');
    inputEl.setAttribute('aria-invalid', 'true');
    inputEl.setAttribute('aria-describedby', error.id);
}

function clearFieldError(inputEl) {
    const field = inputEl.closest('.field');
    if (!field) return;
    const error = field.querySelector('.field-error');
    if (error) error.remove();
    inputEl.classList.remove('invalid');
    inputEl.removeAttribute('aria-invalid');
    inputEl.removeAttribute('aria-describedby');
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
// Icons
// ------------------------------------------------------------------

function icon(name, size = 14) {
    const svgNS = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(svgNS, 'svg');
    svg.setAttribute('width', String(size));
    svg.setAttribute('height', String(size));
    svg.setAttribute('viewBox', '0 0 24 24');
    svg.setAttribute('fill', 'none');
    svg.setAttribute('stroke', 'currentColor');
    svg.setAttribute('stroke-width', '2');
    svg.setAttribute('stroke-linecap', 'round');
    svg.setAttribute('stroke-linejoin', 'round');
    svg.setAttribute('aria-hidden', 'true');
    svg.style.flexShrink = '0';
    svg.style.verticalAlign = 'middle';

    const paths = {
        // Navigation
        grid: 'M3 3h7v7H3zM14 3h7v7h-7zM14 14h7v7h-7zM3 14h7v7H3z',
        code: 'M16 18l6-6-6-6M8 6l-6 6 6 6',
        list: 'M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01',
        trophy: 'M8 21h8M12 17v4M7 4h10v5a5 5 0 0 1-10 0V4zM5 4h2M17 4h2',
        user: 'M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2M12 3a4 4 0 1 0 0 8 4 4 0 0 0 0-8z',
        // Theme
        sun: 'M12 3v2M12 19v2M3 12h2M19 12h2M5.6 5.6l1.4 1.4M17 17l1.4 1.4M5.6 18.4L7 17M17 7l1.4-1.4M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z',
        moon: 'M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z',
        // Actions
        search: 'M21 21l-4.35-4.35M17 11a6 6 0 1 1-12 0 6 6 0 0 1 12 0z',
        chevron: 'M9 18l6-6-6-6',
        check: 'M20 6L9 17l-5-5',
        close: 'M18 6L6 18M6 6l12 12',
    };

    const p = document.createElementNS(svgNS, 'path');
    p.setAttribute('d', paths[name] || '');
    svg.appendChild(p);
    return svg;
}


// ------------------------------------------------------------------
// Breadcrumb
// ------------------------------------------------------------------

/**
 * Build a breadcrumb navigation.
 * @param {Array<{label: string, href?: string}>} items
 */
function breadcrumb(items) {
    const nav = el('nav', { class: 'breadcrumb', 'aria-label': 'Breadcrumb' });
    items.forEach((item, idx) => {
        if (idx > 0) nav.appendChild(el('span', { class: 'sep', text: '/' }));
        if (item.href) {
            nav.appendChild(el('a', { href: item.href, text: item.label }));
        } else {
            nav.appendChild(el('span', {
                class: 'current',
                'aria-current': 'page',
                text: item.label,
            }));
        }
    });
    return nav;
}


// ------------------------------------------------------------------
// Header
// ------------------------------------------------------------------

function renderHeader(activePage) {
    const user = Auth.getUser();
    const header = el('header', { class: 'app-header' });

    // Brand
    const brand = el('a', { href: 'dashboard.html', class: 'brand', text: 'CodeArena' });
    header.appendChild(brand);

    // Nav with icons
    const nav = el('nav');
    const pages = [
        { href: 'dashboard.html', label: 'Dashboard', key: 'dashboard', icon: 'grid' },
        { href: 'problems.html', label: 'Problems', key: 'problems', icon: 'code' },
        { href: 'history.html', label: 'Submissions', key: 'history', icon: 'list' },
        { href: 'leaderboard.html', label: 'Leaderboard', key: 'leaderboard', icon: 'trophy' },
    ];
    for (const p of pages) {
        const a = el('a', { href: p.href });
        if (activePage === p.key) {
            a.classList.add('active');
            a.setAttribute('aria-current', 'page');
        }
        a.appendChild(icon(p.icon));
        a.appendChild(document.createTextNode(p.label));
        nav.appendChild(a);
    }
    header.appendChild(nav);

    // Command palette trigger (fake search input)
    const searchBtn = el('button', {
        class: 'btn ghost',
        style: {
            display: 'flex',
            alignItems: 'center',
            gap: '8px',
            background: 'var(--neutral)',
            border: '1px solid var(--neutral-deep)',
            color: 'var(--secondary)',
            padding: '6px 12px',
            borderRadius: 'var(--radius-sm)',
            fontSize: '13px',
            marginRight: 'auto', // Pushes it away from the user menu
            cursor: 'pointer'
        },
        onclick: () => {
            // We can trigger the command palette manually
            // The global keyboard listener will catch a synthetic Ctrl+K,
            // but it's cleaner to just open it directly if we exposed the function.
            // Since the palette is encapsulated, we dispatch an event.
            window.dispatchEvent(new KeyboardEvent('keydown', {
                key: 'k',
                ctrlKey: true,
                metaKey: true
            }));
        }
    }, [
        icon('search', 14),
        el('span', { text: 'Search...' }),
        el('kbd', { text: '⌘K', style: { marginLeft: '8px', fontSize: '10px' } })
    ]);

    header.appendChild(searchBtn);

    if (user) {
        const menu = el('div', { class: 'user-menu' });

        // Theme toggle (only if dark mode is implemented)
        if (typeof currentTheme === 'function' && typeof setTheme === 'function') {
            const themeBtn = el('button', {
                class: 'theme-toggle',
                title: 'Toggle theme',
                'aria-label': 'Toggle theme',
                onclick: () => {
                    const next = currentTheme() === 'dark' ? 'light' : 'dark';
                    setTheme(next);
                    // Swap icon
                    themeBtn.innerHTML = '';
                    themeBtn.appendChild(icon(next === 'dark' ? 'sun' : 'moon'));
                },
            });
            themeBtn.appendChild(icon(currentTheme() === 'dark' ? 'sun' : 'moon'));
            menu.appendChild(themeBtn);
        }

        // Streak badge
        const streakContainer = el('div', {
            id: 'header-streak',
            class: 'streak-badge hidden',
            title: 'Daily streak',
        });
        menu.appendChild(streakContainer);

        if (typeof Api.getStreak === 'function') {
            Api.getStreak().then(data => {
                if (data && data.current_streak > 0) {
                    streakContainer.textContent = `🔥 ${data.current_streak}`;
                    streakContainer.classList.remove('hidden');
                }
            }).catch(() => { /* optional */ });
        }

        const avatarLink = createAvatarLink(user);
        menu.appendChild(avatarLink);
        menu.appendChild(el('span', { text: user.username }));

        const themeBtn = menu.querySelector('.theme-toggle');
        if (themeBtn) menu.appendChild(themeBtn);

        // Logout
        const logout = el('button', {
            class: 'btn-logout',
            text: 'Logout',
            onclick: () => Auth.logout(),
        });
        menu.appendChild(logout);
        menu.appendChild(el('a', {
            href: 'settings.html',
            text: 'Settings',
        }));
        header.appendChild(menu);
    }

    return header;
}

function requireAuth(activePage) {
    if (!Auth.isLoggedIn()) {
        location.href = 'index.html';
        return false;
    }
    document.body.classList.toggle(
        'no-sticky-header',
        localStorage.getItem('codearena_no_sticky') === '1'
    );
    const header = renderHeader(activePage);
    const skipLink = document.querySelector('.skip-link');
    if (skipLink) skipLink.after(header);
    else document.body.insertBefore(header, document.body.firstChild);
    refreshCachedUser(header);
    return true;
}

function createAvatarLink(user) {
    const initial = (user.username || '?').charAt(0).toUpperCase();
    const avatarLink = el('a', {
        href: `profile.html?user=${encodeURIComponent(user.username)}`,
        class: 'avatar',
        title: 'View your profile',
        'aria-label': `View ${user.username}'s profile`,
        style: { textDecoration: 'none' },
    }, [el('span', { class: 'avatar-initial', text: initial })]);

    if (user.avatar_url) {
        avatarLink.appendChild(el('img', {
            class: 'avatar-image',
            src: user.avatar_url,
            alt: '',
            referrerpolicy: 'no-referrer',
            onerror: event => event.currentTarget.remove(),
        }));
    }

    return avatarLink;
}

function refreshCachedUser(header) {
    const token = Auth.getToken();
    Api.me().then(user => {
        if (!user || Auth.getToken() !== token) return;
        Auth.saveToken(token, user);
        const avatarLink = header.querySelector('.avatar');
        if (avatarLink) avatarLink.replaceWith(createAvatarLink(user));
    }).catch(err => {
        console.warn('Could not refresh current user profile:', err);
    });
}

// ------------------------------------------------------------------
// Polling
// ------------------------------------------------------------------

function poll(fn, intervalMs = 2000, onError = null) {
    let stopped = false;
    let timer = null;
    async function tick() {
        if (stopped) return;
        let cont = false;
        try {
            cont = await fn();
        } catch (e) {
            if (onError) onError(e);
            else console.error('poll error', e);
        }
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
// Offline & reconnect states
// ------------------------------------------------------------------

(function connectionMonitor() {
    let hideTimer = null;

    function ensureBanner() {
        let banner = document.getElementById('connection-banner');
        if (!banner) {
            banner = document.createElement('div');
            banner.id = 'connection-banner';
            banner.className = 'connection-banner hidden';
            banner.setAttribute('role', 'status');
            banner.setAttribute('aria-live', 'polite');
            document.body.appendChild(banner);
        }
        return banner;
    }

    function showOffline() {
        clearTimeout(hideTimer);
        const banner = ensureBanner();
        banner.textContent = '⚠ You are offline. Your work is saved locally.';
        banner.classList.remove('hidden');
        void banner.offsetWidth;
        banner.classList.add('visible');
    }

    function showOnline() {
        const banner = ensureBanner();
        banner.textContent = '✓ Back online.';
        banner.classList.remove('hidden');
        void banner.offsetWidth;
        banner.classList.add('visible');
        clearTimeout(hideTimer);
        hideTimer = setTimeout(() => {
            banner.classList.remove('visible');
            hideTimer = setTimeout(() => banner.classList.add('hidden'), 300);
        }, 3000);
    }

    window.addEventListener('offline', showOffline);
    window.addEventListener('online', showOnline);

    if (!navigator.onLine) showOffline();
})();
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
    if (window.matchMedia?.('(prefers-reduced-motion: reduce)').matches) return;

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

// ------------------------------------------------------------------
// Keyboard shortcuts modal
// ------------------------------------------------------------------

const SHORTCUTS = [
    {
        category: 'Navigation',
        items: [
            { keys: ['g', 'd'], label: 'Go to Dashboard' },
            { keys: ['g', 'p'], label: 'Go to Problems' },
            { keys: ['g', 's'], label: 'Go to Submissions' },
            { keys: ['g', 'l'], label: 'Go to Leaderboard' },
            { keys: ['g', 'm'], label: 'Go to your Profile' },
        ]
    },
    {
        category: 'Editor',
        items: [
            { keys: ['Ctrl', 'Enter'], label: 'Submit code' },
            { keys: ['Tab'], label: 'Insert 4 spaces' },
            { keys: ['?'], label: 'Show this help menu' },
            { keys: ['Esc'], label: 'Close modal or dialog' },
        ]
    }
];

(function setupShortcutsModal() {
    if (typeof window === 'undefined') return;

    let modalEl = null;
    let backdropEl = null;
    let lastFocusedElement = null;
    let currentSequence = '';

    function buildModal() {
        if (modalEl) return;

        backdropEl = el('div', { class: 'shortcuts-backdrop' });
        backdropEl.addEventListener('click', (e) => {
            if (e.target === backdropEl) closeModal();
        });

        modalEl = el('div', {
            class: 'shortcuts-modal',
            role: 'dialog',
            'aria-modal': 'true',
            'aria-labelledby': 'shortcuts-title'
        });

        // Header
        const header = el('div', { class: 'shortcuts-header' }, [
            el('h2', { id: 'shortcuts-title', text: 'Keyboard Shortcuts' }),
            el('button', {
                class: 'shortcuts-close',
                'aria-label': 'Close',
                onclick: closeModal
            }, [
                // Simple X icon
                (() => {
                    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
                    svg.setAttribute('width', '20');
                    svg.setAttribute('height', '20');
                    svg.setAttribute('viewBox', '0 0 24 24');
                    svg.setAttribute('fill', 'none');
                    svg.setAttribute('stroke', 'currentColor');
                    svg.setAttribute('stroke-width', '2');
                    const p = document.createElementNS('http://www.w3.org/2000/svg', 'path');
                    p.setAttribute('d', 'M18 6L6 18M6 6l12 12');
                    svg.appendChild(p);
                    return svg;
                })()
            ])
        ]);
        modalEl.appendChild(header);

        // Body
        const body = el('div', { class: 'shortcuts-body' });

        SHORTCUTS.forEach(section => {
            const sectionEl = el('div', { class: 'shortcuts-section' }, [
                el('h3', { text: section.category })
            ]);

            section.items.forEach(item => {
                const keysEl = el('div', { class: 'shortcut-keys' });
                item.keys.forEach((key, i) => {
                    keysEl.appendChild(el('kbd', { text: key }));
                    if (i < item.keys.length - 1) {
                        keysEl.appendChild(el('span', { text: '+', class: 'muted', style: { fontSize: '10px' } }));
                    }
                });

                sectionEl.appendChild(el('div', { class: 'shortcut-row' }, [
                    el('span', { text: item.label }),
                    keysEl
                ]));
            });

            body.appendChild(sectionEl);
        });

        modalEl.appendChild(body);
        backdropEl.appendChild(modalEl);
        document.body.appendChild(backdropEl);
    }

    function openModal() {
        buildModal();
        lastFocusedElement = document.activeElement;

        backdropEl.classList.add('visible');

        // Focus the close button for accessibility
        const closeBtn = modalEl.querySelector('.shortcuts-close');
        if (closeBtn) closeBtn.focus();

        // Trap focus inside modal
        document.addEventListener('keydown', trapFocus);
    }

    function closeModal() {
        if (!backdropEl) return;
        backdropEl.classList.remove('visible');
        document.removeEventListener('keydown', trapFocus);

        // Restore focus to where it was before the modal opened
        if (lastFocusedElement && lastFocusedElement.focus) {
            lastFocusedElement.focus();
        }
    }

    function trapFocus(e) {
        if (e.key !== 'Tab') return;

        const focusable = modalEl.querySelectorAll('button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])');
        if (focusable.length === 0) return;

        const first = focusable[0];
        const last = focusable[focusable.length - 1];

        if (e.shiftKey && document.activeElement === first) {
            e.preventDefault();
            last.focus();
        } else if (!e.shiftKey && document.activeElement === last) {
            e.preventDefault();
            first.focus();
        }
    }

    // Global keydown handler
    window.addEventListener('keydown', (e) => {
        // Ignore if user is typing in an input/textarea or Monaco editor
        const tag = e.target.tagName.toLowerCase();
        if (tag === 'input' || tag === 'textarea' || e.target.closest('.monaco-editor')) {
            return;
        }

        // Escape closes the modal
        if (e.key === 'Escape') {
            closeModal();
            return;
        }

        // Question mark opens the modal
        if (e.key === '?' || (e.shiftKey && e.key === '/')) {
            e.preventDefault();
            openModal();
            return;
        }

        // Quick navigation sequences (g + d, g + p, etc.)
        if (e.key === 'g') {
            currentSequence = 'g';
            setTimeout(() => { currentSequence = ''; }, 1000); // reset after 1 second
            return;
        }

        if (currentSequence === 'g') {
            currentSequence = '';
            const routes = {
                'd': 'dashboard.html',
                'p': 'problems.html',
                's': 'history.html',
                'l': 'leaderboard.html',
                'm': () => {
                    const user = Auth.getUser();
                    return user ? `profile.html?user=${encodeURIComponent(user.username)}` : null;
                }
            };

            const dest = routes[e.key];
            if (dest) {
                e.preventDefault();
                const url = typeof dest === 'function' ? dest() : dest;
                if (url) location.href = url;
            }
        }
    });
})();

// ------------------------------------------------------------------
// Global Command Palette (Ctrl+K / Cmd+K)
// ------------------------------------------------------------------

(function setupCommandPalette() {
    if (typeof window === 'undefined') return;

    let backdropEl = null;
    let inputEl = null;
    let resultsEl = null;
    let isOpen = false;

    // Cache problems so we don't fetch on every keystroke
    let allProblems = [];
    let hasFetchedProblems = false;

    // Track which item is selected via keyboard
    let activeIndex = 0;
    let currentFilteredItems = [];

    function openPalette() {
        if (isOpen) return;
        isOpen = true;

        buildPalette();
        backdropEl.classList.add('visible');

        // Reset state
        inputEl.value = '';
        activeIndex = 0;
        renderResults('');

        // Focus input after transition
        setTimeout(() => inputEl.focus(), 50);

        // Fetch problems in background if not done yet
        if (!hasFetchedProblems) {
            hasFetchedProblems = true;
            Api.listProblems().then(problems => {
                allProblems = problems;
                // If the palette is still open and empty, re-render to show them
                if (isOpen && inputEl.value === '') {
                    renderResults('');
                }
            }).catch(err => {
                console.warn('Command palette could not fetch problems:', err);
            });
        }
    }

    function closePalette() {
        if (!isOpen) return;
        isOpen = false;
        backdropEl.classList.remove('visible');
    }

    function buildPalette() {
        if (backdropEl) return;

        backdropEl = el('div', {
            class: 'palette-backdrop', onclick: (e) => {
                if (e.target === backdropEl) closePalette();
            }
        });

        const modal = el('div', {
            class: 'palette-modal',
            role: 'dialog',
            'aria-modal': 'true'
        });

        // Search bar
        const searchWrap = el('div', { class: 'palette-search-wrap' }, [
            el('div', { class: 'palette-search-icon' }, [icon('search', 18)]),
            el('input', {
                class: 'palette-input',
                type: 'text',
                placeholder: 'Search problems, pages, or actions...',
                oninput: (e) => renderResults(e.target.value),
                onkeydown: handleInputKeydown
            }),
            el('span', { class: 'palette-esc-hint', text: 'ESC' })
        ]);
        inputEl = searchWrap.querySelector('input');
        modal.appendChild(searchWrap);

        // Results area
        resultsEl = el('div', { class: 'palette-results' });
        modal.appendChild(resultsEl);

        backdropEl.appendChild(modal);
        document.body.appendChild(backdropEl);
    }

    function getActions() {
        const actions = [
            { type: 'nav', label: 'Dashboard', href: 'dashboard.html', icon: 'grid' },
            { type: 'nav', label: 'Problems', href: 'problems.html', icon: 'code' },
            { type: 'nav', label: 'Submissions', href: 'history.html', icon: 'list' },
            { type: 'nav', label: 'Leaderboard', href: 'leaderboard.html', icon: 'trophy' },
            {
                type: 'action', label: 'Toggle Theme', action: () => {
                    const current = currentTheme();
                    setTheme(current === 'light' ? 'dark' : 'light');
                }, icon: 'moon'
            },
            { type: 'action', label: 'Logout', action: () => Auth.logout(), icon: 'chevron' }
        ];

        const user = Auth.getUser();
        if (user) {
            actions.unshift({
                type: 'nav',
                label: 'My Profile',
                href: `profile.html?user=${encodeURIComponent(user.username)}`,
                icon: 'user'
            });
        }
        return actions;
    }

    function renderResults(query) {
        const q = query.toLowerCase().trim();
        resultsEl.innerHTML = '';
        activeIndex = 0;
        currentFilteredItems = [];

        // 1. Filter Actions & Pages
        const matchedActions = getActions().filter(a => a.label.toLowerCase().includes(q));

        // 2. Filter Problems (limit to 5 matches to keep it snappy)
        let matchedProblems = [];
        if (hasFetchedProblems) {
            matchedProblems = allProblems
                .filter(p => p.title.toLowerCase().includes(q))
                .slice(0, 5);
        }

        if (matchedActions.length === 0 && matchedProblems.length === 0) {
            resultsEl.appendChild(el('div', { class: 'palette-empty', text: 'No results found.' }));
            return;
        }

        // Render Actions
        if (matchedActions.length > 0) {
            resultsEl.appendChild(el('div', { class: 'palette-section-title', text: 'Navigation & Actions' }));
            matchedActions.forEach(item => {
                currentFilteredItems.push(item);
                resultsEl.appendChild(buildRow(item, currentFilteredItems.length - 1));
            });
        }

        // Render Problems
        if (matchedProblems.length > 0) {
            resultsEl.appendChild(el('div', { class: 'palette-section-title', text: 'Problems' }));
            matchedProblems.forEach(p => {
                const item = {
                    type: 'problem',
                    label: p.title,
                    subtitle: p.difficulty,
                    href: `problem.html?id=${p.id}`,
                    icon: 'code'
                };
                currentFilteredItems.push(item);
                resultsEl.appendChild(buildRow(item, currentFilteredItems.length - 1));
            });
        }

        // Highlight the first item by default
        updateActiveItem(0);
    }

    function buildRow(item, index) {
        const isActive = index === activeIndex;
        const row = el('div', {
            class: `palette-item ${isActive ? 'active' : ''}`,
            'data-index': index,
            onclick: () => executeItem(item),
            onmouseenter: () => updateActiveItem(index)
        }, [
            el('div', { class: 'palette-item-left' }, [
                el('div', { class: 'palette-item-icon' }, [icon(item.icon || 'chevron', 16)]),
                el('span', { class: 'palette-item-text', text: item.label }),
                item.subtitle ? el('span', { class: 'palette-item-subtitle', text: `(${item.subtitle})` }) : null
            ]),
            item.type === 'action' ? el('div', { class: 'palette-item-shortcut' }, [el('kbd', { text: '↵' })]) : null
        ].filter(Boolean));

        return row;
    }

    function updateActiveItem(newIndex) {
        if (newIndex < 0 || newIndex >= currentFilteredItems.length) return;

        // Remove active class from old element
        const oldActive = resultsEl.querySelector('.palette-item.active');
        if (oldActive) oldActive.classList.remove('active');

        activeIndex = newIndex;

        // Add active class to new element
        const newActive = resultsEl.querySelector(`.palette-item[data-index="${activeIndex}"]`);
        if (newActive) {
            newActive.classList.add('active');
            // Scroll into view if needed
            newActive.scrollIntoView({ block: 'nearest' });
        }
    }

    function executeItem(item) {
        closePalette();
        if (item.type === 'nav' || item.type === 'problem') {
            location.href = item.href;
        } else if (item.type === 'action') {
            item.action();
        }
    }

    function handleInputKeydown(e) {
        if (e.key === 'ArrowDown') {
            e.preventDefault();
            updateActiveItem(activeIndex + 1);
        } else if (e.key === 'ArrowUp') {
            e.preventDefault();
            updateActiveItem(activeIndex - 1);
        } else if (e.key === 'Enter') {
            e.preventDefault();
            if (currentFilteredItems[activeIndex]) {
                executeItem(currentFilteredItems[activeIndex]);
            }
        } else if (e.key === 'Escape') {
            e.preventDefault();
            closePalette();
        }
    }

    // Global listener for Cmd+K / Ctrl+K
    window.addEventListener('keydown', (e) => {
        if ((e.ctrlKey || e.metaKey) && e.key === 'k') {
            e.preventDefault(); // Prevent browser search bar
            if (isOpen) closePalette();
            else openPalette();
        }
    });
})();