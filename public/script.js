// Home page — fetch posts and render the list, with search

const container = document.getElementById("posts");
const searchInput = document.getElementById("search");

async function loadPosts(query) {
  const url = query ? `/api/posts?q=${encodeURIComponent(query)}` : "/api/posts";
  const response = await fetch(url);
  const posts = await response.json();

  if (posts.length === 0) {
    container.innerHTML = `<p class="no-results">No posts found.</p>`;
    return;
  }

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

// Debounce: wait until the user stops typing for 250ms before searching.
// Without this, every keystroke would fire an API request.
let debounceTimer;
searchInput.addEventListener("input", () => {
  clearTimeout(debounceTimer);
  debounceTimer = setTimeout(() => {
    loadPosts(searchInput.value.trim());
  }, 250);
});

loadPosts();
