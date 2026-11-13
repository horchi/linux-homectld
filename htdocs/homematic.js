/*
 *  homematic.js
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Setup page 'HomeMatic': the devices of the service hmmqtt (CUL stick, no CCU),
 *  pairing of new devices, removing devices and the running times of the blinds
 *  (see hmservice.c)
 */

var homeMatic = {};

// Running times of a blind: the actor has no position sensor and doesn't detect the end positions,
//   it calculates the position (%) from its running times down / up. The panel edits them and the
//   characteristic of hmmqtt ('half': level of the actor at half the height of the window) and writes
//   them once to the actor. A stop watch helps to measure: the blind is driven as usual (with the
//   running times of the actor, nothing is written for it), the user clicks at half height and at the
//   end position.

var hmEdit = null;        // { serial, channel, title, down, up, half (texts of the inputs), run, t0, tHalf, halfDown, halfUp }
var hmEditTimer = null;

function hmSend(object)
{
   socket.send({ 'event': 'homematic', 'object': object });
}

function hmEditStart(dev, title)
{
   let ch = (dev.channels && dev.channels.length) ? dev.channels[0] : {};

   hmEdit = { 'serial': dev.serial, 'channel': ch.channel, 'title': title || dev.serial,
              'down': '', 'up': '', 'half': '', 'filled': false };

   if (dev.runtimeDown == null || dev.runtimeUp == null)
      hmSend({ 'action': 'runtime', 'serial': dev.serial, 'quiet': true });     // read them

   initHomeMatic();
}

function hmEditMeasure(direction)
{
   hmEdit.run = direction;
   hmEdit.t0 = Date.now();
   hmEdit.tHalf = null;
   hmSend({ 'action': 'drive', 'channel': hmEdit.channel, 'level': direction == 'down' ? 0 : 100 });
   initHomeMatic();
}

function hmEditMeasureDone()
{
   let t = (Date.now() - hmEdit.t0) / 1000;

   if (hmEdit.run == 'down') {
      hmEdit.down = t.toFixed(1);
      if (hmEdit.tHalf)
         hmEdit.halfDown = 100 - hmEdit.tHalf / t * 100;
   }
   else {
      hmEdit.up = t.toFixed(1);
      if (hmEdit.tHalf)
         hmEdit.halfUp = hmEdit.tHalf / t * 100;
   }

   // half height: mean of the measured directions

   let halves = [hmEdit.halfDown, hmEdit.halfUp].filter(h => h != null);

   if (hmEdit.tHalf && halves.length)
      hmEdit.half = (halves.reduce((a, b) => a + b) / halves.length).toFixed(1);

   hmEdit.run = null;
   hmEdit.dirty = true;
   initHomeMatic();
}

