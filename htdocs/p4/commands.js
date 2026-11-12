/*
 *  p4/commands.js
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  The tab 'Kommandos' of the page of the Fröling S 3200 (p4/page.js): a test platform for the command
 *  topic (<base>/cmd) of the service p4mqttd. The request is typed as JSON and sent as it is, the answer
 *  of the topic <base>/reply is shown as JSON and prepared (see p4service.c, performP4Raw()).
 *  For the beginning the commands 'parget' and 'parset'.
 *
 *  The homectld only sets the 'rid' (to assign the answer) and, for 'parset', the 'user' who is logged in.
 *  Writing needs the right 'settings' - the value is written to the heating control at once!
 *
 *  The addresses which come into question (the parameters of the service menu) are delivered by the
 *  command 'parlist' of the service, the selected one is put into the JSON of the request by a button.
 */

var p4CmdRequest = '{\n  "cmd": "parget",\n  "address": 0\n}';    // the request as it is typed
var p4CmdResult = null;       // the last answer { request, reply, duration } or { error }
var p4CmdPars = null;         // the parameters of the service menu (parlist)
var p4CmdParsError = '';
var p4CmdFilter = '';
var p4CmdSelected = null;     // the selected address of the list
var p4CmdEditor = null;       // CodeMirror of the request (without it the plain textarea stays)

p4RegisterTab({ id: 'cmd', title: 'Kommandos', show: p4ShowCommands });

function p4ShowCommands()
{
   p4CmdRender();

   if (!p4CmdPars)
      p4CmdLoadPars();
}

function p4CmdHex(address)
{
   return '0x' + (address >>> 0).toString(16);
}

function p4CmdSend(text, tag)
{
   socket.send({ 'event': 'p4raw', 'object': { 'json': text, 'tag': tag } });
}

function p4CmdLoadPars()
{
   p4CmdParsError = '';
   p4CmdSend('{"cmd":"parlist"}', 'list');
}

// the text of the request (CodeMirror or the plain textarea)

function p4CmdRequestText()
{
   return p4CmdEditor ? p4CmdEditor.getValue() : $('#p4CmdRequest').val();
}

// CodeMirror with the JSON highlighting of the HTML widgets (iosetup.js, htmlwidgets.js) for the request

async function p4CmdInitEditor()
{
   let area = document.getElementById('p4CmdRequest');

   if (typeof loadCodeMirror != 'function' || typeof defineHtmlWidgetModes != 'function')
      return;

   if (!await loadCodeMirror() || !document.body.contains(area))
      return;                                // page left or rebuilt meanwhile

   defineHtmlWidgetModes();                  // 'jsonsimple'

   p4CmdEditor = CodeMirror.fromTextArea(area, {
      mode:           'jsonsimple',
      lineNumbers:    true,
      indentUnit:     3,
      tabSize:        3,
      indentWithTabs: false,
      lineWrapping:   true,
      extraKeys:      { 'Ctrl-Enter': function() { p4CmdSubmit(); } }
   });

   let editor = p4CmdEditor;

   editor.getWrapperElement().classList.add('htmlWidgetCm', 'p4CmdCm');
   editor.on('changes', function() { p4CmdRequest = editor.getValue(); });

   // resizable like a textarea (css 'resize'), CodeMirror has to measure the new size

   new ResizeObserver(function() { editor.refresh(); }).observe(editor.getWrapperElement());
   editor.refresh();
}

// the request as object, null (and the error is shown) if it isn't JSON

function p4CmdParse()
{
   p4CmdRequest = p4CmdRequestText();

   try {
      let obj = JSON.parse(p4CmdRequest);

      if (obj == null || typeof obj != 'object' || Array.isArray(obj))
         throw new Error('kein Objekt');

      return obj;
   }
   catch (e) {
      p4CmdResult = { 'error': 'Die Anfrage ist kein gültiges JSON Objekt (' + e.message + ')' };
      p4CmdRenderResult();
      return null;
   }
}

function p4CmdSetRequest(obj)
{
   p4CmdRequest = JSON.stringify(obj, null, 2);

   if (p4CmdEditor)
      p4CmdEditor.setValue(p4CmdRequest);
   else
      $('#p4CmdRequest').val(p4CmdRequest);
}

// a template of a command, the address of the request so far is kept

