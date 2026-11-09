/*
 *  list.js
 *
 *  (c) 2020-2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Page 'List': all active sensors as compact list, grouped by their type (titles of the
 *  event 'valuetypes'), with filter. Click on the title -> info dialog of the sensor,
 *  click on the symbol of a switchable sensor -> toggle.
 */

var listFilter = localStorage.getItem(storagePrefix + 'listFilter') || '';
var listKeys = '';                 // the keys of the shown rows (rebuild if they change)
var listTimer = null;              // refresh of the ages

const listSwitchTypes = [0, 9, 12];            // Symbol, SymbolValue, SymbolText (as updateWidget() of the dashboard)
const listSkipTypes = [10, 11];                // Space, Time

function initList()
{
   if (allSensors == null || valueFacts == null) {
      console.log("Fatal: Missing sensors!");
      return;
   }

   $('#container').removeClass('hidden').empty()
      .append($('<div></div>').addClass('listPage')
              .append(listStateBox())
              .append($('<div></div>').addClass('listToolbar')
                      .append($('<input></input>').attr('type', 'search').attr('placeholder', 'Filter')
                              .addClass('rounded-border input listFilterInput')
                              .val(listFilter)
                              .on('input', function() {
                                 listFilter = $(this).val();
                                 localStorage.setItem(storagePrefix + 'listFilter', listFilter);
                                 listRender();
                              }))
                      .append($('<span></span>').attr('id', 'listCount').addClass('listCount')))
              .append($('<div></div>').attr('id', 'listContainer').addClass('listContainer')));

   listRender();

   if (!listTimer)
      listTimer = setInterval(function() {
         if (currentPage == 'list')
            updateList();
         else {
            clearInterval(listTimer);
            listTimer = null;
         }
      }, 30 * 1000);

   // calc container size

   let resize = function() {
      $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('footer') - sab);
   };

   resize();
   window.onresize = resize;
}

// daemon state

function listStateBox()
{
   let $box = $('<div></div>').addClass('rounded-border listState');

   if (daemonState.state == null || daemonState.state != 0)
      return $box.addClass('listStateFail').text((config.instanceName || 'homectld') + ' OFFLINE');

   let row = function(label, value) {
      return $('<div></div>').append($('<span></span>').addClass('listStateLabel').text(label))
         .append($('<span></span>').text(value));
   };

   return $box
      .append($('<div></div>').addClass('listStateTitle')
              .append($('<span></span>').addClass('mdi mdi-check-circle-outline'))
              .append(' ' + (config.instanceName || 'homectld') + ' online'))
      .append(row('Läuft seit', daemonState.runningsince))
      .append(row('Version', daemonState.version))
      .append(row('Last', [daemonState.average0, daemonState.average1, daemonState.average2].join('  ')));
}

// the shown sensors { group title: [ key, ... ] } in the order of the value types

function listGroups()
{
   let filter = listFilter.trim().toLowerCase();
   let groups = {};
   let order = valueTypes.map(t => t.title);

   for (let key in allSensors) {
      let fact = valueFacts[key];

      if (!fact || !fact.state || listSkipTypes.includes(listWidgetOf(key).widgettype))
         continue;

      let group = listGroupOf(fact.type);

      if (filter != '' && ![factTitle(key), key, group, listValue(key)].join(' ').toLowerCase().includes(filter))
         continue;

      if (!groups[group])
         groups[group] = [];

      groups[group].push(key);
   }

   for (let g in groups)
      groups[g].sort((a, b) => factTitle(a).localeCompare(factTitle(b)));

   let sorted = {};

   Object.keys(groups)
      .sort((a, b) => (order.indexOf(a) + 1 || 999) - (order.indexOf(b) + 1 || 999) || a.localeCompare(b))
      .forEach(g => sorted[g] = groups[g]);

   return sorted;
}

function listGroupOf(type)
{
   let vt = valueTypes.find(t => t.type == type);
   return vt && vt.title ? vt.title : type;
}

// symbol of a group (from the valuetypes table, the first type with this title having one)

function listGroupIcon(title)
{
   let vt = valueTypes.find(t => t.title == title && listMdi(t.symbol));
   return vt ? listMdi(vt.symbol) : 'mdi-folder-outline';
}

function listRender()
{
   let groups = listGroups();
   let $list = $('#listContainer').empty();
   let count = 0;
   let keys = [];

   for (let group in groups) {
      $list.append($('<div></div>').addClass('listGroup')
                   .append($('<span></span>').addClass('listGroupIcon mdi ' + listGroupIcon(group)))
                   .append($('<span></span>').addClass('listGroupTitle').text(group))
                   .append($('<span></span>').addClass('listGroupCount').text(groups[group].length)));

      for (let key of groups[group]) {
         $list.append(listRow(key));
         keys.push(key);
         count++;
      }
   }

   if (!count)
      $list.append($('<div></div>').addClass('listEmpty').text(listFilter != '' ? 'kein passender Sensor' : 'keine Sensoren'));

   $('#listCount').text(count + ' Sensoren');
   listKeys = keys.join(',');
   updateList();
}