function hmEditPanel(online)
{
   let dev = (homeMatic.devices || []).find(d => d.serial == hmEdit.serial) || {};
   let $panel = $('<div></div>').addClass('rounded-border homeMaticLearn');
   let $text = $('<div></div>').addClass('homeMaticLearnText');
   let $buttons = $('<div></div>').addClass('homeMaticActions');
   let button = function(text, onClick) {
      return $('<button></button>').addClass('rounded-border button1').text(text)
         .prop('disabled', !online).click(onClick);
   };

   if (!hmEdit.filled && dev.runtimeDown != null && dev.runtimeUp != null) {    // the values of the actor
      hmEdit.filled = true;
      hmEdit.down = dev.runtimeDown.toFixed(1);
      hmEdit.up = dev.runtimeUp.toFixed(1);
      hmEdit.half = dev.half != null ? dev.half.toFixed(1) : '';
   }

   $panel.append($('<div></div>').addClass('homeMaticLearnTitle')
                 .text('Laufzeiten \'' + hmEdit.title + '\' (' + hmEdit.serial + ')'));

   if (hmEdit.run) {
      let down = hmEdit.run == 'down';

      $text.html('Der Rollladen fährt ' + (down ? 'ab' : 'auf') + ': <span class="homeMaticLearnClock"></span><br>' +
                 '\'Halbe Höhe\' klicken wenn das Fenster halb ' + (down ? 'bedeckt' : 'frei') + ' ist (optional), \'' +
                 (down ? 'Ist unten' : 'Ist oben') + '\' sobald er in der Endlage steht.' +
                 (hmEdit.tHalf ? '<br>Halbe Höhe nach ' + hmEdit.tHalf.toFixed(1).replace('.', ',') + ' s' : ''));

      if (!hmEdit.tHalf)
         $buttons.append(button('Halbe Höhe', function() {
            hmEdit.tHalf = (Date.now() - hmEdit.t0) / 1000;
            initHomeMatic();
         }));

      $buttons.append(button(down ? 'Ist unten' : 'Ist oben', hmEditMeasureDone));
      $buttons.append(button('Messung abbrechen', function() {
         hmSend({ 'action': 'drive', 'channel': hmEdit.channel, 'stop': true });
         hmEdit.run = null;
         initHomeMatic();
      }));

      if (!hmEditTimer)
         hmEditTimer = setInterval(hmEditClock, 100);

      return $panel.append($text).append($buttons);
   }

   let input = function(field, max) {
      return $('<input></input>').attr('type', 'number').attr('step', '0.1').attr('min', '1').attr('max', max)
         .addClass('rounded-border inputSetting homeMaticLearnInput')
         .val(hmEdit[field])
         .on('input', function() {
            hmEdit[field] = $(this).val();

            if (!hmEdit.dirty) {                      // not saved: no direct change to another blind
               hmEdit.dirty = true;
               $('.homeMaticEditOpen').prop('disabled', true);
            }
         });
   };

   $text.append($('<div></div>').addClass('homeMaticLearnValues')
                .append($('<span></span>').text('Laufzeit ab '))
                .append(input('down', 600))
                .append($('<span></span>').text(' s   auf '))
                .append(input('up', 600))
                .append($('<span></span>').text(' s   halbe Höhe bei '))
                .append(input('half', 99))
                .append($('<span></span>').text(' % (Position des Aktors bei halb bedecktem Fenster, leer: keine Kennlinie)')));
   $text.append($('<div></div>').addClass('homeMaticLearnState')
                .html('Messen: der Rollladen fährt wie gewohnt von einer Endlage in die andere, die Uhr läuft mit und die ' +
                      'Zeit wird oben eingetragen. Für \'Ab messen\' muss er ganz oben stehen, für \'Auf messen\' ganz unten. ' +
                      'In den Aktor geschrieben wird erst mit \'Schreiben\'.'));

   $buttons.append(button('Ab messen', function() { hmEditMeasure('down'); }));
   $buttons.append(button('Auf messen', function() { hmEditMeasure('up'); }));
   $buttons.append(button('Schreiben', function() {
      let down = parseFloat(hmEdit.down);
      let up = parseFloat(hmEdit.up);
      let half = parseFloat(hmEdit.half);

      if (!(down >= 1 && down <= 600 && up >= 1 && up <= 600)) {
         showInfoDialog({ 'message': 'Laufzeiten müssen zwischen 1 und 600 s liegen', 'status': -1 });
         return;
      }

      hmSend({ 'action': 'runtime', 'serial': hmEdit.serial, 'down': down, 'up': up,
               'half': (half > 0 && half < 100) ? half : -1 });
      hmEdit.dirty = false;
      initHomeMatic();
   }).attr('title', 'Laufzeiten in den Aktor schreiben und zurücklesen, Kennlinie im Dienst speichern'));
   $buttons.append(button('Schließen', function() { hmEdit = null; initHomeMatic(); }).prop('disabled', false));

   return $panel.append($text).append($buttons);
}

function hmEditClock()
{
   if (!hmEdit || !hmEdit.run) {
      clearInterval(hmEditTimer);
      hmEditTimer = null;
      return;
   }

   $('.homeMaticLearnClock').text(((Date.now() - hmEdit.t0) / 1000).toFixed(1).replace('.', ',') + ' s');
}

