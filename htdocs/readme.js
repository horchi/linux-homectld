// README viewer of the WEBIF: README.md (overview) and the chapters in doc/*.md
//
// target (optional): '<file>#<anchor>', '<file>' or '<anchor>' (of README.md),
//   e.g. 'doc/gps.md#gps-tours'; the anchor is the id of an element (e.g. <a id="hasp-panel"></a>)
//   or of a heading (slug of its text) to scroll to

const readmeGitHub = 'https://github.com/horchi/linux-homectld/blob/master/';
var readmeFile = 'README.md';

function initReadme(target = null)
{
   $('#container').removeClass('hidden');
   $('#container').html('<div id="systemContainer"></div>');

   prepareSetupMenu();

   $('#systemContainer')
      .addClass('setupContainer')
      .append($('<article></article>')
              .attr('id', 'readme')
              .addClass('markdown-body'));

   let file = 'README.md';
   let anchor = null;

   if (target) {
      let [path, hash] = target.split('#');

      if (path.endsWith('.md'))
         file = path;
      else
         hash = path;

      anchor = hash || null;
   }

   loadAndRenderReadme(file, anchor);
}

// Holt die Markdown Datei und rendert sie im HTML-Container

async function loadAndRenderReadme(file = 'README.md', anchor = null)
{
    const container = document.getElementById('readme');

    if (!container)
        return;

    try {
        // the daemon sends cache-control max-age=3600, always fetch the current file
        const response = await fetch(file, { cache: 'no-store' });

        if (!response.ok) {
            throw new Error(`Server lieferte HTTP-Status ${response.status} für '${file}'`);
        }

        const markdownText = await response.text();
        container.innerHTML = window.marked.parse(markdownText);
        readmeFile = file;

        // give the headings ids (slug of the text) so links and the help buttons can jump to them

        container.querySelectorAll('h1, h2, h3, h4').forEach(function(h) {
            if (!h.id)
                h.id = h.textContent.trim().toLowerCase().replace(/[^a-z0-9äöüß]+/g, '-').replace(/^-+|-+$/g, '');
        });

        readmeLinks(container, file);

        if (anchor) {
            const target = document.getElementById(anchor);
            if (target)
                target.scrollIntoView({ behavior: 'smooth', block: 'start' });
            else
                console.log('README anchor not found: ' + anchor + ' in ' + file);
        }
        else
            container.scrollIntoView({ block: 'start' });

    } catch (error) {
        console.error('Fehler beim Laden von ' + file + ':', error);
        container.style.display = "block";
        container.innerHTML = `
            <div style="color: #cf222e; padding: 20px; background: #ffebe9; border-radius: 6px; border: 1px solid rgba(207,34,46,0.15);">
                <strong>Fehler beim Laden der Dokumentation:</strong> ${error.message}
            </div>`;
    }
}

// links of the markdown: README.md and doc/*.md are opened in the viewer, anchors scroll in the page,
//   other files of the project (e.g. tpms/README.md, not part of the WEBIF) are opened at GitHub

function readmeLinks(container, file)
{
    container.querySelectorAll('a[href]').forEach(function(a) {
        const href = a.getAttribute('href');

        if (/^[a-z]+:/i.test(href))                     // http:, https:, mailto:, ...
            return;

        if (href.startsWith('#')) {
            a.addEventListener('click', function(event) {
                event.preventDefault();
                const target = document.getElementById(decodeURIComponent(href.substring(1)));
                if (target)
                    target.scrollIntoView({ behavior: 'smooth', block: 'start' });
            });
            return;
        }

        // resolve relative to the current file ('doc/gps.md' + '../README.md' -> 'README.md')

        const url = new URL(href, 'http://x/' + file);
        const path = decodeURIComponent(url.pathname.substring(1));
        const anchor = url.hash ? decodeURIComponent(url.hash.substring(1)) : null;

        if (path == 'README.md' || /^doc\/[^\/]+\.md$/.test(path)) {
            a.addEventListener('click', function(event) {
                event.preventDefault();
                loadAndRenderReadme(path, anchor);
            });
        }
        else {
            a.setAttribute('href', readmeGitHub + path + url.hash);
            a.setAttribute('target', '_blank');
        }
    });
}
