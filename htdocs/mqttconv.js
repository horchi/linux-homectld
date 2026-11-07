/*
 *  mqttconv.js
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Setup page 'MQTT Konverter' (admin only): the converter scripts of <confDir>/mqtt.d
 *  for MQTT messages of foreign devices (e.g. TASMOTA), see mqttconv.c
 *
 *    script   <topic with _ for />.sh, called with the MQTT url, topic, message, call count
 *    alias    symbolic link to a script (one script for several topics)
 *    skip     empty file, the messages of the topic are dropped
 */

var mqttConv = null;                // { dir, converters: [ ... ], subscriptions: [ ... ], topics: [ ... ] }
var mqttConvSelected = null;        // file of the converter in the editor, 'new' for a new one
var mqttConvNew = null;             // preset of a new one { topic, kind, code }
var mqttConvEditor = null;          // CodeMirror of the script

const mqttConvKinds = {
   'script': { 'title': 'Skript',     'icon': 'mdi-script-text-outline' },
   'alias':  { 'title': 'Alias',      'icon': 'mdi-link-variant' },
   'skip':   { 'title': 'Verwerfen',  'icon': 'mdi-cancel' }
};

const mqttConvTemplate = `#! /bin/bash

# converter of the MQTT messages of a foreign device to the format of homectld
#   \$1 MQTT url of homectld (homectld2mqtt/scripts), \$2 topic, \$3 message, \$4 call count (1 = first call)

MQTTURL="\${1}"
INTOPIC="\${2}"
MSG="\${3}"
CALL_COUNT=\${4}

LOGGER="logger -t sensormqtt -p kern.warn"

sensor()
{
   ADDRESS=\${1}
   ELEMENT="\${2}"
   TITLE="\${3}"
   UNIT="\${4}"

   value=$(echo "\${MSG}" | jq -r "\${ELEMENT}")

   RESULT=$(jo \\
               type="MYDEV" \\
               address=\${ADDRESS} \\
               kind="value" \\
               title="\${TITLE}" \\
               unit="\${UNIT}" \\
               valid=true \\
               value=\${value})

   mosquitto_pub --quiet -L \${MQTTURL} -m "\${RESULT}"
}

sensor 1 '.temperature' 'Temperatur' '°C'
`;

// event 'mqttconverters'

function mqttConvReceived(obj)
{
   mqttConv = obj;

   if (currentPage == 'mqttconverters')
      initMqttConvPage();
}

// file of a topic, as the daemon names it ('stat/bbqlight/POWER' -> 'stat_bbqlight_POWER.sh')

function mqttConvFileOf(topic)
{
   return topic.replaceAll('/', '_') + '.sh';
}

// MQTT topic filter ('+' one level, '#' the rest)

function mqttTopicMatches(filter, topic)
{
   let f = filter.split('/');
   let t = topic.split('/');

   for (let i = 0; i < f.length; i++) {
      if (f[i] == '#')
         return true;
      if (i >= t.length || (f[i] != '+' && f[i] != t[i]))
         return false;
   }

   return f.length == t.length;
}

// the subscription of the configuration receiving the topic, null if none

function mqttConvSubscription(topic)
{
   return (mqttConv.subscriptions || []).find(s => mqttTopicMatches(s, topic)) || null;
}

function mqttConvSeen(topic)
{
   return (mqttConv.topics || []).find(t => t.topic == topic) || null;
}

//***************************************************************************
// Page
//***************************************************************************