function initHomeMatic()
{
   $('#container').removeClass('hidden');
   prepareSetupMenu();
   setupContainerHeight();

   let hm = homeMatic;
   let status = hm.status || {};
   let pairing = hm.pairing || {};
   let viaService = hm.interface && hm.bridge == 'hmmqtt';

   let $page = $('<div></div>').addClass('homeMaticPage');      // styles in homematic.css

   $page.append($('<div></div>').addClass('homeMaticHead')
                .append($('<span></span>').addClass('mdi mdi-home-automation'))
                .append($('<span></span>').text('HomeMatic'))
                .append($('<span></span>').addClass('homeMaticCount').text((hm.devices || []).length)));

   if (!viaService) {
      $page.append($('<div></div>').addClass('homeMaticNote')
                   .html('HomeMatic ist nicht über den Dienst <b>hmmqtt</b> angebunden.<br>' +
                         'Konfiguration -> HomeMatic: \'Anbindung\' = hmmqtt.'));
      $('#container').empty().append($page);
      return;
   }

   // service state

   let online = status.state == 'online';

   let $state = $('<div></div>').addClass('homeMaticState')
       .toggleClass('homeMaticOnline', online)
       .append($('<span></span>').addClass('homeMaticDot'))
       .append($('<span></span>').addClass('homeMaticStateText').text('Dienst hmmqtt: ' + (status.state || 'keine Meldung')));

   if (status.cul)
      $state.append($('<span></span>').addClass('homeMaticChip').append($('<span></span>').addClass('mdi mdi-usb-port')).append(' ' + status.cul));
   if (status.hmId)
      $state.append($('<span></span>').addClass('homeMaticChip').append($('<span></span>').addClass('mdi mdi-access-point')).append(' Zentrale ' + status.hmId));

   $page.append($state);

   // pairing / refresh

   let $actions = $('<div></div>').addClass('homeMaticActions');

   if (pairing.active) {
      $actions
         .append($('<button></button>').addClass('rounded-border button1 homeMaticPairing')
                 .text('Anlernen aktiv – noch ' + pairing.remaining + ' s (beenden)')
                 .click(function() { socket.send({ 'event': 'homematic', 'object': { 'action': 'pairstop' } }); }))
         .append($('<span></span>').addClass('homeMaticHint')
                 .text('Jetzt die Anlerntaste am Gerät drücken'));
   }
   else {
      $actions
         .append($('<button></button>').addClass('rounded-border button1')
                 .text('Anlernen (60 s)')
                 .attr('title', 'Anlernmodus des Dienstes für 60 s, dann die Anlerntaste am Gerät drücken')
                 .prop('disabled', !online)
                 .click(function() { socket.send({ 'event': 'homematic', 'object': { 'action': 'pair', 'duration': 60 } }); }));
   }

   $actions.append($('<button></button>').addClass('rounded-border button1')
                   .text('Status abfragen')
                   .attr('title', 'Position aller Geräte neu abfragen')
                   .prop('disabled', !online)
                   .click(function() { socket.send({ 'event': 'homematic', 'object': { 'action': 'refresh' } }); }));

   $page.append($actions);

   // last pairing / removing result

   if (hm.lastEvent) {
      let ev = hm.lastEvent;
      let ok = ev.result == 'ok';
      let when = ev.time ? new Date(ev.time * 1000).toLocaleTimeString(dateLocale) + ' ' : '';
      let what = ev.event == 'paired' ? 'Anlernen' : ev.event == 'runtime' ? 'Laufzeiten' : 'Abmelden';

      let times = ev.event == 'runtime' && ev.runtimeDown != null
          ? ' (' + ev.runtimeDown + ' s ab / ' + ev.runtimeUp + ' s auf)' : '';

      $page.append($('<div></div>').addClass('homeMaticEvent ' + (ok ? 'ok' : 'failed'))
                   .append($('<span></span>').addClass('mdi ' + (ok ? 'mdi-check-circle-outline' : 'mdi-alert-circle-outline')))
                   .append($('<span></span>').text(when + what + ' ' + (ev.serial || '') + (ev.type ? ' (' + ev.type + ')' : '') + ': ' +
                                                   (ev.text || ev.result) + times)));
   }

   // running times of a blind

   if (hmEdit)
      $page.append(hmEditPanel(online));

   // devices

   let $table = $('<table></table>').addClass('tableMultiCol homeMaticDevices')
       .append($('<thead></thead>').append($('<tr></tr>')
                                           .append($('<td></td>').text('Name'))
                                           .append($('<td></td>').text('Typ'))
                                           .append($('<td></td>').text('Seriennummer'))
                                           .append($('<td></td>').text('Funkadresse'))
                                           .append($('<td></td>').text('Firmware'))
                                           .append($('<td></td>').text('Position'))
                                           .append($('<td></td>').text('Laufzeit ab / auf'))
                                           .append($('<td></td>').text('Zuletzt gehört'))
                                           .append($('<td></td>').text('RSSI'))
                                           .append($('<td></td>'))));
   let $body = $('<tbody></tbody>');

   for (const dev of (hm.devices || [])) {
      let ch = (dev.channels && dev.channels.length) ? dev.channels[0] : {};
      let title = ch.key && valueFacts[ch.key] ? factTitle(ch.key) : (ch.title || '');
      let seen = dev.lastSeen ? new Date(dev.lastSeen * 1000).toLocaleString(dateLocale) : '-';
      let blind = ch.kind == 'BLIND';
      let runtime = dev.runtimeDown != null && dev.runtimeUp != null
          ? dev.runtimeDown.toFixed(1).replace('.', ',') + ' / ' + dev.runtimeUp.toFixed(1).replace('.', ',') + ' s' : '-';
      if (dev.half != null)
         runtime += ', ½ bei ' + Math.round(dev.half) + ' %';

      // the position as bar, the RSSI as signal (green > -70, orange > -85, red below)

      let pos = ch.value != null ? Math.max(0, Math.min(100, Math.round(ch.value))) : null;
      let rssiCls = !dev.rssi ? '' : dev.rssi > -70 ? 'homeMaticGood' : dev.rssi > -85 ? 'homeMaticFair' : 'homeMaticPoor';

      $body.append($('<tr></tr>')
                   .append($('<td></td>').attr('title', ch.key || '')
                           .append($('<div></div>').addClass('homeMaticName').text(title))
                           .append($('<div></div>').addClass('homeMaticSub').text(ch.key || '')))
                   .append($('<td></td>').text(dev.type))
                   .append($('<td></td>').addClass('homeMaticMono').text(dev.serial))
                   .append($('<td></td>').addClass('homeMaticMono').text(dev.rfAddress))
                   .append($('<td></td>').text(dev.firmware))
                   .append($('<td></td>').append(pos == null ? '-' :
                                                 $('<span></span>').addClass('homeMaticPos')
                                                 .append($('<span></span>').addClass('homeMaticPosBar')
                                                         .append($('<span></span>').css('width', pos + '%')))
                                                 .append($('<span></span>').text(pos + ' %'))))
                   .append($('<td></td>').text(blind ? runtime : ''))
                   .append($('<td></td>').text(seen))
                   .append($('<td></td>').addClass(rssiCls)
                           .append(dev.rssi ? [$('<span></span>').addClass('mdi mdi-signal'), ' ' + dev.rssi + ' dBm'] : '-'))
                   .append($('<td></td>').addClass('homeMaticButtons')
                           .append(!blind ? '' : $('<button></button>').addClass('rounded-border button1')
                                   .text('Lesen')
                                   .attr('title', 'Laufzeiten aus dem Aktor lesen')
                                   .prop('disabled', !online || !!hmEdit)
                                   .click(function() { hmSend({ 'action': 'runtime', 'serial': dev.serial }); }))
                           .append(!blind ? '' : $('<button></button>').addClass('rounded-border button1')
                                   .addClass('homeMaticEditOpen')
                                   .text('Laufzeiten')
                                   .attr('title', 'Laufzeiten und Kennlinie (halbe Höhe) eintragen oder messen und in den Aktor schreiben' +
                                         (hmEdit && (hmEdit.dirty || hmEdit.run) ? ' – erst die offenen Laufzeiten schreiben oder schließen' : ''))
                                   .prop('disabled', !online || (hmEdit && (hmEdit.dirty || !!hmEdit.run)))
                                   .click(function() { hmEditStart(dev, title); }))
                           .append($('<button></button>').addClass('rounded-border button1')
                                   .text('Abmelden')
                                   .attr('title', 'Zentrale im Gerät löschen und aus der Liste entfernen')
                                   .prop('disabled', !online || !!hmEdit)
                                   .click(function() {
                                      confirmDialog(function() {
                                         socket.send({ 'event': 'homematic', 'object': { 'action': 'remove', 'serial': dev.serial } });
                                      }, '\'' + (title || dev.serial) + '\' (' + dev.serial + ') wirklich abmelden?', 'Abmelden', 'Abbrechen');
                                   }))));
   }

   if (!(hm.devices || []).length)
      $body.append($('<tr></tr>').append($('<td></td>').attr('colspan', 10).addClass('homeMaticEmpty')
                                         .text('Keine Geräte - neue mit \'Anlernen\' hinzufügen')));

   $table.append($body);
   $page.append($('<div></div>').addClass('homeMaticTableWrap').append($table));

   $('#container').empty().append($page);
}
