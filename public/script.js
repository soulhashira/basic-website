// Home page — fetch all posts and render the list

async function loadPosts() {
  const container = document.getElementById("posts");
  const response = await fetch("/api/posts");
  const posts = await response.json();

  container.innerHTML = posts
    .map(
      (post) => `
      <div class="post-item">
        <div class="post-date">${formatDate(post.date)}</div>
        <h2 class="post-title"><a href="/post/${post.slug}">${post.title}</a></h2>
        <p class="post-excerpt">${post.excerpt}</p>
      </div>
    `
    )
    .join("");
}

function formatDate(dateStr) {
  const date = new Date(dateStr + "T00:00:00");
  return date.toLocaleDateString("en-US", {
    year: "numeric",
    month: "long",
    day: "numeric",
  });
}

loadPosts();