function p4CmdTemplate(cmd)
{
   let address = p4CmdSelected != null ? p4CmdSelected : 0;

   try {
      let old = JSON.parse(p4CmdRequestText());

      if (old && Number.isInteger(old.address))
         address = old.address;
   }
   catch (e) { }

   let obj = { 'cmd': cmd, 'address': address };

   if (cmd == 'parset')
      obj.value = '';

   p4CmdSetRequest(obj);
}

// the selected address of the list into the JSON of the request

function p4CmdTakeAddress()
{
   if (p4CmdSelected == null)
      return;

   let obj = p4CmdParse();

   if (!obj)
      return;

   obj.address = p4CmdSelected;
   p4CmdSetRequest(obj);
}

function p4CmdSubmit()
{
   let obj = p4CmdParse();

   if (!obj)
      return;

   function send() {
      showProgressDialog(60000);
      p4CmdSend(p4CmdRequest, 'request');
   }

   if (obj.cmd == 'parset') {
      confirmDialog(send, 'Wert "' + p4Esc(obj.value) + '" in den Parameter ' + p4Esc(obj.address) +
                    (Number.isInteger(obj.address) ? ' (' + p4CmdHex(obj.address) + ')' : '') +
                    ' der Heizung schreiben?', 'Schreiben', 'Abbrechen');
   }
   else
      send();
}

function p4RawReceived(data)
{
   if (data.tag == 'list') {
      if (data.error || !data.reply || data.reply.status != 'ok') {
         p4CmdPars = null;
         p4CmdParsError = data.error || (data.reply && data.reply.message) ||
            'Der Dienst p4mqttd liefert die Liste der Parameter nicht (Kommando \'parlist\', ab p4mqttd 0.11.0)';

         if (data.reply && data.reply.message == 'unknown command')
            p4CmdParsError = 'Der Dienst p4mqttd kennt das Kommando \'parlist\' nicht (ab p4mqttd 0.11.0)';
      }
      else {
         p4CmdPars = data.reply.parameters || [];
      }

      if (currentPage == 'p4menu' && p4Tab == 'cmd')
         p4CmdRenderPars();

      return;
   }

   hideProgressDialog();
   p4CmdResult = data;

   if (currentPage == 'p4menu' && p4Tab == 'cmd')
      p4CmdRenderResult();
}

//***************************************************************************
// Render
//***************************************************************************

