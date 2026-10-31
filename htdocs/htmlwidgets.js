/*
 *  htmlwidgets.js
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Dashboard widget 'HTML' (widgettype 18) and the setup page 'HTML Widgets'.
 *  The snippets are stored in the table 'htmlwidgets', a widget references one
 *  by its options { "snippet": <id>, "params": { "<name>": "<value>" } }.
 *
 *  Template (kind 'html'):
 *    {{W1:0x01}}              value of the sensor
 *    {{W1:0x01.unit}}         field of the sensor: value, state, text, unit, title, valid, battery,
 *                             last, peakmax, peakmin, color, key (state e.g. motion of a motion sensor)
 *    {{W1:0x01|1}}            value with 1 decimal
 *    {{DZL:0x03 ? an : aus}}  'an' if the field (default value) is set (not 0 / false / empty)
 *    {{p.temp}}               parameter of the widget, a sensor key as parameter works like
 *                             the key itself ({{p.temp.unit}}, {{p.temp|1}}), else its text
 *    {{p.vl - p.rl|1}}        calculation: + - * / and ( ), numbers, refs (no eval, see htmlWidgetEval())
 *    {{p.temp > 25 ? warm : ok}}  comparison: > < >= <= == != (text compare with ==, != : {{p.x == 'auf' ? ..}})
 *    {{DZS:0x23.changed|ago}} 'vor 3 min'; fields age / changed are the seconds since the last update / change
 *    {{w.id}}                 unique id of the widget (prefix for own ids: id="{{w.id}}-temp")
 *  Actions (attributes, the key may be a parameter 'p.<name>'):
 *    data-toggle="DZL:0x03"              switch
 *    data-set="HMB:0x02" data-value="0"  brightness / position
 *    data-chart="W1:0x01"                chart dialog of the sensor
 *  Kind 'iframe': the code is the URL (placeholders work there too).
 *  The CSS of a snippet is nested into '.htmlw-<id>' (CSS nesting) and so applies to its widgets only.
 */

var htmlWidgets = {};                 // the snippets { "<id>": { name, kind, code, css, parameters } }
var htmlWidgetDeps = {};              // the sensors of the HTML widgets on the dashboard { "<widget key>": Set(<sensor key>) }
var htmlWidgetSelected = null;        // id of the snippet in the editor, 'new' for a new one
var htmlWidgetKnownIds = [];          // ids before storing a new snippet (to select it afterwards)
var htmlWidgetEditors = {};           // CodeMirror of the fields code, css and parameters { "<textarea id>": editor }

const htmlWidgetKeyRegex = /^([A-Za-z0-9_]+):(0x[0-9a-fA-F]+|[0-9]+)$/;

//***************************************************************************
// Snippets received
//***************************************************************************

function htmlWidgetsReceived(snippets)
{
   htmlWidgets = snippets || {};
   htmlWidgetStyles();

   if (htmlWidgetSelected == 'new') {         // select the snippet just stored
      for (let id in htmlWidgets) {
         if (!htmlWidgetKnownIds.includes(id))
            htmlWidgetSelected = id;
      }
   }
   else if (htmlWidgetSelected != null && !htmlWidgets[htmlWidgetSelected])
      htmlWidgetSelected = null;              // deleted

   if (currentPage == 'htmlwidgets')
      initHtmlWidgetsPage();
   else if (currentPage == 'dashboard' && dashboards[actDashboard] != null && Object.keys(allSensors).length)
      initDashboard(true);                    // not at the login, the dashboard follows with the sensors
}

// the style sheets of the snippets, one per snippet, nested into its class

function htmlWidgetStyles()
{
   $('style.htmlWidgetStyle').remove();

   for (let id in htmlWidgets) {
      if (!htmlWidgets[id].css || htmlWidgets[id].css.trim() == '')
         continue;

      let error = htmlWidgetCssError(htmlWidgets[id].css);

      if (error) {       // would leave the nesting and apply to the whole WEBIF
         console.log("HTML widget '" + htmlWidgets[id].name + "': CSS ignored, " + error);
         continue;
      }

      $('head').append($('<style></style>')
                       .addClass('htmlWidgetStyle')
                       .text('.htmlw-' + id + ' {\n' + htmlWidgets[id].css + '\n}'));
   }
}

// braces of the CSS balanced? (comments and strings skipped) -> null or the error

function htmlWidgetCssError(css)
{
   let depth = 0;
   let line = 1;

   for (let i = 0; i < css.length; i++) {
      let c = css[i];

      if (c == '\n')
         line++;
      else if (c == '/' && css[i+1] == '*') {
         let end = css.indexOf('*/', i + 2);
         if (end == -1)
            return 'Kommentar ab Zeile ' + line + ' nicht geschlossen';
         line += (css.substring(i, end).match(/\n/g) || []).length;
         i = end + 1;
      }
      else if (c == '"' || c == "'") {
         let end = i + 1;
         while (end < css.length && css[end] != c && css[end] != '\n')
            end += css[end] == '\\' ? 2 : 1;
         if (css[end] != c)
            return 'String in Zeile ' + line + ' nicht geschlossen';
         i = end;
      }
      else if (c == '{')
         depth++;
      else if (c == '}' && --depth < 0)
         return 'überzählige \'}\' in Zeile ' + line;
   }

   return depth > 0 ? (depth + ' \'}\' fehlen') : null;
}

// constructs of the template which affect the WEBIF -> list of warnings