function listRow(key)
{
   let fact = valueFacts[key];
   let widget = listWidgetOf(key);
   let isSwitch = listSwitchTypes.includes(widget.widgettype);
   let canSwitch = isSwitch && (localStorage.getItem(storagePrefix + 'Rights') & fact.rights);
   let id = listId(key);

   let $symbol = $('<span></span>').attr('id', 'listSym' + id).addClass('listSymbol');

   if (canSwitch)
      $symbol.addClass('listSwitch')
         .attr('title', 'Klicken: schalten')
         .click(function() { toggleIo(fact.address, fact.type); });

   return $('<div></div>').addClass('listRow').attr('id', 'listRow' + id)
      .append($symbol)
      .append($('<div></div>').addClass('listTitle')
              .attr('title', 'Info')
              .click(function() { sensorInfoDialog(key); })
              .append($('<div></div>').addClass('listName').text(factTitle(key)))
              .append($('<div></div>').addClass('listKey').text(key)))
      .append($('<div></div>').attr('id', 'listVal' + id).addClass('listValue'))
      .append($('<div></div>').attr('id', 'listAge' + id).addClass('listAge'));
}

function listId(key)
{
   return key.replace(/[^A-Za-z0-9]/g, '_');
}

// the value as text

function listValue(key)
{
   let sensor = allSensors[key];
   let fact = valueFacts[key];
   let widget = listWidgetOf(key);
   let unit = widget.unit;

   if (!sensor)
      return '';

   if (listSwitchTypes.includes(widget.widgettype)) {
      let text = sensor.value ? 'an' : 'aus';
      if (widget.widgettype == 9 && sensor.value != null)
         text = sensor.value + (unit ? ' ' + unit : '');
      else if (sensor.score)
         text += ' ' + sensor.score + ' %';
      if (!isEmpty(sensor.text) && widget.widgettype == 12)
         text += ' · ' + sensor.text;
      return text;
   }

   if (widget.widgettype == 2 || widget.widgettype == 7 || widget.widgettype == 8 || (sensor.value == null && !isEmpty(sensor.text)))
      return sensor.text || '';

   if (sensor.value == null)
      return '';

   return sensor.value + (unit ? ' ' + unit : '');
}

// values, symbols, ages (event 'update' / 'all' and every 30 s)

function updateList()
{
   if (!$('#listContainer').length)
      return;

   // other sensors (new / removed / activated) -> rebuild

   let groups = listGroups();
   let keys = Object.values(groups).flat().join(',');

   if (keys != listKeys)
      return listRender();

   let now = Date.now() / 1000 - (daemonState.timeOffset || 0);

   for (let key of keys.split(',')) {
      let sensor = allSensors[key];
      let fact = valueFacts[key];
      let widget = listWidgetOf(key);
      let id = listId(key);

      if (!sensor || !fact)
         continue;

      let valid = sensor.valid !== false;

      $('#listRow' + id).toggleClass('listInvalid', !valid)
         .attr('title', valid ? null : 'ungültig (keine aktuellen Daten)');
      $('#listVal' + id).text(listValue(key));

      let age = sensor.last ? prettyDuration(Math.max(0, Math.round(now - sensor.last))) : '';
      // line break only between the parts ('vor 1 Minute' / '30 Sekunden')
      $('#listAge' + id).text(age ? ('vor ' + age).replace(/(vor|\d+) /g, '$1\u00a0') : '')
         .attr('title', sensor.last ? new Date(sensor.last * 1000).toLocaleString('de-DE') : null);

      // symbol: of a symbol widget as on the dashboard (widgetSymbolOf()), else by the unit

      let $sym = $('#listSym' + id);

      if (listSwitchTypes.includes(widget.widgettype)) {
         let sym = widgetSymbolOf(fact, widget, sensor);
         let switchable = $sym.attr('title') != null;      // rights, see listRow()

         if (sym.image) {
            $sym.attr('class', 'listSymbol listSymbolImg');
            if ($sym.find('img').attr('src') != sym.image)
               $sym.empty().append($('<img></img>').attr('src', sym.image));
         }
         else
            $sym.empty().attr('class', 'listSymbol ' + (sym.classes || 'mdi mdi-toggle-switch' + (sym.state ? '' : '-off-outline')));

         $sym.toggleClass('listSwitch', switchable).css('color', sym.color || '');
      }
      else if (!$sym.hasClass('mdi'))
         $sym.addClass('mdi ' + listTypeIcon(fact, widget));
   }
}

// the widget of the sensor: as on the current dashboard, else on the first dashboard showing it,
//   else the defaults of the fact (unit of the fact if the widget has none, as updateWidget())

function listWidgetOf(key)
{
   let fact = valueFacts[key] || {};
   let widget = null;

   if (dashboards && dashboards[actDashboard] && dashboards[actDashboard].widgets[key])
      widget = dashboards[actDashboard].widgets[key];
   else {
      for (let id in dashboards || {}) {
         if (dashboards[id].widgets && dashboards[id].widgets[key]) {
            widget = dashboards[id].widgets[key];
            break;
         }
      }
   }

   widget = Object.assign({}, fact.widget || {}, widget || {});

   if (widget.unit == null || widget.unit == '')
      widget.unit = fact.unit || '';

   return widget;
}

// 'mdi:mdi-lightbulb' -> 'mdi-lightbulb', else null

function listMdi(symbol)
{
   return symbol && symbol.startsWith('mdi:mdi-') ? symbol.substring(4) : null;
}

function listTypeIcon(fact, widget)
{
   if (listMdi(widget.symbol))
      return listMdi(widget.symbol);

   let unit = widget.unit != null ? widget.unit : fact.unit;

   if (unit == '°C')  return 'mdi-thermometer';
   if (unit == '%')   return 'mdi-percent-outline';
   if (unit == 'bar' || unit == 'hPa') return 'mdi-gauge';
   if (unit == 'W' || unit == 'kW' || unit == 'kWh') return 'mdi-flash-outline';
   if (unit == 'V' || unit == 'A')   return 'mdi-sine-wave';
   if (widget.widgettype == 2 || widget.widgettype == 7) return 'mdi-text-short';

   return 'mdi-circle-small';
}
