const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");

const PORT = 3000;
const POSTS_DIR = path.join(__dirname, "posts");
const WIKIS_DIR = path.join(__dirname, "wikis");
const PUBLIC_DIR = path.join(__dirname, "public");
const ALLOWED_FONTS = ["Inter", "Merriweather", "JetBrains Mono", "Lora", "Space Grotesk"];

const MIME_TYPES = {
  ".html": "text/html",
  ".css": "text/css",
  ".js": "text/javascript",
  ".json": "application/json",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".svg": "image/svg+xml",
  ".ico": "image/x-icon",
  ".wasm": "application/wasm",
};

// ── API: list all posts, sorted newest-first ──────────────
function getAllPosts() {
  const files = fs.readdirSync(POSTS_DIR).filter((f) => f.endsWith(".json"));
  const posts = files.map((file) => {
    const raw = fs.readFileSync(path.join(POSTS_DIR, file), "utf-8");
    const post = JSON.parse(raw);
    post.slug = file.replace(".json", "");
    return post;
  });
  posts.sort((a, b) => new Date(b.date) - new Date(a.date));
  return posts;
}

// ── API: get a single post by slug ────────────────────────
function getPost(slug) {
  const filePath = path.join(POSTS_DIR, `${slug}.json`);
  if (!filePath.startsWith(POSTS_DIR)) return null;
  try {
    const raw = fs.readFileSync(filePath, "utf-8");
    const post = JSON.parse(raw);
    post.slug = slug;
    return post;
  } catch {
    return null;
  }
}

// ── API: list all wikis, sorted alphabetically ────────────
function getAllWikis() {
  const files = fs.readdirSync(WIKIS_DIR).filter((f) => f.endsWith(".json"));
  const wikis = files.map((file) => {
    const raw = fs.readFileSync(path.join(WIKIS_DIR, file), "utf-8");
    const wiki = JSON.parse(raw);
    wiki.slug = file.replace(".json", "");
    return wiki;
  });
  wikis.sort((a, b) => a.name.localeCompare(b.name));
  return wikis;
}

// ── API: get a single wiki by slug ────────────────────────
function getWiki(slug) {
  const filePath = path.join(WIKIS_DIR, `${slug}.json`);
  if (!filePath.startsWith(WIKIS_DIR)) return null;
  try {
    const raw = fs.readFileSync(filePath, "utf-8");
    const wiki = JSON.parse(raw);
    wiki.slug = slug;
    return wiki;
  } catch {
    return null;
  }
}

// ── Helper: read the full request body ────────────────────
function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on("data", (chunk) => chunks.push(chunk));
    req.on("end", () => resolve(Buffer.concat(chunks).toString()));
    req.on("error", reject);
  });
}

// ── Helper: turn a title into a URL-safe slug ─────────────
function slugify(title) {
  return title
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-|-$/g, "");
}