function htmlWidgetWarnings(code)
{
   let warnings = [];

   if (/<style\b/i.test(code))
      warnings.push('<style> wirkt auf die ganze WEBIF, das CSS gehört ins Feld \'CSS\'');
   if (/<link\b/i.test(code))
      warnings.push('<link> lädt Styles für die ganze WEBIF');
   if (/<script\b/i.test(code))
      warnings.push('<script> wird nicht ausgeführt');
   if (/<[^>]*\son[a-z]+\s*=/i.test(code))
      warnings.push('on...= Handler laufen im globalen Scope der WEBIF, besser data-toggle / data-set / data-chart');

   for (const m of code.matchAll(/<[^>]*\sid\s*=\s*(["'])(.*?)\1/gi)) {
      if (!m[2].includes('{{w.id}}'))
         warnings.push('id="' + m[2] + '" ohne {{w.id}}: doppelt bei mehreren Widgets, kann mit IDs der WEBIF kollidieren');
   }

   return warnings;
}

//***************************************************************************
// Template
//***************************************************************************

function htmlWidgetEscape(text)
{
   return String(text)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

// the sensor key of a reference (key or 'p.<name>'), null if it is no sensor

function htmlWidgetKeyOf(ref, params)
{
   ref = (ref || '').trim();

   if (ref.startsWith('p.'))
      ref = String(params[ref.substring(2)] ?? '').trim();

   let m = ref.match(htmlWidgetKeyRegex);

   return m ? toKey(m[1], parseInt(m[2])) : null;
}

function htmlWidgetField(key, field, ctx)
{
   let sensor = allSensors[key];
   let fact = valueFacts[key];
   let now = Date.now() / 1000 - ((typeof daemonState != 'undefined' && daemonState.timeOffset) || 0);

   switch (field) {
      case 'key':     return key;
      case 'title':   return factTitle(key);
      case 'unit':    return fact ? fact.unit : '';
      case 'last':    return sensor && sensor.last ? new Date(sensor.last * 1000).toLocaleTimeString(dateLocale) : '';
      case 'age':                       // seconds since the last update resp. change
      case 'changed': {
         let at = sensor ? (field == 'age' ? sensor.last : sensor.changedAt) : null;
         ctx.usesTime = true;
         return at ? Math.max(0, Math.round(now - at)) : null;
      }
      default:        return sensor ? sensor[field] : null;
   }
}

// seconds -> 'vor 3 min'

function htmlWidgetAgo(seconds)
{
   seconds = Number(seconds);

   if (isNaN(seconds))
      return null;
   if (seconds < 60)
      return 'gerade eben';
   if (seconds < 3600)
      return 'vor ' + Math.floor(seconds / 60) + ' min';
   if (seconds < 86400)
      return 'vor ' + Math.floor(seconds / 3600) + ':' + String(Math.floor(seconds % 3600 / 60)).padStart(2, '0') + ' h';

   let days = Math.floor(seconds / 86400);
   return 'vor ' + days + (days == 1 ? ' Tag' : ' Tagen');
}

//***************************************************************************
// Expression of a placeholder (no eval):
//   expr    := sum [ ('>' | '<' | '>=' | '<=' | '==' | '!=') sum ]
//   sum     := term { ('+' | '-') term }
//   term    := factor { ('*' | '/') factor }
//   factor  := number | 'text' | "text" | ref | '(' expr ')' | '-' factor
//   ref     := TYPE:0xNN[.field] | p.<name>[.field]
//***************************************************************************

function htmlWidgetTokens(text)
{
   const rules = [
      ['ref',    /^p\.[A-Za-z_]\w*(?:\.\w+)?/],
      ['ref',    /^[A-Za-z0-9_]+:(?:0x[0-9a-fA-F]+|[0-9]+)(?:\.\w+)?/],
      ['number', /^[0-9]+(?:\.[0-9]+)?/],
      ['string', /^'[^']*'|^"[^"]*"/],
      ['op',     /^(?:>=|<=|==|!=|[<>+\-*\/()])/]
   ];
   let tokens = [];

   text = text.trim();

   while (text.length) {
      let m = null;

      for (const [type, regex] of rules) {
         if ((m = text.match(regex))) {
            tokens.push({ 'type': type, 'text': m[0] });
            break;
         }
      }

      if (!m)
         throw new Error('unexpected \'' + text + '\'');

      text = text.substring(m[0].length).trimStart();
   }

   return tokens;
}

function htmlWidgetNumber(value)
{
   if (value == null || value === '')
      return null;
   if (typeof value == 'boolean')
      return value ? 1 : 0;

   let n = Number(value);

   return isNaN(n) ? null : n;
}

function htmlWidgetEval(text, ctx)
{
   let tokens = htmlWidgetTokens(text);
   let pos = 0;

   const peek = () => tokens[pos] ? tokens[pos].text : null;

   function ref(name) {
      let field = null;
      let m = name.match(/^(p\.[A-Za-z_]\w*|[A-Za-z0-9_]+:(?:0x[0-9a-fA-F]+|[0-9]+))(?:\.(\w+))?$/);

      if (m) {
         name = m[1];
         field = m[2] || null;
      }

      let key = htmlWidgetKeyOf(name, ctx.params);

      if (key == null)                  // a parameter which is no sensor -> its text
         return field == null && name.startsWith('p.') ? (ctx.params[name.substring(2)] ?? '') : null;

      ctx.deps.add(key);

      return htmlWidgetField(key, field || 'value', ctx);
   }

   function factor() {
      let t = tokens[pos++];

      if (!t)
         throw new Error('incomplete');
      if (t.type == 'number')
         return Number(t.text);
      if (t.type == 'string')
         return t.text.slice(1, -1);
      if (t.type == 'ref')
         return ref(t.text);
      if (t.text == '(') {
         let v = expr();
         if (tokens[pos++]?.text != ')')
            throw new Error('missing )');
         return v;
      }
      if (t.text == '-') {
         ctx.computed = true;
         let v = htmlWidgetNumber(factor());
         return v == null ? null : -v;
      }

      throw new Error('unexpected \'' + t.text + '\'');
   }

   function term() {
      let v = factor();

      while (peek() == '*' || peek() == '/') {
         let op = tokens[pos++].text;
         let a = htmlWidgetNumber(v), b = htmlWidgetNumber(factor());
         ctx.computed = true;
         v = a == null || b == null || (op == '/' && b == 0) ? null : op == '*' ? a * b : a / b;
      }

      return v;
   }

   function sum() {
      let v = term();

      while (peek() == '+' || peek() == '-') {
         let op = tokens[pos++].text;
         let a = htmlWidgetNumber(v), b = htmlWidgetNumber(term());
         ctx.computed = true;
         v = a == null || b == null ? null : op == '+' ? a + b : a - b;
      }

      return v;
   }

   function expr() {
      let v = sum();

      if (['>', '<', '>=', '<=', '==', '!='].includes(peek())) {
         let op = tokens[pos++].text;
         let w = sum();
         let a = htmlWidgetNumber(v), b = htmlWidgetNumber(w);

         if (a == null || b == null) {       // not numeric -> text compare (== / != only)
            if (v == null || w == null)
               return false;
            a = String(v);
            b = String(w);
            if (op != '==' && op != '!=')
               return false;
         }

         switch (op) {
            case '>':  return a > b;
            case '<':  return a < b;
            case '>=': return a >= b;
            case '<=': return a <= b;
            case '==': return a == b;
            case '!=': return a != b;
         }
      }

      return v;
   }

   let value = expr();

   if (pos < tokens.length)
      throw new Error('unexpected \'' + tokens[pos].text + '\'');

   return value;
}

// one placeholder (without the braces) -> its text
//   expr [| digits | ago]    expr ? a : b

function htmlWidgetPlaceholder(expr, ctx)
{
   let original = expr;
   let cond = null;
   let format = null;

   if (expr == 'w.id')
      return ctx.wid;

   // condition 'expr ? a : b' (a key contains a ':' itself, therefore the '?' first)

   let q = expr.indexOf('?');

   if (q != -1) {
      let rest = expr.substring(q + 1);
      let c = rest.indexOf(':');
      cond = c != -1 ? [rest.substring(0, c).trim(), rest.substring(c + 1).trim()] : [rest.trim(), ''];
      expr = expr.substring(0, q).trim();
   }

   let p = expr.lastIndexOf('|');

   if (p != -1) {
      format = expr.substring(p + 1).trim();
      expr = expr.substring(0, p).trim();
   }

   let value;
   ctx.computed = false;

   try {
      value = htmlWidgetEval(expr, ctx);
   }
   catch (e) {
      return htmlWidgetEscape('{{' + original + '}}');     // not a valid expression -> as it is
   }

   if (cond) {
      let set = value != null && value !== false && value !== '' && value !== 0 && value !== '0';
      return htmlWidgetEscape(set ? cond[0] : cond[1]);
   }

   if (format == 'ago')
      value = htmlWidgetAgo(value);

   if (value == null)
      return '–';

   let digits = format != null && /^[0-9]+$/.test(format) ? parseInt(format) : null;

   if (digits != null && value !== '' && !isNaN(Number(value)))
      value = Number(value).toLocaleString(dateLocale, { minimumFractionDigits: digits, maximumFractionDigits: digits });
   else if (ctx.computed && typeof value == 'number' && !Number.isInteger(value))
      value = value.toLocaleString(dateLocale, { maximumFractionDigits: 2 });   // no float noise of a calculation

   return htmlWidgetEscape(value);
}

// the snippet with the params of the widget -> { html, deps }

function htmlWidgetRender(snippet, widgetParams, wid = 'hw')
{
   let params = Object.assign({}, snippet.parameters || {}, widgetParams || {});
   let ctx = { 'params': params, 'deps': new Set(), 'wid': wid, 'usesTime': false };
   let html = (snippet.code || '').replace(/\{\{([^{}]+)\}\}/g, function(all, expr) {
      return htmlWidgetPlaceholder(expr.trim(), ctx);
   });
   let deps = ctx.deps;

   if (snippet.kind == 'iframe')
      html = '<iframe src="' + html.trim() + '" class="widget-html-iframe" loading="lazy"></iframe>';

   return { 'html': html, 'deps': deps, 'params': params, 'usesTime': ctx.usesTime };
}

//***************************************************************************
// Content (dashboard and preview)
//***************************************************************************

// the unique id of a widget for {{w.id}} ('HTML:0x01' -> 'hw-HTML_0x01')

function htmlWidgetId(key)
{
   return 'hw-' + key.replace(/[^\w-]/g, '_');
}

function htmlWidgetContent($content, snippetId, widgetParams, wid)
{
   let snippet = htmlWidgets[snippetId];

   if (!snippet) {
      $content.html('<div class="widget-html-missing"><span class="mdi mdi-code-tags"></span>' +
                    '<div>' + (snippetId != null ? 'Snippet fehlt' : 'Kein Snippet gewählt') + '</div></div>');
      return new Set();
   }

   let result = htmlWidgetRender(snippet, widgetParams, wid);

   // only a changed content is set (keeps an iframe from reloading and the hover state)

   if ($content.data('html') !== result.html) {
      $content.data('html', result.html);
      $content.data('params', result.params);
      $content.html(result.html);
   }

   $content.data('usesTime', result.usesTime);

   return result.deps;
}

// widgets with relative times ('age', 'changed') age without a sensor update -> re-render every 30 seconds

setInterval(function() {
   if (currentPage == 'htmlwidgets') {
      htmlWidgetPreview();
      return;
   }

   if (currentPage != 'dashboard' || dashboards[actDashboard] == null)
      return;

   for (let key in htmlWidgetDeps) {
      let widget = dashboards[actDashboard].widgets[key];
      let $content = $('#widget' + key.replace(':', '\\:'));

      if (widget && widget.widgettype == 18 && $content.data('usesTime'))
         htmlWidgetDeps[key] = htmlWidgetContent($content, widget.snippet, widget.params, htmlWidgetId(key));
   }
}, 30 * 1000);

// actions of the content (delegated, survive re-rendering)

function htmlWidgetActions($content)
{
   $content.on('click', '[data-toggle], [data-set], [data-chart]', function(event) {
      if (setupMode)
         return;

      event.preventDefault();
      event.stopPropagation();

      let params = $content.data('params') || {};
      let $el = $(this);

      if ($el.is('[data-toggle]')) {
         let key = htmlWidgetKeyOf($el.attr('data-toggle'), params);
         if (key && valueFacts[key])
            toggleIo(valueFacts[key].address, valueFacts[key].type);
      }
      else if ($el.is('[data-set]')) {
         let key = htmlWidgetKeyOf($el.attr('data-set'), params);
         let value = parseInt($el.attr('data-value'));
         if (key && valueFacts[key] && !isNaN(value))
            toggleIo(valueFacts[key].address, valueFacts[key].type, value);
      }
      else if ($el.is('[data-chart]')) {
         let key = htmlWidgetKeyOf($el.attr('data-chart'), params);
         if (key && valueFacts[key])
            toggleChartDialog(valueFacts[key].type, valueFacts[key].address);
      }
   });
}

//***************************************************************************
// Dashboard Widget
//***************************************************************************

function initHtmlWidget(key, widget, elem, title, titleClass)
{
   let snippet = htmlWidgets[widget.snippet];

   if (setupMode && title.trim() == '')
      title = ' ' + (snippet ? snippet.name : 'HTML');

   $(elem).addClass('widget widgetHtml widgetDropZone');

   if (title.trim() != '' || setupMode)
      $(elem).append($('<div></div>')
                     .addClass('widget-title ' + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : ''))
                     .addClass(titleClass)
                     .css('user-select', 'none')
                     .click(function(event) {titleClick(event.ctrlKey, key);})
                     .html(title));

   let $content = $('<div></div>')
       .attr('id', 'widget' + key)
       .addClass('widget-html htmlw-' + widget.snippet);

   $(elem).append($content);

   if (snippet && snippet.kind == 'iframe' && setupMode)
      $content.css('pointer-events', 'none');      // the iframe would catch the drag & drop

   htmlWidgetDeps[key] = htmlWidgetContent($content, widget.snippet, widget.params, htmlWidgetId(key));
   htmlWidgetActions($content);

   // Ctrl+click on the whole widget opens the widget setup (also without title), before the
   // actions of the content (capture); the element survives a re-init, register only once

   if (!elem.htmlCtrlSetup) {
      elem.htmlCtrlSetup = true;
      elem.addEventListener('click', function(event) {
         if (!event.ctrlKey)
            return;
         event.preventDefault();
         event.stopPropagation();
         titleClick(true, key);
      }, true);
   }
}

// sensors changed -> the HTML widgets using them

function updateHtmlWidgets(changed)
{
   if (dashboards[actDashboard] == null)
      return;

   for (let key in htmlWidgetDeps) {
      let widget = dashboards[actDashboard].widgets[key];

      if (!widget || widget.widgettype != 18)
         continue;

      let uses = false;

      for (let sensorKey in changed) {
         if (htmlWidgetDeps[key].has(sensorKey)) {
            uses = true;
            break;
         }
      }

      if (uses)
         htmlWidgetDeps[key] = htmlWidgetContent($('#widget' + key.replace(':', '\\:')), widget.snippet, widget.params, htmlWidgetId(key));
   }
}

//***************************************************************************
// Widget Setup (dialog of the dashboard widget, widgetsetup.js)
//***************************************************************************

function htmlWidgetSetupRows(widget)
{
   let $select = $('<select></select>')
       .addClass('rounded-border inputSetting')
       .attr('id', 'htmlSnippet')
       .change(function() { htmlWidgetSetupParams(widget); })
       .append($('<option></option>').val('').html('- wählen -'));

   let ids = Object.keys(htmlWidgets).sort((a, b) => htmlWidgets[a].name.localeCompare(htmlWidgets[b].name));

   for (let id of ids)
      $select.append($('<option></option>')
                     .val(id)
                     .html(htmlWidgetEscape(htmlWidgets[id].name))
                     .attr('selected', widget.snippet == id));

   return $('<div></div>')
      .addClass('htmlWidgetSetup')
      .append($('<div></div>')
              .attr('id', 'divSnippet')
              .addClass('htmlWidgetSetupRow')
              .append($('<span></span>').html('Snippet'))
              .append($('<span></span>').append($select)))
      .append($('<div></div>')
              .attr('id', 'divSnippetParams'))
      .append($('<datalist></datalist>')
              .attr('id', 'htmlSensorKeys'));
}

// the inputs of the parameters of the selected snippet

function htmlWidgetSetupParams(widget)
{
   let snippet = htmlWidgets[$('#htmlSnippet').val()];
   let $div = $('#divSnippetParams').empty();

   if ($('#htmlSensorKeys').children().length == 0) {
      for (let key in valueFacts)
         $('#htmlSensorKeys').append($('<option></option>').val(key).html(htmlWidgetEscape(factTitle(key))));
   }

   if (!snippet)
      return;

   let params = widget.params || {};

   for (let name in snippet.parameters || {}) {
      $div.append($('<div></div>')
                  .addClass('htmlWidgetSetupRow')
                  .append($('<span></span>').text(name))
                  .append($('<span></span>')
                          .append($('<input></input>')
                                  .addClass('rounded-border inputSetting htmlParam')
                                  .attr('type', 'search')
                                  .attr('list', 'htmlSensorKeys')
                                  .attr('placeholder', snippet.parameters[name])
                                  .attr('title', 'Leer: Vorgabe des Snippets \'' + snippet.parameters[name] + '\'')
                                  .data('name', name)
                                  .val(params[name] ?? ''))));
   }
}

// the settings of the dialog into the widget (options)

function htmlWidgetSetupStore(widget)
{
   let id = $('#htmlSnippet').val();
   widget.snippet = id != '' ? parseInt(id) : null;
   widget.params = {};

   $('#divSnippetParams .htmlParam').each(function() {
      if ($(this).val().trim() != '')
         widget.params[$(this).data('name')] = $(this).val().trim();
   });
}

//***************************************************************************
// Setup Page 'HTML Widgets'
//***************************************************************************

async function initHtmlWidgetsPage()
{
   $('#container').removeClass('hidden');
   prepareSetupMenu();

   let ids = Object.keys(htmlWidgets).sort((a, b) => htmlWidgets[a].name.localeCompare(htmlWidgets[b].name));

   if (htmlWidgetSelected == null && ids.length)
      htmlWidgetSelected = ids[0];

   let snippet = htmlWidgetSelected == 'new' || htmlWidgetSelected == null
       ? { 'name': '', 'kind': 'html', 'code': '', 'css': '', 'parameters': {} }
       : htmlWidgets[htmlWidgetSelected];

   // list

   let $items = $('<div></div>').addClass('htmlWidgetListItems');
   let $list = $('<div></div>').addClass('htmlWidgetList')
       .append($('<div></div>').addClass('htmlWidgetListTitle').text('Snippets (' + ids.length + ')'))
       .append($items);

   if (!ids.length)
      $items.append($('<div></div>').addClass('htmlWidgetListEmpty').text('noch keine'));

   for (let id of ids) {
      $items.append($('<div></div>')
                   .addClass('htmlWidgetListItem' + (id == htmlWidgetSelected ? ' active' : ''))
                   .append($('<span></span>').addClass('mdi ' + (htmlWidgets[id].kind == 'iframe' ? 'mdi-web' : 'mdi-code-tags')))
                   .append($('<span></span>').text(' ' + htmlWidgets[id].name))
                   .click(function() { htmlWidgetSelected = id; initHtmlWidgetsPage(); }));
   }

   $list.append($('<div></div>').addClass('htmlWidgetListButtons')
                .append($('<button></button>')
                        .addClass('rounded-border button1')
                        .append($('<span></span>').addClass('mdi mdi-plus'))
                        .append(' Neu')
                        .attr('title', 'Neues Snippet')
                        .click(function() { htmlWidgetSelected = 'new'; initHtmlWidgetsPage(); }))
                .append($('<button></button>')
                        .addClass('rounded-border button1')
                        .append($('<span></span>').addClass('mdi mdi-import'))
                        .append(' Import')
                        .attr('title', 'Snippets aus einer JSON Datei (Export) importieren')
                        .click(function() { $('#hwImportFile').val('').click(); }))
                .append($('<button></button>')
                        .addClass('rounded-border button1')
                        .append($('<span></span>').addClass('mdi mdi-export'))
                        .append(' Export')
                        .attr('title', 'Alle Snippets als JSON Datei speichern')
                        .prop('disabled', !ids.length)
                        .click(function() { htmlWidgetExport(ids); }))
                .append($('<input></input>')
                        .attr('id', 'hwImportFile')
                        .attr('type', 'file')
                        .attr('accept', '.json,application/json')
                        .css('display', 'none')
                        .change(function() { if (this.files.length) htmlWidgetImportFile(this.files[0]); })));

   // editor

   let paramsText = Object.keys(snippet.parameters || {}).length ? JSON.stringify(snippet.parameters, undefined, 2) : '';

   let $editor = $('<div></div>').addClass('htmlWidgetEditor')
       .append($('<div></div>').addClass('htmlWidgetRow')
               .append($('<span></span>').text('Name'))
               .append($('<input></input>').attr('id', 'hwName').attr('type', 'text')
                       .addClass('rounded-border inputSetting').val(snippet.name)))
       .append($('<div></div>').addClass('htmlWidgetRow')
               .append($('<span></span>').text('Art'))
               .append($('<select></select>').attr('id', 'hwKind')
                       .addClass('rounded-border inputSetting')
                       .append($('<option></option>').val('html').text('HTML Template'))
                       .append($('<option></option>').val('iframe').text('iframe (URL)'))
                       .val(snippet.kind || 'html')
                       .change(function() { htmlWidgetPreview(); })))
       .append($('<div></div>').addClass('htmlWidgetRow')
               .append($('<span></span>').attr('id', 'hwCodeLabel').text('Code'))
               .append($('<textarea></textarea>').attr('id', 'hwCode').attr('spellcheck', false)
                       .attr('placeholder', '<div class="wert" data-chart="p.temp">{{p.temp|1}} {{p.temp.unit}}</div>')
                       .addClass('rounded-border htmlWidgetCode').val(snippet.code)))
       .append($('<div></div>').addClass('htmlWidgetRow')
               .append($('<span></span>').text('CSS'))
               .append($('<textarea></textarea>').attr('id', 'hwCss').attr('spellcheck', false)
                       .attr('placeholder', '.wert { font-size: 2em; }   (gilt nur für dieses Snippet)')
                       .addClass('rounded-border htmlWidgetCode htmlWidgetCss').val(snippet.css)))
       .append($('<div></div>').addClass('htmlWidgetRow')
               .append($('<span></span>').text('Parameter'))
               .append($('<textarea></textarea>').attr('id', 'hwParams').attr('spellcheck', false)
                       .attr('placeholder', '{ "temp": "W1:0x01", "raum": "Wohnzimmer" }')
                       .addClass('rounded-border htmlWidgetCode htmlWidgetParams').val(paramsText)))
       .append($('<div></div>').addClass('htmlWidgetRow')
               .append($('<span></span>'))
               .append($('<div></div>')
                       .append($('<div></div>').attr('id', 'hwError').addClass('htmlWidgetError'))
                       .append($('<div></div>').attr('id', 'hwWarn').addClass('htmlWidgetWarn'))))
       .append($('<div></div>').addClass('htmlWidgetButtons')
               .append($('<button></button>').addClass('rounded-border button1').text('Speichern')
                       .click(function() { storeHtmlWidget(); }))
               .append($('<button></button>').addClass('rounded-border button1').text('Duplizieren')
                       .prop('disabled', htmlWidgetSelected == 'new' || htmlWidgetSelected == null)
                       .click(function() { storeHtmlWidget(true); }))
               .append($('<button></button>').addClass('rounded-border button1').text('Export')
                       .attr('title', 'Dieses Snippet (gespeicherter Stand) als JSON Datei speichern')
                       .prop('disabled', htmlWidgetSelected == 'new' || htmlWidgetSelected == null)
                       .click(function() { htmlWidgetExport([htmlWidgetSelected]); }))
               .append($('<button></button>').addClass('rounded-border button1 htmlWidgetDelete').text('Löschen')
                       .prop('disabled', htmlWidgetSelected == 'new' || htmlWidgetSelected == null)
                       .click(function() { deleteHtmlWidget(); })))
       .append($('<div></div>').addClass('htmlWidgetHelp')
               .html('<b>Platzhalter:</b> <code>{{W1:0x01}}</code> Wert, <code>{{W1:0x01|1}}</code> eine Nachkommastelle, ' +
                     '<code>{{W1:0x01.unit}}</code> Feld (value, state, text, unit, title, valid, battery, last, age, changed, peakmax, peakmin; state z. B. Bewegung), ' +
                     '<code>{{DZL:0x03 ? an : aus}}</code> Bedingung, <code>{{p.temp}}</code> Parameter (ein Sensor-Key als Parameter wirkt wie der Key), ' +
                     '<code>{{w.id}}</code> eindeutige ID des Widgets für eigene IDs (<code>id="{{w.id}}-temp"</code>)<br>' +
                     '<b>Ausdrücke:</b> <code>{{p.vl - p.rl|1}}</code> rechnen (+ - * / und Klammern), ' +
                     '<code>{{p.temp > 25 ? warm : ok}}</code> vergleichen (&gt; &lt; &gt;= &lt;= == !=, Text in \'...\'), ' +
                     '<code>{{p.motion.changed|ago}}</code> \'vor 3 min\' (Felder age / changed: Sekunden seit Update / Änderung)<br>' +
                     '<b>Aktionen:</b> <code>data-toggle="DZL:0x03"</code> schalten, <code>data-set="HMB:0x02" data-value="0"</code> Helligkeit / Position, ' +
                     '<code>data-chart="W1:0x01"</code> Chart, statt des Keys auch <code>p.&lt;name&gt;</code><br>' +
                     'Die Parameter sind die Vorgaben, im Widget-Setup des Dashboards je Widget überschreibbar. ' +
                     'Die Farben des Themes als <code>var(--fontColor)</code>, <code>var(--green)</code>, ... und MDI Symbole als <code>&lt;span class="mdi mdi-lightbulb"&gt;</code>.'));

   // preview

   let $preview = $('<div></div>').addClass('htmlWidgetPreviewArea')
       .append($('<div></div>').addClass('htmlWidgetPreviewTitle').text('Vorschau'))
       .append($('<div></div>').addClass('widgetContainer htmlWidgetPreviewContainer')
               .append($('<div></div>').addClass('widget widgetHtml htmlWidgetPreviewWidget')
                       .append($('<div></div>').attr('id', 'hwPreview').addClass('widget-html htmlw-preview'))));

   let $page = $('<div></div>').addClass('rounded-border inputTableConfig htmlWidgetPage')
       .append($('<div></div>').addClass('htmlWidgetSide').append($preview).append($list))
       .append($('<div></div>').addClass('htmlWidgetMain').append($editor));

   $('#container').empty().append($page);

   htmlWidgetEditors = {};
   $('#hwCode, #hwCss, #hwParams').on('input', function() { htmlWidgetPreview(); });
   htmlWidgetActions($('#hwPreview'));
   htmlWidgetPreview();

   // CodeMirror (iosetup.js), without it the plain textareas stay

   let areas = { 'hwCode': 'htmltpl', 'hwCss': 'csssimple', 'hwParams': 'jsonsimple' };
   let elements = {};

   for (let id in areas)
      elements[id] = document.getElementById(id);

   if (!await loadCodeMirror() || !document.body.contains(elements.hwCode))
      return;                                // page left or rebuilt meanwhile

   try {
      await loadAssets(['lib/codemirror/placeholder.js']);    // the placeholder texts of the empty fields
   }
   catch (e) {
      console.log("Loading the CodeMirror placeholder addon failed: " + e.message);
   }

   if (!document.body.contains(elements.hwCode))
      return;

   defineHtmlWidgetModes();

   for (const id in areas) {
      let editor = CodeMirror.fromTextArea(elements[id], {
         mode:           areas[id],
         placeholder:    elements[id].getAttribute('placeholder') || '',
         lineNumbers:    true,
         indentUnit:     3,
         tabSize:        3,
         indentWithTabs: false,
         lineWrapping:   true,
         extraKeys:      { 'Ctrl-S': function() { storeHtmlWidget(); } }
      });

      editor.getWrapperElement().classList.add('htmlWidgetCm', id + 'Cm');
      editor.on('changes', function() { htmlWidgetPreview(); });

      // resizable like a textarea (css 'resize'), CodeMirror has to measure the new size

      new ResizeObserver(function() { editor.refresh(); }).observe(editor.getWrapperElement());
      htmlWidgetEditors[id] = editor;
   }
}

function htmlWidgetEditorValue(id)
{
   return htmlWidgetEditors[id] ? htmlWidgetEditors[id].getValue() : $('#' + id).val();
}

// the modes of the editors (CodeMirror simple mode, lib/codemirror/simple.js)

function defineHtmlWidgetModes()
{
   if (CodeMirror.modes.htmltpl)
      return;

   const placeholder = {regex: /\{\{[^{}]*\}\}/, token: 'variable-2'};

   CodeMirror.defineSimpleMode('htmltpl', {
      start: [
         {regex: /<!--/,                       token: 'comment',   next: 'comment'},
         placeholder,
         {regex: /<\/?[A-Za-z][\w-]*/,          token: 'tag',       next: 'tag'},
         {regex: /&[#\w]+;/,                   token: 'atom'},
         {regex: /[^<{&]+|[{&]/,               token: null}
      ],
      tag: [
         {regex: /\/?>/,                        token: 'tag',       next: 'start'},
         {regex: /data-(?:toggle|set|value|chart)\b/, token: 'keyword'},
         {regex: /[A-Za-z_:][\w:.-]*/,          token: 'attribute'},
         {regex: /=/,                           token: 'operator'},
         {regex: /"/,                           token: 'string',    next: 'dq'},
         {regex: /'/,                           token: 'string',    next: 'sq'},
         {regex: /\s+/,                         token: null}
      ],
      dq: [
         placeholder,
         {regex: /"/,                           token: 'string',    next: 'tag'},
         {regex: /[^"{]+|\{/,                   token: 'string'}
      ],
      sq: [
         placeholder,
         {regex: /'/,                           token: 'string',    next: 'tag'},
         {regex: /[^'{]+|\{/,                   token: 'string'}
      ],
      comment: [
         {regex: /.*?-->/,                      token: 'comment',   next: 'start'},
         {regex: /.*/,                          token: 'comment'}
      ]
   });

   CodeMirror.defineSimpleMode('csssimple', {
      start: [
         {regex: /\/\*/,                        token: 'comment',   next: 'comment'},
         {regex: /"(?:[^\\"]|\\.)*"?|'(?:[^\\']|\\.)*'?/, token: 'string'},
         {regex: /var\(--[\w-]+\)/,             token: 'variable-2'},
         {regex: /#[0-9a-fA-F]{3,8}\b/,         token: 'atom'},
         {regex: /-?[0-9]*\.?[0-9]+(?:px|em|rem|%|vh|vw|s|ms|deg)?\b/, token: 'number'},
         {regex: /[a-z-]+(?=\s*:(?![\w-]*\s*[{,]))/, token: 'property'},
         {regex: /[.#]?[A-Za-z_][\w-]*/,        token: 'tag'},
         {regex: /[{}();:,>+~*]/,              token: 'operator'}
      ],
      comment: [
         {regex: /.*?\*\//,                     token: 'comment',   next: 'start'},
         {regex: /.*/,                          token: 'comment'}
      ]
   });

   CodeMirror.defineSimpleMode('jsonsimple', {
      start: [
         {regex: /"(?:[^\\"]|\\.)*"(?=\s*:)/,   token: 'property'},
         {regex: /"(?:[^\\"]|\\.)*"?/,          token: 'string'},
         {regex: /true|false|null/,             token: 'atom'},
         {regex: /-?[0-9]+\.?[0-9]*(?:[eE][+-]?[0-9]+)?/, token: 'number'},
         {regex: /[{}\[\],:]/,                   token: 'operator'}
      ]
   });
}

// the snippet of the editor, null on an error (shown)

function htmlWidgetFromEditor()
{
   let snippet = {
      'name': $('#hwName').val().trim(),
      'kind': $('#hwKind').val(),
      'code': htmlWidgetEditorValue('hwCode'),
      'css': htmlWidgetEditorValue('hwCss'),
      'parameters': {}
   };

   let paramsText = htmlWidgetEditorValue('hwParams').trim();

   if (paramsText != '') {
      try {
         snippet.parameters = JSON.parse(paramsText);
         if (typeof snippet.parameters != 'object' || Array.isArray(snippet.parameters))
            throw new Error('kein Objekt { "name": "wert", ... }');
      }
      catch (e) {
         $('#hwError').text('Parameter: ' + e.message);
         return null;
      }
   }

   snippet.cssError = htmlWidgetCssError(snippet.css);
   $('#hwError').text(snippet.cssError ? 'CSS: ' + snippet.cssError : '');

   return snippet;
}

function htmlWidgetPreview()
{
   if (!$('#hwPreview').length)
      return;

   $('#hwCodeLabel').text($('#hwKind').val() == 'iframe' ? 'URL' : 'Code');

   if (htmlWidgetEditors.hwCode) {
      let placeholder = $('#hwKind').val() == 'iframe' ? 'http://grafana:3000/d/xyz?kiosk' : $('#hwCode').attr('placeholder');
      if (htmlWidgetEditors.hwCode.getOption('placeholder') != placeholder)
         htmlWidgetEditors.hwCode.setOption('placeholder', placeholder);
   }

   let snippet = htmlWidgetFromEditor();

   if (!snippet)
      return;

   $('#hwWarn').html(snippet.kind == 'html' ? htmlWidgetWarnings(snippet.code).map(htmlWidgetEscape).join('<br>') : '');
   $('style.htmlWidgetPreviewStyle').remove();

   if (snippet.css.trim() != '' && !snippet.cssError)
      $('head').append($('<style></style>')
                       .addClass('htmlWidgetPreviewStyle')
                       .text('.htmlw-preview {\n' + snippet.css + '\n}'));

   let result = htmlWidgetRender(snippet, {}, 'hw-preview');

   if ($('#hwPreview').data('html') !== result.html) {
      $('#hwPreview').data('html', result.html);
      $('#hwPreview').data('params', result.params);
      $('#hwPreview').html(result.html);
   }
}

function storeHtmlWidget(duplicate = false)
{
   let snippet = htmlWidgetFromEditor();

   if (!snippet)
      return;

   if (snippet.name == '') {
      $('#hwError').text('Der Name fehlt');
      $('#hwName').addClass('htmlWidgetMissing').focus()
         .one('input', function() { $(this).removeClass('htmlWidgetMissing'); $('#hwError').text(''); });
      return;
   }

   if (snippet.cssError) {
      showInfoDialog({ 'status': -1, 'message': 'Nicht gespeichert, CSS: ' + snippet.cssError });
      return;
   }

   let id = htmlWidgetSelected;

   if (duplicate || id == 'new' || id == null) {
      if (duplicate)
         snippet.name += ' (Kopie)';
      htmlWidgetKnownIds = Object.keys(htmlWidgets);
      htmlWidgetSelected = 'new';
      id = -1;
   }

   socket.send({ 'event': 'storehtmlwidgets', 'object': {
      'action': 'store',
      'id': parseInt(id),
      'name': snippet.name,
      'kind': snippet.kind,
      'code': snippet.code,
      'css': snippet.css,
      'parameters': Object.keys(snippet.parameters).length ? JSON.stringify(snippet.parameters) : ''
   }});
}

function deleteHtmlWidget()
{
   let id = htmlWidgetSelected;
   let snippet = htmlWidgets[id];

   if (!snippet)
      return;

   confirmDialog(function() {
      htmlWidgetSelected = null;
      socket.send({ 'event': 'storehtmlwidgets', 'object': { 'action': 'delete', 'id': parseInt(id) } });
   }, '\'' + snippet.name + '\' wirklich löschen? Die Widgets mit diesem Snippet bleiben leer.', 'Löschen', 'Abbrechen');
}

//***************************************************************************
// Export / Import
//   file: { "type": "homectld-htmlwidgets", "version": 1, "exported": "<iso time>",
//           "snippets": [ { "name", "kind", "code", "css", "parameters": {...} }, ... ] }
//***************************************************************************

function htmlWidgetExport(ids)
{
   let snippets = [];

   for (let id of ids) {
      let s = htmlWidgets[id];
      if (s)
         snippets.push({ 'name': s.name, 'kind': s.kind, 'code': s.code, 'css': s.css, 'parameters': s.parameters || {} });
   }

   if (!snippets.length)
      return;

   let data = { 'type': 'homectld-htmlwidgets', 'version': 1, 'exported': new Date().toISOString(), 'snippets': snippets };
   let fileName = snippets.length == 1
       ? 'htmlwidget-' + snippets[0].name.replace(/[^\w\-äöüÄÖÜß]+/g, '_') + '.json'
       : 'htmlwidgets-' + new Date().toISOString().substring(0, 10) + '.json';

   let url = URL.createObjectURL(new Blob([JSON.stringify(data, undefined, 2)], { type: 'application/json' }));
   let a = document.createElement('a');
   a.href = url;
   a.download = fileName;
   document.body.appendChild(a);
   a.click();
   a.remove();
   setTimeout(function() { URL.revokeObjectURL(url); }, 1000);
}

function htmlWidgetImportFile(file)
{
   let reader = new FileReader();

   reader.onload = function() {
      let snippets = null;

      try {
         let data = JSON.parse(reader.result);
         snippets = Array.isArray(data) ? data : data.snippets;

         if (!Array.isArray(snippets))
            throw new Error('keine Snippets gefunden');

         snippets = snippets.filter(s => s && typeof s == 'object' && typeof s.name == 'string' && s.name.trim() != '');

         if (!snippets.length)
            throw new Error('keine Snippets mit Namen gefunden');
      }
      catch (e) {
         showInfoDialog({ 'status': -1, 'message': 'Import von \'' + file.name + '\' fehlgeschlagen: ' + e.message });
         return;
      }

      htmlWidgetImport(snippets);
   };

   reader.readAsText(file);
}

// existing names -> ask: overwrite or as copy

function htmlWidgetImport(snippets)
{
   let byName = {};

   for (let id in htmlWidgets)
      byName[htmlWidgets[id].name] = id;

   let conflicts = [...new Set(snippets.filter(s => byName[s.name.trim()] != null).map(s => s.name.trim()))];

   if (!conflicts.length) {
      htmlWidgetImportSend(snippets, byName, 'copy');
      return;
   }

   let form = document.createElement('div');

   $(form)
      .append($('<div></div>').text(snippets.length + ' Snippet(s) in der Datei, diese gibt es schon:'))
      .append($('<ul></ul>').append(conflicts.map(n => $('<li></li>').text(n))))
      .append($('<div></div>')
              .html('<b>Überschreiben</b>: die vorhandenen werden ersetzt, ihre Widgets zeigen sofort den neuen Stand<br>' +
                    '<b>Als Kopie</b>: sie werden mit neuem Namen angelegt, z. B. \'' + htmlWidgetEscape(conflicts[0]) + ' (2)\''));

   $(form).dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      width: 'auto',
      title: 'Snippets importieren',
      buttons: {
         'Abbrechen': function() { $(this).dialog('close'); },
         'Als Kopie': function() { htmlWidgetImportSend(snippets, byName, 'copy'); $(this).dialog('close'); },
         'Überschreiben': function() { htmlWidgetImportSend(snippets, byName, 'overwrite'); $(this).dialog('close'); }
      },
      close: function() { $(this).dialog('destroy').remove(); }
   });
}

function htmlWidgetImportSend(snippets, byName, mode)
{
   let taken = new Set(Object.keys(byName));
   let list = [];

   for (let s of snippets) {
      let name = s.name.trim();
      let id = -1;

      if (byName[name] != null && mode == 'overwrite')
         id = parseInt(byName[name]);
      else if (taken.has(name)) {
         let n = 2;
         while (taken.has(name + ' (' + n + ')'))
            n++;
         name = name + ' (' + n + ')';
      }

      taken.add(name);

      let parameters = s.parameters;

      if (parameters && typeof parameters == 'object')
         parameters = Object.keys(parameters).length ? JSON.stringify(parameters) : '';

      list.push({ 'id': id, 'name': name, 'kind': s.kind || 'html', 'code': s.code || '', 'css': s.css || '',
                  'parameters': typeof parameters == 'string' ? parameters : '' });
   }

   socket.send({ 'event': 'storehtmlwidgets', 'object': { 'action': 'import', 'snippets': list } });
}
