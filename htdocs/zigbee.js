/*
 *  zigbee.js
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Setup page 'Zigbee' (admin only): the devices of deCONZ - pairing (search), renaming,
 *  removing, activating their sensors (DZL / DZS), the API key of deCONZ; see zigbee.c
 */

var zigbee = null;                  // { gateway: {...}, search: { active, remaining }, devices: [ ... ] }
var zigbeeSearchEnd = 0;            // [ms] end of the running search (local clock)
var zigbeeTimer = null;
var zigbeeFilter = '';
var zigbeeSearchTotal = 180;        // [s] duration of the running search (deCONZ reports it, e.g. 180 or 600)

// event 'zigbee'

function zigbeeReceived(obj)
{
   zigbee = obj;
   zigbeeSearchEnd = obj.search && obj.search.active ? Date.now() + obj.search.remaining * 1000 : 0;

   if (obj.search && obj.search.duration)
      zigbeeSearchTotal = obj.search.duration;

   if (currentPage == 'zigbee')
      initZigbeePage();
}

function zigbeeSend(object)
{
   socket.send({ 'event': 'zigbee', 'object': object });
}

//***************************************************************************
// Page
//***************************************************************************

function initZigbeePage()
{
   $('#container').removeClass('hidden');
   prepareSetupMenu();

   if (!zigbee) {
      $('#container').empty().append($('<div></div>').addClass('rounded-border inputTableConfig').text('wird geladen ...'));
      return;
   }

   let gw = zigbee.gateway || {};
   let configured = !isEmpty(gw.url) && gw.apikey;

   let $page = $('<div></div>').addClass('rounded-border inputTableConfig zigbeePage')
       .append(zigbeeGatewayBox(gw));

   if (configured && !gw.error)
      $page.append(zigbeeSearchBox()).append(zigbeeDevicesBox());

   $('#container').empty().append($page);

   zigbeeTick();

   if (!zigbeeTimer)
      zigbeeTimer = setInterval(zigbeeTick, 1000);
}

// gateway: URL, connection, firmware, API key

function zigbeeGatewayBox(gw)
{
   let $info = $('<div></div>').addClass('zigbeeInfo');
   let row = function(title, $value) {
      $info.append($('<span></span>').text(title)).append($('<span></span>').append($value));
   };

   if (isEmpty(gw.url)) {
      row('deCONZ', $('<span></span>').addClass('zigbeeWarn')
          .text('Keine URL konfiguriert (Konfiguration -> DECONZ -> deCONZ HTTP URL, z. B. 192.168.1.10:8081)'));
   }
   else {
      row('URL', $('<span></span>').text(gw.url));
      row('Verbindung', $('<span></span>')
          .addClass(gw.connected ? 'zigbeeOk' : 'zigbeeWarn')
          .text(gw.connected ? 'Websocket verbunden' : (gw.apikey ? 'Websocket getrennt' : 'nicht registriert')));

      if (gw.error)
         row('Fehler', $('<span></span>').addClass('zigbeeWarn').text(gw.error));

      if (gw.name)
         row('Gateway', $('<span></span>').text(gw.name + (gw.devicename ? ' (' + gw.devicename + ')' : '')));
      if (gw.swversion)
         row('Version', $('<span></span>').text('deCONZ ' + gw.swversion + (gw.fwversion ? ', Firmware ' + gw.fwversion : '')));
      if (gw.channel)
         row('Netz', $('<span></span>').text('Kanal ' + gw.channel + ', PAN ID 0x' + Number(gw.panid).toString(16).padStart(4, '0')));

      row('API-Key', $('<span></span>')
          .append($('<span></span>').addClass(gw.apikey ? 'zigbeeOk' : 'zigbeeWarn').text(gw.apikey ? 'vorhanden ' : 'fehlt '))
          .append($('<button></button>').addClass('rounded-border button1 zigbeeSmallButton')
                  .text(gw.apikey ? 'Neu registrieren' : 'API-Key holen')
                  .attr('title', 'homectld bei deCONZ registrieren, vorher das Gateway in Phoscon entsperren')
                  .click(function() { zigbeeApiKey(gw.apikey); })));
   }

   return $('<div></div>').addClass('zigbeeBox')
      .append($('<div></div>').addClass('zigbeeBoxTitle').text('deCONZ Gateway'))
      .append($info);
}