async function initMqttConvPage()
{
   $('#container').removeClass('hidden');
   prepareSetupMenu();

   if (!mqttConv) {
      $('#container').empty().append($('<div></div>').addClass('rounded-border inputTableConfig').text('wird geladen ...'));
      return;
   }

   let converters = mqttConv.converters || [];
   let files = converters.map(c => c.file);

   if (mqttConvSelected != 'new' && !files.includes(mqttConvSelected))
      mqttConvSelected = files.length ? files[0] : 'new';

   let isNew = mqttConvSelected == 'new';
   let conv = isNew
       ? Object.assign({ 'topic': '', 'kind': 'script', 'code': mqttConvTemplate }, mqttConvNew || {})
       : converters.find(c => c.file == mqttConvSelected);

   // list of the converters

   let $items = $('<div></div>').addClass('htmlWidgetListItems');

   if (!converters.length)
      $items.append($('<div></div>').addClass('htmlWidgetListEmpty').text('noch keine'));

   for (let c of converters) {
      let $item = $('<div></div>')
          .addClass('htmlWidgetListItem' + (c.file == mqttConvSelected ? ' active' : ''))
          .attr('title', c.file + (c.kind == 'alias' ? ' -> ' + c.target : ''))
          .append($('<span></span>').addClass('mdi ' + mqttConvKinds[c.kind].icon))
          .append($('<span></span>').text(' ' + c.topic))
          .click(function() { mqttConvSelected = c.file; mqttConvNew = null; initMqttConvPage(); });

      if (!mqttConvSubscription(c.topic))
         $item.append($('<span></span>').addClass('mdi mdi-alert-outline mqttConvWarnIcon').attr('title', 'Topic nicht abonniert'));

      $items.append($item);
   }

   let $list = $('<div></div>').addClass('htmlWidgetList')
       .append($('<div></div>').addClass('htmlWidgetListTitle').text('Konverter (' + converters.length + ')'))
       .append($items)
       .append($('<div></div>').addClass('htmlWidgetListButtons')
               .append($('<button></button>')
                       .addClass('rounded-border button1')
                       .append($('<span></span>').addClass('mdi mdi-plus'))
                       .append(' Neu')
                       .click(function() { mqttConvStartNew({}); })));

   // the received topics without converter (via the subscriptions)

   let open = (mqttConv.topics || []).filter(t => !files.includes(mqttConvFileOf(t.topic)))
       .sort((a, b) => a.topic.localeCompare(b.topic));
   let $topics = $('<div></div>').addClass('htmlWidgetListItems');

   if (!open.length)
      $topics.append($('<div></div>').addClass('htmlWidgetListEmpty').text('keine'));

   for (let t of open) {
      $topics.append($('<div></div>')
                     .addClass('htmlWidgetListItem')
                     .attr('title', 'Letzte Nachricht ' + mqttConvAgo(t.time) + ':\n' + t.message.substring(0, 300) + '\n\nKlick: Konverter anlegen')
                     .append($('<span></span>').addClass('mdi ' + (mqttConvIsNative(t.message) ? 'mdi-check' : 'mdi-help-circle-outline')))
                     .append($('<span></span>').text(' ' + t.topic))
                     .click(function() { mqttConvStartNew({ 'topic': t.topic }); }));
   }

   let $seen = $('<div></div>').addClass('htmlWidgetList mqttConvSeenList')
       .append($('<div></div>').addClass('htmlWidgetListTitle')
               .text('Empfangene Topics ohne Konverter (' + open.length + ')')
               .attr('title', 'Seit dem Start von homectld über die abonnierten Topics empfangen.\n' +
                     '✓ versteht homectld direkt (JSON mit type), ? braucht vermutlich einen Konverter'))
       .append($topics);

   // editor

   let scripts = converters.filter(c => c.kind == 'script' && c.file != conv.file);
   let $target = $('<select></select>').attr('id', 'mcTarget').addClass('rounded-border inputSetting');

   for (let s of scripts)
      $target.append($('<option></option>').val(s.file).text(s.topic + '  (' + s.file + ')'));

   $target.val(conv.target || (scripts.length ? scripts[0].file : ''));

   let $topicList = $('<datalist></datalist>').attr('id', 'mcTopicList');

   for (let t of open)
      $topicList.append($('<option></option>').val(t.topic));

   let $editor = $('<div></div>').addClass('htmlWidgetEditor')
       .append($('<div></div>').addClass('htmlWidgetRow')
               .append($('<span></span>').text('Topic'))
               .append($('<input></input>').attr('id', 'mcTopic').attr('type', 'text').attr('list', 'mcTopicList')
                       .attr('placeholder', 'stat/mydevice/POWER')
                       .addClass('rounded-border inputSetting').val(conv.topic)
                       .prop('readonly', !isNew)
                       .attr('title', isNew ? 'Genau ein Topic, ohne Platzhalter (+ #)' : 'Für ein anderes Topic \'Duplizieren\' bzw. einen Alias anlegen')
                       .on('input', function() { mqttConvTopicInfo(); }))
               .append($topicList))
       .append($('<div></div>').addClass('htmlWidgetRow')
               .append($('<span></span>'))
               .append($('<div></div>').attr('id', 'mcTopicInfo').addClass('mqttConvInfo')))
       .append($('<div></div>').addClass('htmlWidgetRow')
               .append($('<span></span>').text('Art'))
               .append($('<select></select>').attr('id', 'mcKind')
                       .addClass('rounded-border inputSetting')
                       .append($('<option></option>').val('script').text('Skript'))
                       .append($('<option></option>').val('alias').text('Alias (anderes Skript verwenden)').prop('disabled', !scripts.length))
                       .append($('<option></option>').val('skip').text('Verwerfen (Nachrichten ignorieren)'))
                       .val(conv.kind)
                       .change(function() { mqttConvKindChanged(); })))
       .append($('<div></div>').addClass('htmlWidgetRow mcForScript')
               .append($('<span></span>').text('Skript'))
               .append($('<textarea></textarea>').attr('id', 'mcCode').attr('spellcheck', false)
                       .addClass('rounded-border htmlWidgetCode').val(conv.kind == 'script' ? (conv.code || '') : mqttConvTemplate)))
       .append($('<div></div>').addClass('htmlWidgetRow mcForAlias')
               .append($('<span></span>').text('Skript von'))
               .append($target))
       .append($('<div></div>').addClass('htmlWidgetRow mcForSkip')
               .append($('<span></span>'))
               .append($('<div></div>').addClass('mqttConvInfo')
                       .text('Leere Datei: die Nachrichten dieses Topics werden verworfen, z. B. Antworten eines Geräts auf ' +
                             'einem Topic, das über einen Platzhalter mit abonniert ist.')))
       .append($('<div></div>').addClass('htmlWidgetRow')
               .append($('<span></span>'))
               .append($('<div></div>').attr('id', 'mcState').addClass('htmlWidgetWarn')))
       .append($('<div></div>').addClass('htmlWidgetButtons')
               .append($('<button></button>').addClass('rounded-border button1').text('Speichern')
                       .click(function() { mqttConvStore(); }))
               .append($('<button></button>').addClass('rounded-border button1').text('Duplizieren')
                       .attr('title', 'Als neuen Konverter für ein anderes Topic')
                       .prop('disabled', isNew)
                       .click(function() { mqttConvStartNew({ 'kind': conv.kind, 'code': mqttConvCode(), 'target': $('#mcTarget').val() }); }))
               .append($('<button></button>').addClass('rounded-border button1').text('Alias anlegen')
                       .attr('title', 'Ein weiteres Topic mit diesem Skript')
                       .prop('disabled', isNew || conv.kind != 'script')
                       .click(function() { mqttConvStartNew({ 'kind': 'alias', 'target': conv.file }); }))
               .append($('<button></button>').addClass('rounded-border button1 htmlWidgetDelete').text('Löschen')
                       .prop('disabled', isNew)
                       .click(function() { mqttConvDelete(conv); })))
       .append($('<div></div>').addClass('htmlWidgetRow')
               .append($('<span></span>').text('Letzte Nachricht'))
               .append($('<pre></pre>').attr('id', 'mcLastMessage').addClass('rounded-border mqttConvMessage')))
       .append($('<div></div>').addClass('htmlWidgetRow mcForScript')
               .append($('<span></span>').text('Testnachricht'))
               .append($('<textarea></textarea>').attr('id', 'mcTestMessage').attr('spellcheck', false).attr('rows', 4)
                       .attr('placeholder', 'Nachricht des Geräts für den Test, z. B. ON oder {"data": {"temperature": 21.5}}')
                       .addClass('rounded-border mqttConvMessage')
                       .on('input', function() { $(this).data('edited', true); mqttConvTestState(); })))
       .append($('<div></div>').addClass('htmlWidgetRow mcForScript')
               .append($('<span></span>'))
               .append($('<div></div>').addClass('mqttConvTestBar')
                       .append($('<button></button>').attr('id', 'mcTest').addClass('rounded-border button1')
                               .append($('<span></span>').addClass('mdi mdi-play'))
                               .append(' Testen')
                               .attr('title', 'Das Skript im Editor (auch ungespeichert) mit der Testnachricht ausführen, ' +
                                     'die erzeugten Nachrichten werden nur angezeigt, nicht gesendet')
                               .click(function() { mqttConvTest(); }))
                       .append($('<button></button>').attr('id', 'mcTestLast').addClass('rounded-border button1')
                               .append($('<span></span>').addClass('mdi mdi-restore'))
                               .append(' Letzte Nachricht')
                               .attr('title', 'Die letzte empfangene Nachricht des Topics als Testnachricht')
                               .click(function() { mqttConvTestMessage(true); }))))
       .append($('<div></div>').addClass('htmlWidgetRow mcForScript')
               .append($('<span></span>').text('Testergebnis'))
               .append($('<div></div>').attr('id', 'mcTestResult').addClass('mqttConvTestResult')))
       .append($('<div></div>').addClass('htmlWidgetHelp')
               .html('<b>Achtung:</b> die Skripte laufen als <b>root</b> auf dem Server.<br>' +
                     'Für jede Nachricht eines Topics ruft homectld <code>' + mqttConv.dir + '/&lt;Topic mit _ statt /&gt;.sh</code> auf, ' +
                     'mit <code>$1</code> MQTT URL (homectld2mqtt/scripts), <code>$2</code> Topic, <code>$3</code> Nachricht, ' +
                     '<code>$4</code> Aufrufzähler (1 beim ersten Aufruf nach dem Start bzw. nach dem Speichern, z. B. für <code>action="init"</code>).<br>' +
                     'Das Skript sendet die Werte im Format von homectld per <code>mosquitto_pub -L $1 -m \'{ "type": "...", "address": 1, "kind": "value", "title": "...", "unit": "...", "value": 1.0 }\'</code> ' +
                     '(<code>kind</code> value | status mit <code>state</code> | text mit <code>text</code>), <code>jq</code> und <code>jo</code> helfen beim Zerlegen und Bauen des JSON.<br>' +
                     'Der Konverter wird nur aufgerufen, wenn das Topic abonniert ist: <i>Konfiguration -&gt; MQTT -&gt; Zusätzliche sensor Topics</i>. ' +
                     '<a href="#" class="mqttConvHelp">Mehr im README</a>'));

   let $page = $('<div></div>').addClass('rounded-border inputTableConfig htmlWidgetPage mqttConvPage')
       .append($('<div></div>').addClass('htmlWidgetSide').append($list).append($seen))
       .append($('<div></div>').addClass('htmlWidgetMain').append($editor));

   $('#container').empty().append($page);
   $('.mqttConvHelp').click(function(e) { e.preventDefault(); showHelp('doc/mqtt-converter.md#mqtt-converter'); });

   mqttConvEditor = null;
   mqttConvKindChanged();
   mqttConvTopicInfo();

   let state = [];

   if (!isNew && conv.kind == 'script' && conv.executable === false)
      state.push('Das Skript ist nicht ausführbar, homectld kann es nicht aufrufen - Speichern setzt die Rechte');
   if (!isNew && conv.kind == 'alias' && conv.valid === false)
      state.push('Das Ziel \'' + conv.target + '\' des Alias gibt es nicht');
   if (!isNew && conv.kind == 'script') {
      let aliases = converters.filter(c => c.kind == 'alias' && c.target == conv.file).map(c => c.topic);
      if (aliases.length)
         $('#mcState').before($('<div></div>').addClass('mqttConvInfo').text('Auch verwendet von: ' + aliases.join(', ')));
   }

   $('#mcState').text(state.join(', '));

   // CodeMirror (iosetup.js), without it the plain textarea stays

   let area = document.getElementById('mcCode');

   if (!await loadCodeMirror() || !document.body.contains(area))
      return;

   defineShellMode();

   mqttConvEditor = CodeMirror.fromTextArea(area, {
      mode:           'shellsimple',
      lineNumbers:    true,
      indentUnit:     3,
      tabSize:        3,
      indentWithTabs: false,
      lineWrapping:   true,
      extraKeys:      { 'Ctrl-S': function() { mqttConvStore(); } }
   });

   mqttConvEditor.getWrapperElement().classList.add('htmlWidgetCm', 'mcCodeCm');
   new ResizeObserver(function() { mqttConvEditor && mqttConvEditor.refresh(); }).observe(mqttConvEditor.getWrapperElement());
}

