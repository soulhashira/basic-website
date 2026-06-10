const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");

const PORT = 3000;

// Map file extensions to Content-Type headers.
// Without the correct type, the browser won't know how to handle the file.
const MIME_TYPES = {
  ".html": "text/html",
  ".css": "text/css",
  ".js": "text/javascript",
  ".json": "application/json",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".svg": "image/svg+xml",
};

// Every HTTP request that hits this server runs this single function.
// `req` is what the browser sent us. `res` is how we answer.
function handleRequest(req, res) {
  // req.url is the path after localhost:3000 — e.g. "/" or "/style.css"
  let filePath = req.url === "/" ? "/index.html" : req.url;

  // Resolve to an absolute path inside public/
  const fullPath = path.join(__dirname, "public", filePath);

  // Security: block directory traversal ("../../etc/passwd")
  if (!fullPath.startsWith(path.join(__dirname, "public"))) {
    res.writeHead(403);
    res.end("Forbidden");
    return;
  }

  // API route — server generates data, not a file
  if (req.url === "/api/time") {
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ time: new Date().toISOString() }));
    return;
  }

  // Try to read the file from disk and send it back
  const ext = path.extname(fullPath);
  const contentType = MIME_TYPES[ext] || "application/octet-stream";

  fs.readFile(fullPath, (err, data) => {
    if (err) {
      res.writeHead(404, { "Content-Type": "text/html" });
      res.end("<h1>404 — Not Found</h1>");
      return;
    }
    res.writeHead(200, { "Content-Type": contentType });
    res.end(data);
  });
}

// Create the server and start listening.
// http.createServer returns a server object; .listen binds it to a port.
const server = http.createServer(handleRequest);

server.listen(PORT, () => {
  console.log(`Server running at http://localhost:${PORT}`);
});
