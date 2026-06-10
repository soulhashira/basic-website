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

  container.innerHTML = `
    <div class="post-date">${formatDate(post.date)}</div>
    <h1 class="post-title">${post.title}</h1>
    <div class="post-body">${bodyHtml}</div>
    <a href="/" class="back-link">&larr; Back</a>
  `;
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