function zigbeeApiKey(exists)
{
   let message = 'Zuerst das Gateway in Phoscon entsperren: <i>Gateway -> Erweitert -> App verbinden</i>, ' +
       'danach hier innerhalb von 60 Sekunden bestätigen.';

   if (exists)
      message += '<br/><br/>Der bisherige API-Key wird durch den neuen ersetzt (der alte bleibt in deCONZ registriert).';

   confirmDialog(function() { zigbeeSend({ 'action': 'apikey' }); }, message, 'Registrieren', 'Abbrechen');
}

// search (permit join)

function zigbeeSearchBox()
{
   let active = zigbeeSearchEnd > Date.now();
   let found = zigbee.devices.filter(d => d.new);   // found by the search or new within 5 min

   let $box = $('<div></div>').addClass('zigbeeBox')
       .append($('<div></div>').addClass('zigbeeBoxTitle').text('Gerät anlernen'))
       .append($('<div></div>').addClass('zigbeeSearchBar')
               .append($('<button></button>').addClass('rounded-border button1')
                       .append($('<span></span>').addClass('mdi ' + (active ? 'mdi-stop' : 'mdi-magnify')))
                       .append(active ? ' Suche beenden' : ' Suche starten')
                       .click(function() { zigbeeSend({ 'action': active ? 'stop' : 'search' }); }))
               .append($('<div></div>').addClass('zigbeeProgress' + (active ? '' : ' hidden'))
                       .append($('<div></div>').attr('id', 'zigbeeProgressBar').addClass('zigbeeProgressBar')))
               .append($('<span></span>').attr('id', 'zigbeeCountdown')));

   if (active) {
      $box.append($('<div></div>').addClass('zigbeeHelp')
                  .html('Das Zigbee-Netz ist offen - das Gerät jetzt in den Anlernmodus bringen, z. B. ' +
                        'IKEA Lampe: 6× aus/an schalten, IKEA Fernbedienung: 4× den Pairing-Knopf drücken (nahe am Stick), ' +
                        'Aqara / Xiaomi: den Knopf ca. 5 s halten bis die LED blinkt, danach alle paar Sekunden kurz drücken ' +
                        '(damit er wach bleibt), Philips Hue: Reset per Hue-App bzw. Dimmer (I + O 10 s).<br/>' +
                        'Gefundene Geräte erscheinen unten markiert, ihre Sensoren sind sofort aktiv.'));
   }

   if (found.length) {
      $box.append($('<div></div>').addClass('zigbeeFound')
                  .text((active ? 'Bisher gefunden / neu: ' : 'Neu in den letzten 5 Minuten: ') + found.map(d => d.name + ' (' + d.model + ')').join(', ')));
   }
   else if (!active) {
      $box.append($('<div></div>').addClass('zigbeeHelp')
                  .text('Öffnet das Zigbee-Netz für einige Minuten (Dauer legt deCONZ fest). ' +
                        'Die Sensoren der dabei gefundenen Geräte werden in homectld gleich aktiviert.'));
   }

   return $box;
}

function zigbeeTick()
{
   if (currentPage != 'zigbee') {
      clearInterval(zigbeeTimer);
      zigbeeTimer = null;
      return;
   }

   zigbeeUpdateValues();          // 'vor x s' of the switches

   if (!zigbeeSearchEnd)
      return;

   let remaining = Math.max(0, Math.round((zigbeeSearchEnd - Date.now()) / 1000));

   $('#zigbeeCountdown').text(remaining > 0 ? 'noch ' + remaining + ' s' : '');
   $('#zigbeeProgressBar').css('width', (remaining / zigbeeSearchTotal * 100) + '%');

   if (remaining <= 0) {
      zigbeeSearchEnd = 0;
      zigbeeSend({ 'action': 'list' });     // the daemon reports the end too, but the clocks may differ
   }
}

// devices

