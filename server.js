const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");

const PORT = 3000;
const POSTS_DIR = path.join(__dirname, "posts");
const PUBLIC_DIR = path.join(__dirname, "public");

const MIME_TYPES = {
  ".html": "text/html",
  ".css": "text/css",
  ".js": "text/javascript",
  ".json": "application/json",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".svg": "image/svg+xml",
  ".ico": "image/x-icon",
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

// ── Helper: send JSON response ────────────────────────────
function sendJSON(res, status, data) {
  res.writeHead(status, { "Content-Type": "application/json" });
  res.end(JSON.stringify(data));
}

async function handleRequest(req, res) {
  // API: all posts (GET) or create post (POST)
  if (req.url === "/api/posts" && req.method === "GET") {
    sendJSON(res, 200, getAllPosts());
    return;
  }

  if (req.url === "/api/posts" && req.method === "POST") {
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

    const post = {
      title: data.title,
      date: new Date().toISOString().split("T")[0],
      excerpt: data.excerpt || data.body.slice(0, 120) + "...",
      body: data.body,
    };

    fs.writeFileSync(filePath, JSON.stringify(post, null, 2));
    post.slug = slug;
    sendJSON(res, 201, post);
    return;
  }

  // API: single post — GET or DELETE /api/posts/hello-world
  const postMatch = req.url.match(/^\/api\/posts\/([a-z0-9-]+)$/);
  if (postMatch && req.method === "GET") {
    const post = getPost(postMatch[1]);
    if (!post) {
      sendJSON(res, 404, { error: "Post not found" });
      return;
    }
    sendJSON(res, 200, post);
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

  // Pages: /new → serve new.html (compose page)
  if (req.url === "/new") {
    const data = fs.readFileSync(path.join(PUBLIC_DIR, "new.html"));
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(data);
    return;
  }

  // Pages: /post/some-slug → serve post.html (client-side routing)
  if (req.url.startsWith("/post/")) {
    const filePath = path.join(PUBLIC_DIR, "post.html");
    const data = fs.readFileSync(filePath);
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(data);
    return;
  }

  // Static files
  let filePath = req.url === "/" ? "/index.html" : req.url;
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
