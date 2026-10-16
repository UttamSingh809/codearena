# CodeArena UI

Minimal HTML/JS frontend for CodeArena. No build step, framework, or bundler is required. Every page loads `static/app.js` (shared authentication, fetch wrappers, and DOM helpers) and `static/style.css`.

## Pages

| File | Purpose |
|---|---|
| `index.html` | Login / register |
| `problems.html` | List active problems |
| `problem.html?id=N` | View a problem and submit a solution |
| `submission.html?id=N` | View submission status and result |
| `history.html` | View the user's submission history |
| `leaderboard.html` | View global rankings |

## Running the UI

Serve the `ui/` directory over HTTP and open `index.html`:

```bash
cd ~/projects/codearena
python3 -m http.server 3000 --directory ui
```

Then visit [http://localhost:3000](http://localhost:3000).

The UI communicates with the API at `http://localhost:8000`. If you serve the UI from a different host or port, update `API_BASE` in `static/app.js` to point to the API's URL.

## Authentication

The JWT is stored in `localStorage` under the key `codearena_token`.

The API's `GET /api/auth/me` endpoint validates the token for authenticated requests. If the token has expired, the API returns `401 Unauthorized`, and the UI redirects the user to `index.html`.

To log out, click the **Logout** button in the header. This clears the token from `localStorage` and redirects to `index.html`.

## CORS

The API uses `allow_origins=["*"]` for development. If you restrict allowed origins in production, ensure the UI's origin is included in the whitelist.

## Adding Pages

Every page follows the same pattern:

1. Include `static/app.js`.
2. Call `requireAuth('page-key')` at the beginning of the page's script. This checks for a token, redirects unauthenticated users, and renders the header.
3. Use the `Api.*` helpers to call the backend.
4. Use `el()`, `verdictBadge()`, and `formatDate()` to render content.

No build step is required. Edit the files and reload the browser.

---

## How to Run and Test

### Start the Backend

Open three terminals.

**Terminal A — API**

```bash
cd ~/projects/codearena/api
source venv/bin/activate

export DATABASE_URL=postgresql://codearena:codearena@localhost:5432/codearena
export JWT_SECRET="$(python3 -c 'import secrets; print(secrets.token_urlsafe(64))')"

uvicorn src.main:app --host 0.0.0.0 --port 8000
```

**Terminal B — Worker**

```bash
cd ~/projects/codearena/os/worker

sudo DATABASE_URL=postgresql://codearena:codearena@localhost:5432/codearena \
    ./codearena-worker \
    --id ui-worker \
    --queue /codearena_queue \
    --db postgresql://codearena:codearena@localhost:5432/codearena \
    --cgroup-root /sys/fs/cgroup/codearena \
    --tmp /tmp/codearena \
    --all-ns \
    --seccomp
```

**Terminal C — UI**

```bash
cd ~/projects/codearena
python3 -m http.server 3000 --directory ui
```

### Test in the Browser

Open [http://localhost:3000](http://localhost:3000) in your browser.

#### Test 1 — Register

1. Click **Register**.
2. Enter a username, email address, and password.
3. Submit the form.

You should be redirected to `problems.html`.

#### Test 2 — View Problems

The problem list should display the three seeded problems:

- Sum Two Numbers
- Fibonacci Sequence
- Palindrome Check

#### Test 3 — Submit a Solution

1. Click **Sum Two Numbers**.
2. Enter the following C solution:

```c
#include <stdio.h>

int main(void) {
    int a, b;
    scanf("%d %d", &a, &b);
    printf("%d\n", a + b);
    return 0;
}
```

3. Select **C** as the programming language.
4. Click **Submit**.

You should see a message similar to:

```text
Submission #N created. Status: PENDING.
```

Click **View status →** to open the submission page.

#### Test 4 — Watch the Verdict

The `submission.html` page polls for updates every two seconds. Within a few seconds, the verdict should appear as `AC` if the worker successfully executes the solution and all test cases pass.

#### Test 5 — Submission History

Click **My Submissions** in the header.

Your recent submission should appear in the table with the verdict `AC`.

#### Test 6 — Leaderboard

Click **Leaderboard**.

Your username should appear with a score of `100`, assuming the seed configuration and scoring rules award that score for the accepted submission.

#### Test 7 — Logout

1. Click **Logout** in the header.
2. You should be redirected to `index.html`.
3. Try reopening `problems.html`.

The UI should redirect you to `index.html` because the authentication token has been removed.

---

## Sanity Check with the API

If the UI displays unexpected errors, verify that the API is working using `curl`.

### Check API Health

```bash
curl -s http://localhost:8000/api/health
```

### Register a Test User

This request creates a user and returns an authentication token:

```bash
curl -s -X POST http://localhost:8000/api/auth/register \
    -H "Content-Type: application/json" \
    -d '{
        "username": "uicheck",
        "email": "uicheck@example.com",
        "password": "passw0rd123"
    }'
```

If the `curl` requests work but the UI does not, check the CORS configuration and `API_BASE` in `static/app.js`.

Open the browser's developer tools by pressing **F12**, then inspect the **Console** and **Network** tabs for errors and failed requests.

---

## Troubleshooting

| Problem | What to check |
|---|---|
| UI cannot connect to the API | Verify that the API is running on port `8000` and that `API_BASE` is correct. |
| Browser reports a CORS error | Ensure the API allows requests from the UI's origin. |
| Login or registration fails | Inspect the API response in the browser's Network tab. |
| Submission remains `PENDING` | Verify that the worker is running and connected to the correct queue and database. |
| Submission never receives a verdict | Check worker logs, sandbox configuration, test cases, and database connectivity. |
| Protected pages redirect to login | Register or log in again to obtain a valid token. |
| Leaderboard score differs from `100` | Verify the backend's scoring rules and the user's accepted submissions. |

**Note:** These steps assume that PostgreSQL is running, the database schema has been migrated, the worker's queue and sandbox environment are configured, and the required seed problems exist. Run your database migration and seed commands before testing if you have not already done so.