function zigbeeDevicesBox()
{
   let devices = zigbee.devices.slice().sort(function(a, b) {
      if (a.new != b.new)
         return a.new ? -1 : 1;
      if (a.new && (a.since || 0) != (b.since || 0))
         return (b.since || 0) - (a.since || 0);      // the latest first
      return a.name.localeCompare(b.name);
   });

   let filter = zigbeeFilter.toLowerCase();

   if (filter != '')
      devices = devices.filter(d => [d.name, d.manufacturer, d.model, d.mac]
                               .concat(d.resources.map(r => r.name + ' ' + (r.usrtitle || '') + ' ' + r.dztype))
                               .join(' ').toLowerCase().includes(filter));

   let $list = $('<div></div>').addClass('zigbeeDevices');

   for (let device of devices)
      $list.append(zigbeeDevice(device));

   if (!devices.length)
      $list.append($('<div></div>').addClass('zigbeeHelp').text(filter != '' ? 'kein passendes Gerät' : 'keine Geräte'));

   return $('<div></div>').addClass('zigbeeBox')
      .append($('<div></div>').addClass('zigbeeBoxTitle zigbeeDevicesTitle')
              .append($('<span></span>').text('Geräte (' + zigbee.devices.length + ')'))
              .append($('<input></input>').attr('type', 'search').attr('placeholder', 'Filter')
                      .addClass('rounded-border inputSetting zigbeeFilterInput')
                      .val(zigbeeFilter)
                      .on('input', function() {
                         zigbeeFilter = $(this).val();
                         let $new = zigbeeDevicesBox();
                         $(this).closest('.zigbeeBox').find('.zigbeeDevices').replaceWith($new.find('.zigbeeDevices'));
                      })))
      .append($list);
}

function zigbeeDevice(device)
{
   let state = [];

   if (device.battery != null)
      state.push($('<span></span>').addClass(device.battery < 20 ? 'zigbeeWarn' : '')
                 .append($('<span></span>').addClass('mdi ' + zigbeeBatteryIcon(device.battery)))
                 .append(' ' + device.battery + ' %'));

   state.push($('<span></span>').addClass(device.reachable ? 'zigbeeOk' : 'zigbeeWarn')
              .text(device.reachable ? 'erreichbar' : 'nicht erreichbar'));

   if (device.lastseen)
      state.push($('<span></span>').text('zuletzt ' + zigbeeAgo(device.lastseen))
                 .attr('title', moment.utc(device.lastseen).local().format('DD.MM.YYYY HH:mm')));

   let $state = $('<div></div>').addClass('zigbeeDeviceState');

   for (let $s of state)
      $state.append($s);

   let $resources = $('<div></div>').addClass('zigbeeResources');

   for (let res of device.resources)
      $resources.append(zigbeeResource(res));

   return $('<div></div>').addClass('zigbeeDevice' + (device.new ? ' zigbeeDeviceNew' : ''))
      .append($('<div></div>').addClass('zigbeeDeviceHead')
              .append($('<span></span>').addClass('mdi ' + zigbeeDeviceIcon(device) + ' zigbeeDeviceIcon'))
              .append($('<div></div>').addClass('zigbeeDeviceTitle')
                      .append($('<div></div>').addClass('zigbeeDeviceName')
                              .text(device.name)
                              .append(device.new ? $('<span></span>').addClass('zigbeeNewBadge').text('neu')
                                      .attr('title', device.since ? 'seit ' + moment.unix(device.since).format('DD.MM. HH:mm') + ' in homectld' : 'von der Suche gefunden') : null))
                      .append($('<div></div>').addClass('zigbeeDeviceModel')
                              .text([device.manufacturer, device.model, device.swversion ? 'SW ' + device.swversion : ''].filter(s => s).join(' · ')))
                      .append($('<div></div>').addClass('zigbeeDeviceMac').text(device.mac)))
              .append($state)
              .append($('<button></button>').addClass('rounded-border button1 htmlWidgetDelete zigbeeSmallButton')
                      .append($('<span></span>').addClass('mdi mdi-delete-outline'))
                      .append(' Entfernen')
                      .click(function() { zigbeeDelete(device); })))
      .append($resources);
}

