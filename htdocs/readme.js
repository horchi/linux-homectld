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

        readmeHeadingIds(container);
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

// give the headings ids (slug of the text) so links and the help buttons can jump to them

function readmeHeadingIds(container)
{
    container.querySelectorAll('h1, h2, h3, h4').forEach(function(h) {
        if (!h.id)
            h.id = h.textContent.trim().toLowerCase().replace(/[^a-z0-9äöüß]+/g, '-').replace(/^-+|-+$/g, '');
    });
}

// images and links of the markdown: files of the repository are opened in the viewer (images full size in a
//   new tab), anchors scroll in the page; absolute links (/...) are opened at GitHub
//   openFile(path, anchor): how a linked file of the repository is opened (the help dialog opens the viewer)

function readmeLinks(container, file, openFile = loadAndRenderReadme)
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
                openFile(path, anchor);
            });
        }
    });
}

// the section of a rendered markdown starting at the anchor (an id of a heading or an <a id="..."> in front of it):
//   the heading and everything up to the next heading of the same or a higher level, of a h1 only the
//   introduction up to the next heading; null if the anchor isn't found

function readmeSection(container, anchor)
{
    let node = anchor ? Array.from(container.querySelectorAll('[id]')).find(function(e) { return e.id == anchor; }) : null;

    if (!node)
        return null;

    while (node.parentNode != container)              // <a id> is wrapped in a <p>
        node = node.parentNode;

    while (node && !/^H[1-6]$/.test(node.nodeName))   // the heading following the <a id>
        node = node.nextElementSibling;

    if (!node)
        return null;

    const level = parseInt(node.nodeName[1]);
    let section = [node];

    for (let n = node.nextElementSibling; n; n = n.nextElementSibling) {
        if (/^H[1-6]$/.test(n.nodeName) && (level == 1 || parseInt(n.nodeName[1]) <= level))
            break;
        section.push(n);
    }

    return section;
}

// the explanation of a setting in a dialog: the section of the help page 'target' ('<file>#<anchor>'),
//   the paragraphs / list entries naming one of 'terms' (name and title of the setting) are highlighted

async function showHelpDialog(target, title, terms = [])
{
    let [file, anchor] = target.split('#');
    let box = $('<div></div>').addClass('markdown-body helpDialog');

    try {
        const response = await fetch(readmeUrl(file), { cache: 'no-store' });

        if (!response.ok)
            throw new Error(`Server lieferte HTTP-Status ${response.status} für '${file}'`);

        let doc = document.createElement('div');
        doc.innerHTML = window.marked.parse(await response.text());
        readmeHeadingIds(doc);

        let section = readmeSection(doc, anchor) || Array.from(doc.children);
        section.forEach(function(n) { box.append(n); });

        readmeLinks(box[0], file, function(path, anchor) {
            box.dialog('close');
            showHelp(anchor ? path + '#' + anchor : path);
        });

        terms = terms.filter(function(t) { return t && t.length >= 3; })
           .map(function(t) { return t.toLowerCase(); });

        // first the entries naming a term as code (`interval`, exact), else the ones containing it in the text
        //   (short names like 'mail' are part of many entries)

        let candidates = box.find('p, li, td').toArray();

        let matches = candidates.filter(function(el) {
           return $(el).find('code').toArray().some(function(c) { return terms.includes(c.textContent.trim().toLowerCase()); });
        });

        if (!matches.length)
           matches = candidates.filter(function(el) {
              let text = el.textContent.toLowerCase().replace(/\s+/g, ' ');
              return terms.some(function(t) { return text.includes(t); });
           });

        // only the innermost (a list entry, not the list entry around its sub list)

        matches.filter(function(el) { return !matches.some(function(o) { return o !== el && el.contains(o); }); })
           .forEach(function(el) { $(el).addClass('helpMatch'); });
    }
    catch (error) {
        console.error('Fehler beim Laden von ' + file + ':', error);
        box.text('Fehler beim Laden der Dokumentation: ' + error.message);
    }

    box.dialog({
        modal: true,
        resizable: false,
        closeOnEscape: true,
        hide: 'fade',
        width: Math.min(900, window.innerWidth * 0.95),
        maxHeight: window.innerHeight * 0.85,
        title: title,
        open: function() {
            let first = box.find('.helpMatch').first();
            if (first.length)
                box.scrollTop(first.position().top + box.scrollTop() - 40);
        },
        buttons: {
            'In der Hilfe öffnen': function() {
                $(this).dialog('close');
                showHelp(target);
            },
            'Schließen': function() {
                $(this).dialog('close');
            }
        },
        close: function() {
            $(this).dialog('destroy').remove();
        }
    });
}
