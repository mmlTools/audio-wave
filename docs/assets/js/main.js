document.addEventListener('DOMContentLoaded', () => {
    const links = document.querySelectorAll('a[href^="#"]');

    links.forEach(link => {
        link.addEventListener('click', event => {
            const href = link.getAttribute('href');
            const targetId = href.substring(1);

            if (!targetId) {
                return;
            }

            const targetEl = document.getElementById(targetId);
            if (!targetEl) {
                return;
            }

            event.preventDefault();

            targetEl.scrollIntoView({
                behavior: 'smooth',
                block: 'start'
            });

            history.pushState(null, '', href);
        });
    });
});