function zigbeeResource(res)
{
   let key = res.type + ':0x' + (res.address != null ? Number(res.address).toString(16).padStart(2, '0') : '');
   let known = res.address != null && res.state != null;
   let $name = $('<input></input>').attr('type', 'text').attr('maxlength', 32)
       .addClass('rounded-border inputSetting zigbeeName')
       .attr('title', 'Name in deCONZ / Phoscon')
       .val(res.name);
   let $title = $('<input></input>').attr('type', 'text')
       .addClass('rounded-border inputSetting zigbeeTitle')
       .attr('placeholder', res.name)
       .attr('title', 'Eigener Titel in homectld (leer: der Name aus deCONZ)')
       .prop('disabled', !known)
       .val(res.usrtitle || '');
   let $active = $('<input></input>').attr('type', 'checkbox')
       .attr('title', 'Sensor in homectld aktiv')
       .prop('disabled', !known)
       .prop('checked', res.state == 'A');

   let $store = $('<button></button>').addClass('rounded-border button1 zigbeeSmallButton zigbeeStore')
       .text('Speichern')
       .prop('disabled', true);

   let changed = function() {
      return {
         'name':     $name.val().trim() != res.name,
         'activate': known && ($active.is(':checked') != (res.state == 'A') || $title.val().trim() != (res.usrtitle || ''))
      };
   };

   let check = function() {
      let c = changed();
      $store.prop('disabled', !c.name && !c.activate);
   };

   $store.click(function() {
      let c = changed();

      if (c.name)
         zigbeeSend({ 'action': 'rename', 'resource': res.resource, 'id': res.id, 'name': $name.val().trim() });
      if (c.activate)
         zigbeeSend({ 'action': 'activate', 'type': res.type, 'address': res.address,
                      'state': $active.is(':checked'), 'usrtitle': $title.val().trim() });
   });

   $name.on('input', check);
   $title.on('input', check);
   $active.on('change', check);

   return $('<div></div>').addClass('zigbeeResource')
      .append($('<span></span>').addClass('zigbeeResType')
              .text(zigbeeTypeTitle(res.dztype))
              .attr('title', res.dztype + '\n' + res.resource + '/' + res.id + '\n' + res.uniqueid))
      .append($name)
      .append($('<span></span>').addClass('zigbeeKey')
              .append($('<div></div>').text(known ? key : 'kein Sensor')
                      .attr('title', known ? 'ID des Sensors in homectld (wie im Sensor Setup)' : 'deCONZ hat die Resource, homectld (noch) nicht'))
              .append($('<div></div>').addClass('zigbeeDzId').text((res.resource == 'lights' ? 'light ' : 'sensor ') + res.id)
                      .attr('title', 'ID bei deCONZ (REST API ' + res.resource + '/' + res.id + ')')))
      .append($('<label></label>').addClass('zigbeeActive').append($active).append(' aktiv'))
      .append($title)
      .append($store)
      .append($('<span></span>').addClass('zigbeeValue').attr('data-key', known ? key : '')
              .attr('data-dztype', res.dztype)
              .text(known ? zigbeeValueOf(key, res.dztype) : ''));
}

function zigbeeDelete(device)
{
   let hasLight = device.resources.some(r => r.resource == 'lights');
   let sensors = device.resources.filter(r => r.address != null).map(r => r.type + ':0x' + Number(r.address).toString(16).padStart(2, '0'));
   let message = 'Das Gerät <b>' + device.name + '</b> (' + device.model + ', ' + device.mac + ') aus deCONZ entfernen?' +
       (sensors.length ? '<br/><br/>Die Sensoren ' + sensors.join(', ') + ' werden auch in homectld gelöscht ' +
        '(Dashboards, Regeln und Lua-Skripte, die sie verwenden, bitte selbst anpassen; die aufgezeichneten Werte bleiben).' : '') +
       (hasLight ? '<br/><br/><label><input type="checkbox" id="zigbeeReset" checked> Lampe aus dem Netz abmelden und auf Werkseinstellung zurücksetzen</label>' : '') +
       '<br/><br/>Ein batteriebetriebener Sensor merkt sich das Netz, für ein neues Anlernen ihn selbst zurücksetzen.';

   confirmDialog(function() {
      zigbeeSend({ 'action': 'delete', 'mac': device.mac, 'reset': $('#zigbeeReset').is(':checked') });
   }, message, 'Entfernen', 'Abbrechen');
}