function p4CmdRender()
{
   let $page = $('<div></div>').addClass('p4Menu p4Cmd');

   // the request

   let $request = $('<div></div>').addClass('p4CmdBox')
       .append($('<div></div>').addClass('p4CmdHead').text('Anfrage (JSON an <Basis-Topic>/cmd)'))
       .append($('<div></div>').addClass('p4MenuActions')
               .append($('<button></button>').addClass('rounded-border button1').text('parget')
                       .attr('title', 'Vorlage: Parameter lesen').click(function() { p4CmdTemplate('parget'); }))
               .append($('<button></button>').addClass('rounded-border button1').text('parset')
                       .attr('title', 'Vorlage: Parameter schreiben').click(function() { p4CmdTemplate('parset'); })))
       .append($('<textarea></textarea>').attr('id', 'p4CmdRequest').attr('spellcheck', 'false')
               .addClass('input rounded-border p4CmdJson').val(p4CmdRequest)
               .on('input', function() { p4CmdRequest = $(this).val(); }))
       .append($('<div></div>').addClass('p4MenuActions')
               .append($('<button></button>').addClass('rounded-border button1').text('Senden').click(p4CmdSubmit)))
       .append($('<div></div>').addClass('p4EditInfo')
               .text('Senden auch mit Strg+Enter. Die "rid" setzt der homectld (zur Zuordnung der Antwort), bei parset auch den "user", ' +
                     'ein "replyTo" wird entfernt. ' +
                     'Wert bei parset: Zahl, "ja" / "nein" oder Uhrzeit "hh:mm" je nach Parameter.'))
       .append($('<div></div>').addClass('p4EditHint')
               .text(p4MayWrite() ? 'Achtung: parset schreibt sofort in die Heizung!'
                     : 'Das Schreiben (parset) erfordert das Recht "Einstellungen".'));

   // the addresses

   let $pars = $('<div></div>').addClass('p4CmdBox')
       .append($('<div></div>').addClass('p4CmdHead').text('Adressen (Parameter des Service-Menüs)'))
       .append($('<div></div>').attr('id', 'p4CmdPars'));

   $page.append($('<div></div>').addClass('p4CmdRow').append($request).append($pars));
   $page.append($('<div></div>').attr('id', 'p4CmdResult'));

   // how to use it in scripts (see p4mqttd/README.md)

   $page.append($('<div></div>').addClass('p4CmdBox p4CmdGuide')
                .append($('<div></div>').addClass('p4CmdHead').text('Verwendung in Scripts'))
                .append($('<ul></ul>')
                        .append($('<li></li>').text('Kommando als JSON an <Basis-Topic>/cmd (Standard p4/cmd), nicht retained. Erst die Antwort abonnieren, dann senden, mit Timeout arbeiten.'))
                        .append($('<li></li>').text('Die Antwort kommt mit derselben "rid" auf <Basis-Topic>/reply. Das Topic teilen sich alle, also eine eindeutige "rid" mit eigenem Präfix wählen (reserviert: "homectld-") und danach filtern:'))
                        .append($('<pre></pre>').addClass('rounded-border p4CmdJson').text(
                           'RID="myscript-$$-$(date +%s)"\n' +
                           'mosquitto_sub -h <broker> -t p4/reply -W 30 | jq -c --unbuffered "select(.rid==\\"$RID\\")" | head -n1 &\n' +
                           'sleep 0.5\n' +
                           'mosquitto_pub -h <broker> -t p4/cmd -m "{\\"cmd\\":\\"parget\\",\\"rid\\":\\"$RID\\",\\"address\\":480}"\n' +
                           'wait'))
                        .append($('<li></li>').text('Option statt des Filters: mit "replyTo" (ein eigenes Topic unterhalb von <Basis-Topic>/reply/) kommt die Antwort nur dort an:'))
                        .append($('<pre></pre>').addClass('rounded-border p4CmdJson').text(
                           'mosquitto_sub -h <broker> -t p4/reply/myscript -C 1 -W 30 &\n' +
                           'sleep 0.5\n' +
                           'mosquitto_pub -h <broker> -t p4/cmd -m \'{"cmd":"parget","rid":"1","address":480,"replyTo":"p4/reply/myscript"}\'\n' +
                           'wait'))));

   p4CmdEditor = null;
   $('#container').empty().append($page);
   p4CmdRenderPars();
   p4CmdRenderResult();
   p4CmdInitEditor();
}

function p4CmdRenderPars()
{
   let $div = $('#p4CmdPars').empty();

   if (!$div.length)
      return;

   if (!p4CmdPars) {
      $div.append($('<div></div>').addClass(p4CmdParsError ? 'p4MenuError rounded-border' : 'p4EditInfo')
                  .text(p4CmdParsError || 'Liste wird gelesen ...'));

      if (p4CmdParsError)
         $div.append($('<div></div>').addClass('p4MenuActions')
                     .append($('<button></button>').addClass('rounded-border button1').text('Liste neu lesen')
                             .click(function() { p4CmdLoadPars(); p4CmdRenderPars(); })));

      setupContainerHeight();
      return;
   }

   let $select = $('<select></select>').attr('id', 'p4CmdSelect').attr('size', 12).addClass('input rounded-border p4CmdSelect')
       .change(function() { p4CmdSelected = parseInt($(this).val()); $('#p4CmdTake').prop('disabled', false); })
       .dblclick(p4CmdTakeAddress);

   function fill() {
      let words = p4CmdFilter.toLowerCase().split(/\s+/).filter(function(w) { return w.length; });
      let count = 0;

      $select.empty();

      for (const p of p4CmdPars) {
         let text = p.address + ' (' + p4CmdHex(p.address) + ')  ' + (p.path ? p.path + ' / ' : '') + p.title;
         let lower = text.toLowerCase();

         if (!words.every(function(w) { return lower.indexOf(w) >= 0; }))
            continue;

         $select.append($('<option></option>').val(p.address).text(text).prop('selected', p.address == p4CmdSelected));
         count++;
      }

      $('#p4CmdCount').text(count + ' von ' + p4CmdPars.length + ' Parametern');
   }

   $div.append($('<input/>').attr('type', 'search').attr('placeholder', 'Filter (Adresse, Menü, Titel)').attr('autocomplete', 'off')
               .addClass('input rounded-border p4CmdFilter').val(p4CmdFilter)
               .on('input', function() { p4CmdFilter = $(this).val(); fill(); }))
      .append($select)
      .append($('<div></div>').addClass('p4MenuActions')
              .append($('<button></button>').attr('id', 'p4CmdTake').addClass('rounded-border button1').text('Adresse in die Anfrage eintragen')
                      .prop('disabled', p4CmdSelected == null).click(p4CmdTakeAddress))
              .append($('<button></button>').addClass('rounded-border button1').text('Liste neu lesen')
                      .click(function() { p4CmdPars = null; p4CmdLoadPars(); p4CmdRenderPars(); })))
      .append($('<div></div>').attr('id', 'p4CmdCount').addClass('p4EditInfo'));

   fill();
   setupContainerHeight();
}

