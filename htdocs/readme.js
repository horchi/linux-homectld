
// anchor (optional): id of an element in the README (e.g. <a id="hasp-panel"></a>) to scroll to

function initReadme(anchor = null)
{
   $('#container').removeClass('hidden');
   $('#container').html('<div id="systemContainer"></div>');

   prepareSetupMenu();

   $('#systemContainer')
      .addClass('setupContainer')
      .append($('<article></article>')
              .attr('id', 'readme')
              .addClass('markdown-body'));

   loadAndRenderReadme(anchor);
}

// Holt die README.md und rendert sie im HTML-Container

async function loadAndRenderReadme(anchor = null)
{
    const container = document.getElementById('readme');

    try {
        // the daemon sends cache-control max-age=3600, always fetch the current file
        const response = await fetch('README.md', { cache: 'no-store' });

        if (!response.ok) {
            throw new Error(`Server lieferte HTTP-Status ${response.status}`);
        }

        const markdownText = await response.text();
        container.innerHTML = window.marked.parse(markdownText);

        // give the headings ids (slug of the text) so links and the help buttons can jump to them

        container.querySelectorAll('h1, h2, h3, h4').forEach(function(h) {
            if (!h.id)
                h.id = h.textContent.trim().toLowerCase().replace(/[^a-z0-9äöüß]+/g, '-').replace(/^-+|-+$/g, '');
        });

        if (anchor) {
            const target = document.getElementById(anchor);
            if (target)
                target.scrollIntoView({ behavior: 'smooth', block: 'start' });
            else
                console.log('README anchor not found: ' + anchor);
        }

    } catch (error) {
        console.error('Fehler beim Laden des README.md:', error);
        container.style.display = "block";
        container.innerHTML = `
            <div style="color: #cf222e; padding: 20px; background: #ffebe9; border-radius: 6px; border: 1px solid rgba(207,34,46,0.15);">
                <strong>Fehler beim Laden der Dokumentation:</strong> ${error.message}
            </div>`;
    }
}
