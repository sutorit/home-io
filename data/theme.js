// Theme toggle functionality
const checkbox = document.getElementById('theme-toggle-checkbox');
const body = document.body;

document.addEventListener('DOMContentLoaded', () => {
    // Get saved theme or default to dark
    const savedTheme = localStorage.getItem('theme') || 'dark';
    if (savedTheme === 'light') {
        body.classList.add('light-mode');
        if (checkbox) checkbox.checked = true;
    }
});

if (checkbox) {
    checkbox.addEventListener('change', () => {
        if (checkbox.checked) {
            body.classList.add('light-mode');
            localStorage.setItem('theme', 'light');
        } else {
            body.classList.remove('light-mode');
            localStorage.setItem('theme', 'dark');
        }
    });
}