function mqttConvStartNew(preset)
{
   mqttConvSelected = 'new';
   mqttConvNew = preset;
   initMqttConvPage();
}

function mqttConvCode()
{
   return mqttConvEditor ? mqttConvEditor.getValue() : $('#mcCode').val();
}

function mqttConvKindChanged()
{
   let kind = $('#mcKind').val();

   $('.mcForScript').toggle(kind == 'script');
   $('.mcForAlias').toggle(kind == 'alias');
   $('.mcForSkip').toggle(kind == 'skip');

   if (mqttConvEditor)
      mqttConvEditor.refresh();
}

// subscription and last message of the topic in the editor

function mqttConvTopicInfo()
{
   let topic = $('#mcTopic').val().trim();
   let $info = $('#mcTopicInfo').empty().removeClass('htmlWidgetError');

   if (topic == '') {
      $('#mcLastMessage').text('');
      mqttConvTestMessage(false);
      return;
   }

   if (/[+#]/.test(topic))
      $info.addClass('htmlWidgetError').text('Ein Konverter gilt für genau ein Topic, ohne Platzhalter (+ #)');
   else {
      let subscription = mqttConvSubscription(topic);

      if (subscription)
         $info.text('Datei ' + mqttConvFileOf(topic) + ', abonniert über \'' + subscription + '\'');
      else
         $info.addClass('htmlWidgetError')
            .text('Nicht abonniert - der Konverter wird nie aufgerufen. Das Topic (oder ein Muster wie \'' +
                  topic.replace(/\/[^\/]*$/, '/+') + '\') in Konfiguration -> MQTT -> Zusätzliche sensor Topics eintragen.');
   }

   let seen = mqttConvSeen(topic);

   mqttConvTestMessage(false);

   if (!seen) {
      $('#mcLastMessage').text('seit dem Start von homectld nichts empfangen');
      return;
   }

   let text = seen.message;

   try {
      text = JSON.stringify(JSON.parse(seen.message), null, 2);
   }
   catch (e) {}

   $('#mcLastMessage').text(mqttConvAgo(seen.time) + ':\n' + text);
}

// a message homectld processes itself (JSON with 'type'), no converter needed

function mqttConvIsNative(message)
{
   try {
      let o = JSON.parse(message);
      return o != null && typeof o == 'object' && !!o.type;
   }
   catch (e) {
      return false;
   }
}

function mqttConvAgo(time)
{
   let seconds = Math.max(0, Math.round(Date.now() / 1000 - (daemonState.timeOffset || 0) - time));
   return 'vor ' + prettyDuration(seconds);
}

function mqttConvStore()
{
   let isNew = mqttConvSelected == 'new';
   let topic = $('#mcTopic').val().trim();
   let kind = $('#mcKind').val();

   if (topic == '') {
      showInfoDialog({ 'status': -1, 'message': 'Das Topic fehlt' });
      return;
   }

   let send = function() {
      mqttConvSelected = mqttConvFileOf(topic);
      mqttConvNew = null;
      socket.send({ 'event': 'mqttconverters', 'object': {
         'action': 'store',
         'isNew':  isNew,
         'topic':  topic,
         'kind':   kind,
         'code':   kind == 'script' ? mqttConvCode() : '',
         'target': kind == 'alias' ? $('#mcTarget').val() : ''
      }});
   };

   if (!mqttConvSubscription(topic))
      confirmDialog(send, 'Das Topic \'' + topic + '\' ist nicht abonniert, der Konverter wird so nie aufgerufen. Trotzdem speichern?', 'Speichern', 'Abbrechen');
   else
      send();
}

function mqttConvDelete(conv)
{
   confirmDialog(function() {
      mqttConvSelected = null;
      socket.send({ 'event': 'mqttconverters', 'object': { 'action': 'delete', 'file': conv.file } });
   }, 'Konverter \'' + conv.topic + '\' (' + conv.file + ') löschen?', 'Löschen', 'Abbrechen');
}

// shell mode of the editor (CodeMirror simple mode, lib/codemirror/simple.js)

function defineShellMode()
{
   if (CodeMirror.modes.shellsimple)
      return;

   CodeMirror.defineSimpleMode('shellsimple', {
      start: [
         {regex: /#!.*$/,                                          token: 'meta'},
         {regex: /#.*$/,                                           token: 'comment'},
         {regex: /"(?:[^\\"]|\\.)*"?/,                            token: 'string'},
         {regex: /'[^']*'?/,                                       token: 'string'},
         {regex: /\$\{[^}]*\}|\$[A-Za-z_][A-Za-z0-9_]*|\$[0-9#?@*$!-]/, token: 'variable-2'},
         {regex: /(?:if|then|else|elif|fi|for|while|until|do|done|case|esac|in|function|return|local|export|exit|break|continue)\b/, token: 'keyword'},
         {regex: /(?:echo|printf|jq|jo|mosquitto_pub|logger|grep|sed|awk|test|read|cut|tr)\b/, token: 'builtin'},
         {regex: /[0-9]+\b/,                                       token: 'number'},
         {regex: /[|&;<>()\[\]{}=]/,                              token: 'operator'}
      ]
   });
}

//***************************************************************************
// Test - the script of the editor with the last message of the topic, the daemon runs
//   it with a stand-in of mosquitto_pub which only notes the messages (mqttconv.c)
//***************************************************************************

// the test message: the last message of the topic as long as it isn't edited (force: take it anyway)

function mqttConvTestMessage(force)
{
   let $msg = $('#mcTestMessage');
   let seen = mqttConvSeen($('#mcTopic').val().trim());

   if (force || !$msg.data('edited')) {
      $msg.val(seen ? seen.message : '');
      $msg.data('edited', false);
   }

   $('#mcTestLast').prop('disabled', !seen);
   mqttConvTestState();
}

function mqttConvTestState()
{
   $('#mcTest').prop('disabled', $('#mcTopic').val().trim() == '' || $('#mcTestMessage').val() == '');
}

function mqttConvTest()
{
   let topic = $('#mcTopic').val().trim();
   let message = $('#mcTestMessage').val();

   if (topic == '' || message == '')
      return;

   $('#mcTestResult').empty().append($('<div></div>').addClass('mqttConvInfo').text('läuft ...'));

   socket.send({ 'event': 'mqttconverters', 'object': {
      'action':  'test',
      'topic':   topic,
      'message': message,
      'code':    mqttConvCode()
   }});
}

// event 'mqttconvtest'

function mqttConvTestResult(obj)
{
   let $result = $('#mcTestResult').empty();

   if (!$result.length)
      return;

   let published = obj.published || [];

   $result.append($('<div></div>').addClass('mqttConvInfo')
                  .text(published.length ? published.length + ' Nachricht(en) generiert (nicht gesendet):'
                                         : 'Keine Nachricht generiert (mosquitto_pub nicht aufgerufen)'));

   for (let msg of published) {
      let text = msg;
      let problem = null;

      try {
         let o = JSON.parse(msg);
         text = JSON.stringify(o, null, 2);

         if (!o.type)
            problem = '\'type\' fehlt';
         else if (o.address == null)
            problem = '\'address\' fehlt';
         else if (o.value == null && o.state == null && o.text == null && o.action != 'init')
            problem = 'weder \'value\', \'state\' noch \'text\'';
      }
      catch (e) {
         problem = 'kein gültiges JSON: ' + e.message;
      }

      let $pre = $('<pre></pre>').addClass('rounded-border mqttConvMessage').text(text);

      if (problem)
         $pre.addClass('mqttConvBad').attr('title', problem);

      $result.append($pre);

      if (problem)
         $result.append($('<div></div>').addClass('htmlWidgetError').text(problem));
   }

   if (obj.output && obj.output.trim() != '')
      $result.append($('<div></div>').addClass('mqttConvInfo').text('Ausgabe des Skripts:'))
         .append($('<pre></pre>').addClass('rounded-border mqttConvMessage').text(obj.output.trim()));
}
