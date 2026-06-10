// This file runs in the BROWSER, not in Node.js.
// The browser downloaded it because index.html has <script src="/script.js">.

// ── Server Time (fetch from API) ──────────────────────────
const fetchBtn = document.getElementById("fetch-time");
const timeOutput = document.getElementById("time-output");

fetchBtn.addEventListener("click", async () => {
  // fetch() sends an HTTP GET to our Node.js server at /api/time.
  // The server handles it in the "if (req.url === '/api/time')" branch.
  const response = await fetch("/api/time");
  const data = await response.json();
  timeOutput.textContent = data.time;
});

// ── Counter (pure client-side) ────────────────────────────
let count = 0;
const countDisplay = document.getElementById("count");
const incrementBtn = document.getElementById("increment");
const decrementBtn = document.getElementById("decrement");

incrementBtn.addEventListener("click", () => {
  count++;
  countDisplay.textContent = count;
});

decrementBtn.addEventListener("click", () => {
  count--;
  countDisplay.textContent = count;
});
