/*
 *  homematic.js
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Setup page 'HomeMatic': the devices of the service hmmqtt (CUL stick, no CCU),
 *  pairing of new devices and removing devices (see hmservice.c)
 */

var homeMatic = {};

function initHomeMatic()
{
   $('#container').removeClass('hidden');
   prepareSetupMenu();

   let hm = homeMatic;
   let status = hm.status || {};
   let pairing = hm.pairing || {};
   let viaService = hm.interface && hm.bridge == 'hmmqtt';

   let $page = $('<div></div>').addClass('rounded-border inputTableConfig homeMaticPage');

   if (!viaService) {
      $page.append($('<div></div>').addClass('homeMaticNote')
                   .html('HomeMatic ist nicht über den Dienst <b>hmmqtt</b> angebunden.<br>' +
                         'Konfiguration -> HomeMatic: \'HomeMatic Interface\' an und \'Anbindung\' = hmmqtt.'));
      $('#container').empty().append($page);
      return;
   }

   // service state

   let online = status.state == 'online';

   $page.append($('<div></div>').addClass('homeMaticState')
                .append($('<span></span>').addClass('mdi mdi-circle')
                        .css('color', online ? 'var(--green, #4caf50)' : 'var(--red, #e53935)'))
                .append($('<span></span>')
                        .text(' Dienst hmmqtt: ' + (status.state || 'keine Meldung') +
                              (status.cul ? ' – ' + status.cul : '') +
                              (status.hmId ? ' – Zentrale ' + status.hmId : ''))));

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
      let what = ev.event == 'paired' ? 'Anlernen' : 'Abmelden';

      $page.append($('<div></div>').addClass('homeMaticEvent ' + (ok ? 'ok' : 'failed'))
                   .text(when + what + ' ' + (ev.serial || '') + (ev.type ? ' (' + ev.type + ')' : '') + ': ' +
                         (ev.text || ev.result)));
   }

   // devices

   let $table = $('<table></table>').addClass('tableMultiCol homeMaticDevices')
       .append($('<thead></thead>').append($('<tr></tr>')
                                           .append($('<td></td>').text('Name'))
                                           .append($('<td></td>').text('Typ'))
                                           .append($('<td></td>').text('Seriennummer'))
                                           .append($('<td></td>').text('Funkadresse'))
                                           .append($('<td></td>').text('Firmware'))
                                           .append($('<td></td>').text('Position'))
                                           .append($('<td></td>').text('Zuletzt gehört'))
                                           .append($('<td></td>').text('RSSI'))
                                           .append($('<td></td>'))));
   let $body = $('<tbody></tbody>');

   for (const dev of (hm.devices || [])) {
      let ch = (dev.channels && dev.channels.length) ? dev.channels[0] : {};
      let title = ch.key && valueFacts[ch.key] ? factTitle(ch.key) : (ch.title || '');
      let seen = dev.lastSeen ? new Date(dev.lastSeen * 1000).toLocaleString(dateLocale) : '-';

      $body.append($('<tr></tr>')
                   .append($('<td></td>').text(title).attr('title', ch.key || ''))
                   .append($('<td></td>').text(dev.type))
                   .append($('<td></td>').text(dev.serial))
                   .append($('<td></td>').text(dev.rfAddress))
                   .append($('<td></td>').text(dev.firmware))
                   .append($('<td></td>').text(ch.value != null ? Math.round(ch.value) + ' %' : '-'))
                   .append($('<td></td>').text(seen))
                   .append($('<td></td>').text(dev.rssi ? dev.rssi + ' dBm' : '-'))
                   .append($('<td></td>')
                           .append($('<button></button>').addClass('rounded-border button1')
                                   .text('Abmelden')
                                   .attr('title', 'Zentrale im Gerät löschen und aus der Liste entfernen')
                                   .prop('disabled', !online)
                                   .click(function() {
                                      confirmDialog(function() {
                                         socket.send({ 'event': 'homematic', 'object': { 'action': 'remove', 'serial': dev.serial } });
                                      }, '\'' + (title || dev.serial) + '\' (' + dev.serial + ') wirklich abmelden?', 'Abmelden', 'Abbrechen');
                                   }))));
   }

   $table.append($body);
   $page.append($table);

   $('#container').empty().append($page);
}