// ── Helper: escape a string for safe insertion into HTML ──
function escapeHtml(str) {
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

// ── Helper: build the per-post SEO <head> block ───────────
function buildPostSeo(post, origin) {
  const title = escapeHtml(post.title);
  const desc = escapeHtml(post.excerpt || "");
  const url = `${origin}/post/${post.slug}`;
  return [
    `<title>${title}</title>`,
    `<meta name="description" content="${desc}">`,
    `<link rel="canonical" href="${url}">`,
    `<meta property="og:type" content="article">`,
    `<meta property="og:title" content="${title}">`,
    `<meta property="og:description" content="${desc}">`,
    `<meta property="og:url" content="${url}">`,
    `<meta property="article:published_time" content="${escapeHtml(post.date)}">`,
    `<meta name="twitter:card" content="summary">`,
    `<meta name="twitter:title" content="${title}">`,
    `<meta name="twitter:description" content="${desc}">`,
  ].join("\n  ");
}

// ── Helper: send JSON response ────────────────────────────
function sendJSON(res, status, data) {
  res.writeHead(status, { "Content-Type": "application/json" });
  res.end(JSON.stringify(data));
}

async function handleRequest(req, res) {
  // Parse URL and query string
  const parsedUrl = new URL(req.url, `http://localhost:${PORT}`);
  const pathname = parsedUrl.pathname;

  // API: all posts (GET with optional ?q=search) or create post (POST)
  if (pathname === "/api/posts" && req.method === "GET") {
    let posts = getAllPosts();
    const wikiFilter = parsedUrl.searchParams.get("wiki");
    if (wikiFilter) {
      posts = posts.filter((p) => p.wiki === wikiFilter);
    }
    const query = parsedUrl.searchParams.get("q");
    if (query) {
      const q = query.toLowerCase();
      posts = posts.filter(
        (p) =>
          p.title.toLowerCase().includes(q) ||
          p.excerpt.toLowerCase().includes(q) ||
          p.body.toLowerCase().includes(q)
      );
    }
    sendJSON(res, 200, posts);
    return;
  }

  if (pathname === "/api/posts" && req.method === "POST") {
    const body = await readBody(req);
    let data;
    try {
      data = JSON.parse(body);
    } catch {
      sendJSON(res, 400, { error: "Invalid JSON" });
      return;
    }

    if (!data.title || !data.body) {
      sendJSON(res, 400, { error: "Title and body are required" });
      return;
    }

    const slug = slugify(data.title);
    if (!slug) {
      sendJSON(res, 400, { error: "Title must contain at least one letter or number" });
      return;
    }

    const filePath = path.join(POSTS_DIR, `${slug}.json`);
    if (fs.existsSync(filePath)) {
      sendJSON(res, 409, { error: "A post with that title already exists" });
      return;
    }

    const font = ALLOWED_FONTS.includes(data.font) ? data.font : "Inter";

    const post = {
      title: data.title,
      date: new Date().toISOString().split("T")[0],
      excerpt: data.excerpt || data.body.slice(0, 120) + "...",
      body: data.body,
      font: font,
      wiki: data.wiki || null,
    };

    fs.writeFileSync(filePath, JSON.stringify(post, null, 2));
    post.slug = slug;
    sendJSON(res, 201, post);
    return;
  }

  // API: single post — GET, PUT, or DELETE /api/posts/hello-world
  const postMatch = pathname.match(/^\/api\/posts\/([a-z0-9-]+)$/);
  if (postMatch && req.method === "GET") {
    const post = getPost(postMatch[1]);
    if (!post) {
      sendJSON(res, 404, { error: "Post not found" });
      return;
    }
    sendJSON(res, 200, post);
    return;
  }

  if (postMatch && req.method === "PUT") {
    const slug = postMatch[1];
    const filePath = path.join(POSTS_DIR, `${slug}.json`);
    if (!filePath.startsWith(POSTS_DIR) || !fs.existsSync(filePath)) {
      sendJSON(res, 404, { error: "Post not found" });
      return;
    }

    const body = await readBody(req);
    let data;
    try {
      data = JSON.parse(body);
    } catch {
      sendJSON(res, 400, { error: "Invalid JSON" });
      return;
    }

    // Read existing post, merge updates
    const existing = JSON.parse(fs.readFileSync(filePath, "utf-8"));
    if (data.title) existing.title = data.title;
    if (data.excerpt) existing.excerpt = data.excerpt;
    if (data.body) existing.body = data.body;
    if (data.font) existing.font = ALLOWED_FONTS.includes(data.font) ? data.font : existing.font;
    if ("wiki" in data) existing.wiki = data.wiki || null;

    fs.writeFileSync(filePath, JSON.stringify(existing, null, 2));
    existing.slug = slug;
    sendJSON(res, 200, existing);
    return;
  }

  if (postMatch && req.method === "DELETE") {
    const slug = postMatch[1];
    const filePath = path.join(POSTS_DIR, `${slug}.json`);
    if (!filePath.startsWith(POSTS_DIR) || !fs.existsSync(filePath)) {
      sendJSON(res, 404, { error: "Post not found" });
      return;
    }
    fs.unlinkSync(filePath);
    sendJSON(res, 200, { deleted: slug });
    return;
  }

  // ── Wiki API routes ──────────────────────────────────────

  // API: all wikis (GET) or create wiki (POST)
  if (pathname === "/api/wikis" && req.method === "GET") {
    sendJSON(res, 200, getAllWikis());
    return;
  }

  if (pathname === "/api/wikis" && req.method === "POST") {
    const body = await readBody(req);
    let data;
    try {
      data = JSON.parse(body);
    } catch {
      sendJSON(res, 400, { error: "Invalid JSON" });
      return;
    }

    if (!data.name) {
      sendJSON(res, 400, { error: "Name is required" });
      return;
    }

    const slug = slugify(data.name);
    if (!slug) {
      sendJSON(res, 400, { error: "Name must contain at least one letter or number" });
      return;
    }

    const filePath = path.join(WIKIS_DIR, `${slug}.json`);
    if (fs.existsSync(filePath)) {
      sendJSON(res, 409, { error: "A wiki with that name already exists" });
      return;
    }

    const wiki = {
      name: data.name,
      description: data.description || "",
      created: new Date().toISOString().split("T")[0],
    };

    fs.writeFileSync(filePath, JSON.stringify(wiki, null, 2));
    wiki.slug = slug;
    sendJSON(res, 201, wiki);
    return;
  }

  // API: single wiki with its posts — GET /api/wikis/engineering
  const wikiMatch = pathname.match(/^\/api\/wikis\/([a-z0-9-]+)$/);
  if (wikiMatch && req.method === "GET") {
    const wiki = getWiki(wikiMatch[1]);
    if (!wiki) {
      sendJSON(res, 404, { error: "Wiki not found" });
      return;
    }
    // Attach posts that belong to this wiki
    const allPosts = getAllPosts();
    wiki.posts = allPosts.filter((p) => p.wiki === wikiMatch[1]);
    sendJSON(res, 200, wiki);
    return;
  }

  // Pages: /wiki → wiki index
  if (pathname === "/wiki") {
    const data = fs.readFileSync(path.join(PUBLIC_DIR, "wiki.html"));
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(data);
    return;
  }

  // Pages: /wiki/some-slug → single wiki view
  if (pathname.startsWith("/wiki/")) {
    const data = fs.readFileSync(path.join(PUBLIC_DIR, "wiki-view.html"));
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(data);
    return;
  }

  // Simulator page: /sim → serve public/sim/index.html
  if (pathname === "/sim") {
    const data = fs.readFileSync(path.join(PUBLIC_DIR, "sim", "index.html"));
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(data);
    return;
  }

  // Explainer pages: /explainers/:slug → serve public/explainers/:slug.html
  const explainerMatch = pathname.match(/^\/explainers\/([a-z0-9-]+)$/);
  if (explainerMatch) {
    const explainerPath = path.join(PUBLIC_DIR, "explainers", `${explainerMatch[1]}.html`);
    if (!explainerPath.startsWith(PUBLIC_DIR)) {
      res.writeHead(403);
      res.end("Forbidden");
      return;
    }
    try {
      const data = fs.readFileSync(explainerPath);
      res.writeHead(200, { "Content-Type": "text/html" });
      res.end(data);
    } catch {
      res.writeHead(404, { "Content-Type": "text/html" });
      res.end("<h1>404</h1>");
    }
    return;
  }

  // Pages: /edit/some-slug → serve edit.html (client-side routing)
  if (pathname.startsWith("/edit/")) {
    const data = fs.readFileSync(path.join(PUBLIC_DIR, "edit.html"));
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(data);
    return;
  }

  // Pages: /new → serve new.html (compose page)
  if (pathname === "/new") {
    const data = fs.readFileSync(path.join(PUBLIC_DIR, "new.html"));
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(data);
    return;
  }

  // Pages: /about → serve about.html (identity / what-this-is page)
  if (pathname === "/about") {
    const data = fs.readFileSync(path.join(PUBLIC_DIR, "about.html"));
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(data);
    return;
  }

  // Pages: /post/some-slug → serve post.html (client-side routing).
  // Inject real SEO tags server-side so crawlers and link previews
  // see the post's title/description without running the page's JS.
  if (pathname.startsWith("/post/")) {
    let html = fs.readFileSync(path.join(PUBLIC_DIR, "post.html"), "utf-8");
    const slug = pathname.slice("/post/".length);
    const post = getPost(slug);
    if (post) {
      const origin = `http://${req.headers.host || `localhost:${PORT}`}`;
      html = html.replace("<title>Post</title>", buildPostSeo(post, origin));
    }
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(html);
    return;
  }

  // Static files
  let filePath = pathname === "/" ? "/index.html" : pathname;
  const fullPath = path.join(PUBLIC_DIR, filePath);

  if (!fullPath.startsWith(PUBLIC_DIR)) {
    res.writeHead(403);
    res.end("Forbidden");
    return;
  }

  const ext = path.extname(fullPath);
  const contentType = MIME_TYPES[ext] || "application/octet-stream";

  fs.readFile(fullPath, (err, data) => {
    if (err) {
      res.writeHead(404, { "Content-Type": "text/html" });
      res.end("<h1>404</h1>");
      return;
    }
    res.writeHead(200, { "Content-Type": contentType });
    res.end(data);
  });
}

// http.createServer doesn't understand async functions natively.
// We wrap it so that promise rejections don't crash silently.
const server = http.createServer((req, res) => {
  handleRequest(req, res).catch((err) => {
    console.error(err);
    res.writeHead(500, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: "Internal server error" }));
  });
});

server.listen(PORT, () => {
  console.log(`Blog running at http://localhost:${PORT}`);
});
