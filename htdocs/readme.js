// README viewer of the WEBIF: README.md (overview), the chapters in doc/*.md and all other files of the
//   repository linked by them (markdown is rendered, other files are shown as text), see readmeUrl()
//
// target (optional): '<file>#<anchor>', '<file>' or '<anchor>' (of README.md),
//   e.g. 'doc/gps.md#gps-tours'; the anchor is the id of an element (e.g. <a id="hasp-panel"></a>)
//   or of a heading (slug of its text) to scroll to

const readmeGitHub = 'https://github.com/horchi/linux-homectld/blob/master/';
var readmeFile = 'README.md';

// URL of a file of the repository (path relative to the source tree): README.md and doc/ are linked directly
//   in htdocs, all other markdown files and the files they link in htdocs/repo/ (genrepo.sh)

function readmeUrl(path)
{
   if (path == 'README.md' || path.startsWith('doc/'))
      return path;

   return 'repo/' + path;
}

function initReadme(target = null)
{
   $('#container').removeClass('hidden');
   $('#container').html('<div id="systemContainer"></div>');

   prepareSetupMenu();
   setupContainerHeight();

   $('#systemContainer')
      .addClass('setupContainer')
      .append($('<article></article>')
              .attr('id', 'readme')
              .addClass('markdown-body'));

   let file = 'README.md';
   let anchor = null;

   if (target) {
      let [path, hash] = target.split('#');

      if (path.includes('.'))
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
        const response = await fetch(readmeUrl(file), { cache: 'no-store' });

        if (!response.ok) {
            throw new Error(`Server lieferte HTTP-Status ${response.status} für '${file}'`);
        }

        const text = await response.text();

        if (file.endsWith('.md'))
            container.innerHTML = window.marked.parse(text);
        else {                                         // other files (e.g. udev rules) as text
            container.innerHTML = '<h1></h1><pre><code></code></pre>';
            container.querySelector('h1').textContent = file;
            container.querySelector('code').textContent = text;
        }

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
                <strong>Fehler beim Laden der Dokumentation:</strong> ${error.message}<br>
                <a href="${readmeGitHub + file}" target="_blank">${file} auf GitHub öffnen</a>
            </div>`;
    }
}

// images and links of the markdown: files of the repository are opened in the viewer (images full size in a
//   new tab), anchors scroll in the page; absolute links (/...) are opened at GitHub

function readmeLinks(container, file)
{
    // images relative to the markdown file (e.g. 'img/x.jpg' in doc/*.md)

    container.querySelectorAll('img[src]').forEach(function(img) {
        const src = img.getAttribute('src');

        if (!/^[a-z]+:/i.test(src) && !src.startsWith('/')) {
            const url = new URL(src, 'http://x/' + file);
            img.setAttribute('src', readmeUrl(decodeURIComponent(url.pathname.substring(1))) + url.search);
        }
    });

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

        if (href.startsWith('/')) {                                    // absolute -> at GitHub
            a.setAttribute('href', readmeGitHub + path + url.hash);
            a.setAttribute('target', '_blank');
        }
        else if (/\.(jpg|jpeg|png|gif|svg)$/i.test(path)) {         // images (screenshots) -> full size
            a.setAttribute('href', readmeUrl(path));
            a.setAttribute('target', '_blank');
        }
        else {
            a.addEventListener('click', function(event) {
                event.preventDefault();
                loadAndRenderReadme(path, anchor);
            });
        }
    });
}
