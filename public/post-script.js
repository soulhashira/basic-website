// Single post page — extract the slug from the URL, fetch the post, render it

async function loadPost() {
  const container = document.getElementById("post");

  // URL is /post/hello-world — split to get the slug
  const slug = window.location.pathname.split("/post/")[1];

  const response = await fetch(`/api/posts/${slug}`);

  if (!response.ok) {
    container.innerHTML = `
      <h1 class="post-title">Post not found</h1>
      <a href="/" class="back-link">&larr; Back</a>
    `;
    return;
  }

  const post = await response.json();

  // Update the browser tab title
  document.title = post.title;

  // Convert plain text line breaks into <p> tags
  const bodyHtml = post.body
    .split("\n\n")
    .map((p) => `<p>${p}</p>`)
    .join("");

  // Apply the post's chosen font to the body text
  const font = post.font || "Inter";
  const fontStyle = `font-family: "${font}", sans-serif;`;

  container.innerHTML = `
    <div class="post-date">${formatDate(post.date)}</div>
    <h1 class="post-title">${post.title}</h1>
    <div class="post-body" style="${fontStyle}">${bodyHtml}</div>
    <div class="post-actions">
      <a href="/" class="back-link">&larr; Back</a>
      <button id="delete-btn" class="btn-delete">Delete</button>
    </div>
  `;

  document.getElementById("delete-btn").addEventListener("click", async () => {
    if (!confirm("Delete this post?")) return;

    const res = await fetch(`/api/posts/${slug}`, { method: "DELETE" });
    if (res.ok) {
      window.location.href = "/";
    }
  });
}

function formatDate(dateStr) {
  const date = new Date(dateStr + "T00:00:00");
  return date.toLocaleDateString("en-US", {
    year: "numeric",
    month: "long",
    day: "numeric",
  });
}

loadPost();
