
function initReadme()
{
   $('#container').removeClass('hidden');
   $('#container').html('<div id="systemContainer"></div>');

   prepareSetupMenu();

   $('#systemContainer')
      .addClass('setupContainer')
      .append($('<article></article>')
              .attr('id', 'readme')
              .addClass('markdown-body'));

   loadAndRenderReadme();
}

// Holt die README.md und rendert sie im HTML-Container

async function loadAndRenderReadme()
{
    const container = document.getElementById('readme');

    try {
        const response = await fetch('README.md');

        if (!response.ok) {
            throw new Error(`Server lieferte HTTP-Status ${response.status}`);
        }

        const markdownText = await response.text();
        container.innerHTML = window.marked.parse(markdownText);

    } catch (error) {
        console.error('Fehler beim Laden des README.md:', error);
        container.style.display = "block";
        container.innerHTML = `
            <div style="color: #cf222e; padding: 20px; background: #ffebe9; border-radius: 6px; border: 1px solid rgba(207,34,46,0.15);">
                <strong>Fehler beim Laden der Dokumentation:</strong> ${error.message}
            </div>`;
    }
}