// the current values of the sensors (event 'update' / 'all')

function zigbeeUpdateValues()
{
   $('.zigbeeValue').each(function() {
      let key = $(this).attr('data-key');
      if (key)
         $(this).text(zigbeeValueOf(key, $(this).attr('data-dztype')));
   });
}

function zigbeeValueOf(key, dzType)
{
   let sensor = allSensors ? allSensors[key] : null;
   let fact = valueFacts ? valueFacts[key] : null;

   if (!sensor)
      return '';

   if (key.startsWith('DZL'))
      return sensor.value ? 'an' + (sensor.score ? ' ' + sensor.score + ' %' : '') : 'aus';

   if (fact && fact.unit == 'mov')
      return sensor.state ? 'Bewegung' : 'keine Bewegung';

   // switches: the last button event and when (the code is shown also if the same button is pressed again)

   if (dzType == 'ZHASwitch') {
      if (!sensor.value || !sensor.last)
         return 'noch keine Taste';

      let seconds = Math.max(0, Math.round(Date.now() / 1000 - (daemonState.timeOffset || 0) - sensor.last));
      return 'Taste ' + sensor.value + ', vor ' + prettyDuration(seconds);
   }

   if (sensor.value == null)
      return '';

   return sensor.value + (fact && fact.unit ? ' ' + fact.unit : '');
}

// helper

function zigbeeTypeTitle(dzType)
{
   const titles = {
      'ZHATemperature':          'Temperatur',
      'ZHAHumidity':             'Feuchte',
      'ZHAPressure':             'Luftdruck',
      'ZHALightLevel':           'Helligkeit',
      'ZHAPresence':             'Bewegung',
      'ZHASwitch':               'Schalter',
      'ZHAOpenClose':            'Kontakt',
      'ZHAWater':                'Wasser',
      'ZHAVibration':            'Vibration',
      'ZHAPower':                'Leistung',
      'ZHAConsumption':          'Verbrauch',
      'ZHABattery':              'Batterie',
      'On/Off plug-in unit':     'Steckdose',
      'Dimmable light':          'Lampe dimmbar',
      'Color temperature light': 'Lampe Weißton',
      'Color light':             'Lampe Farbe',
      'Extended color light':    'Lampe Farbe',
      'On/Off light':            'Lampe',
      'Window covering device':  'Rollladen'
   };

   return titles[dzType] || dzType;
}

function zigbeeDeviceIcon(device)
{
   let types = device.resources.map(r => r.dztype);

   if (types.some(t => t.includes('plug')))
      return 'mdi-power-socket-de';
   if (device.resources.some(r => r.resource == 'lights'))
      return 'mdi-lightbulb-outline';
   if (types.includes('ZHASwitch'))
      return 'mdi-gesture-tap-button';
   if (types.includes('ZHAPresence'))
      return 'mdi-motion-sensor';
   if (types.includes('ZHATemperature') || types.includes('ZHAHumidity'))
      return 'mdi-thermometer';
   if (types.includes('ZHALightLevel'))
      return 'mdi-brightness-5';
   if (types.includes('ZHAOpenClose'))
      return 'mdi-door';

   return 'mdi-zigbee';
}

function zigbeeBatteryIcon(battery)
{
   if (battery >= 90)
      return 'mdi-battery';
   if (battery < 10)
      return 'mdi-battery-alert-variant-outline';

   return 'mdi-battery-' + Math.floor(battery / 10) * 10;
}

function zigbeeAgo(lastSeen)
{
   // deCONZ: UTC, '2026-11-07T10:42Z'

   let seconds = Math.max(0, Math.round((Date.now() - moment.utc(lastSeen).valueOf()) / 1000));
   return 'vor ' + prettyDuration(seconds);
}
