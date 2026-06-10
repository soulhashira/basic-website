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

function handleRequest(req, res) {
  // API: all posts
  if (req.url === "/api/posts") {
    const posts = getAllPosts();
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify(posts));
    return;
  }

  // API: single post — /api/posts/hello-world
  const postMatch = req.url.match(/^\/api\/posts\/([a-z0-9-]+)$/);
  if (postMatch) {
    const post = getPost(postMatch[1]);
    if (!post) {
      res.writeHead(404, { "Content-Type": "application/json" });
      res.end(JSON.stringify({ error: "Post not found" }));
      return;
    }
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify(post));
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

const server = http.createServer(handleRequest);

server.listen(PORT, () => {
  console.log(`Blog running at http://localhost:${PORT}`);
});