// the labels of the elements of an answer

var p4CmdLabels = {
   'title': 'Parameter', 'address': 'Adresse', 'value': 'Wert', 'old': 'Alter Wert', 'unit': 'Einheit',
   'min': 'Minimum', 'max': 'Maximum', 'default': 'Standard', 'digits': 'Nachkommastellen', 'type': 'Typ'
};

var p4CmdStates = {
   'ok': 'ok', 'nochange': 'Wert unverändert', 'range': 'außerhalb des Bereichs', 'format': 'falsches Format',
   'readonly': 'nur lesen', 'error': 'Fehler', 'comm': 'Kommunikationsfehler', 'notready': 'nicht bereit'
};

function p4CmdRenderResult()
{
   let $div = $('#p4CmdResult').empty();
   let res = p4CmdResult;

   if (!$div.length || !res)
      return;

   if (res.error) {
      $div.append($('<div></div>').addClass('p4MenuError rounded-border').text(res.error));
      setupContainerHeight();
      return;
   }

   let reply = res.reply || {};
   let unit = reply.unit || '';
   let isSwitch = reply.type == 0x08;
   let good = reply.status == 'ok';

   // prepared

   let $body = $('<tbody></tbody>');

   function row(name, value, cls) {
      $body.append($('<tr></tr>').addClass(cls || '').append($('<td></td>').text(name)).append($('<td></td>').text(value)));
   }

   row('Status', (p4CmdStates[reply.status] || reply.status) + (reply.message ? ' - ' + reply.message : ''),
       good ? 'p4CmdOk' : reply.status == 'nochange' ? '' : 'p4ErrorOpen');
   row('Kommando', reply.cmd + ' (rid ' + reply.rid + ', Antwort nach ' + (res.duration || 0) + ' s)');

   for (const key of ['title', 'address', 'value', 'old', 'min', 'max', 'default', 'digits', 'type']) {
      if (reply[key] == null || reply[key] === '')
         continue;

      let value = String(reply[key]);

      if (key == 'address')
         value += ' (' + p4CmdHex(reply.address) + ')';
      else if (key == 'type')
         value = '0x' + Number(reply.type).toString(16);
      else if ((key == 'value' || key == 'old' || key == 'min' || key == 'max' || key == 'default') && !isSwitch && unit)
         value += ' ' + unit;

      row(p4CmdLabels[key], value);
   }

   for (const key of Object.keys(reply)) {                 // whatever else the answer carries
      if (['rid', 'cmd', 'status', 'message', 'unit'].indexOf(key) >= 0 || p4CmdLabels[key])
         continue;

      row(key, typeof reply[key] == 'object' ? JSON.stringify(reply[key]) : String(reply[key]));
   }

   let $nice = $('<div></div>').addClass('p4CmdBox')
       .append($('<div></div>').addClass('p4CmdHead').text('Antwort aufbereitet'))
       .append($('<table></table>').addClass('tableMultiCol p4ParTable')
               .append($('<thead></thead>').append($('<tr></tr>')
                                                   .append($('<td></td>').text('Element'))
                                                   .append($('<td></td>').text('Wert'))))
               .append($body));

   // JSON

   let $json = $('<div></div>').addClass('p4CmdBox')
       .append($('<div></div>').addClass('p4CmdHead').text('Antwort (JSON von <Basis-Topic>/reply)'))
       .append($('<pre></pre>').addClass('rounded-border p4CmdJson').text(JSON.stringify(reply, null, 2)))
       .append($('<div></div>').addClass('p4CmdHead').text('Gesendet'))
       .append($('<pre></pre>').addClass('rounded-border p4CmdJson').text(JSON.stringify(res.request, null, 2)));

   $div.append($('<div></div>').addClass('p4CmdRow').append($json).append($nice));
   setupContainerHeight();
}
