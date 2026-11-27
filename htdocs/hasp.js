/*
 *  hasp.js
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Setup page for the openHASP panel: which sensor is shown on which
 *  page and slot, and how (widget type, symbol). The daemon generates
 *  the panel objects from this configuration and sends them via MQTT.
 *
 *  Layout: a page has up to 4 rows, each row 1..3 widgets.
 *  Slot = row * 10 + column.
 */

var haspPages = {};          // filled by event 'hasppages'
var haspDeletedPages = [];   // ids removed in the UI, sent with 'action: delete' on save
var haspSavedState = null;   // the pages as stored (JSON of haspCollectPages()) -> unsaved changes on 'An Panel senden'
var haspNewPageId = -1;      // negative ids -> new pages
var haspMaxRows = 4;
var haspMaxCols = 3;

// initial widget options for a newly selected sensor: copy of the dashboard widget if the
// sensor is on a dashboard, else the valuefacts defaults. Afterwards the HASP options are
// independent of the dashboard and edited with the same dialog (widgetSetup).

function haspSeedOpts(key)
{
   if (key.startsWith('TIME:'))
      return { 'widgettype': 11, 'title': 'Uhrzeit' };

   if (key.startsWith('TIMER:'))
      return { 'widgettype': 17, 'title': 'Timer', 'minutes': 10, 'steps': '-1,1,5' };

   if (key.startsWith('WEATHER:'))
      return { 'widgettype': 19, 'title': '' };          // empty title -> the place of the forecast

   for (let did in dashboards) {
      let w = dashboards[did].widgets ? dashboards[did].widgets[key] : null;
      if (w) {
         let opts = JSON.parse(JSON.stringify(w));
         opts.widgettype = haspReducedType(opts.widgettype);
         return opts;
      }
   }

   let opts = { 'widgettype': 3 };

   if (valueFacts[key]) {
      if (valueFacts[key].widget)
         opts = JSON.parse(JSON.stringify(valueFacts[key].widget));
      opts.title = valueFacts[key].usrtitle ? valueFacts[key].usrtitle : valueFacts[key].title;
      if (valueFacts[key].unit)
         opts.unit = valueFacts[key].unit;
   }

   return opts;
}

// dashboard widget types without counterpart on the panel -> nearest panel type
// (keep in sync with Daemon::haspTypeOfDashboardType())

function haspReducedType(wt)
{
   switch (parseInt(wt)) {
      case 0: case 14:                  return 0;    // Symbol, SpecialSymbol
      case 2: case 7:                   return 2;    // Text, PlainText
      case 8:                           return 8;    // Choice (button matrix)
      case 4: case 5:                   return 5;    // Gauge, Meter -> Meter
      case 6: case 13:                  return 6;    // MeterLevel, ChartBar -> MeterLevel
      case 9: case 12:                  return 9;    // SymbolValue, SymbolText
      case 11:                          return 11;   // Time
      case 15:                          return 15;   // Level
      case 16:                          return 16;   // Balance
      case 17:                          return 17;   // Timer
      case 19:                          return 19;   // Weather
      default:                          return 3;    // Value, Chart, Space, ...
   }
}

// options of a stored slot: complete them once if they are partial (older versions stored
// only type/symbol/fillcolor), map the type to a panel type

function haspCompleteOpts(key, stored)
{
   let opts = stored;

   if (!('title' in stored) && !('unit' in stored)) {
      opts = haspSeedOpts(key);
      for (let k in stored)
         opts[k] = stored[k];
      if (stored.fillcolor && !stored.barcolor)
         opts.barcolor = stored.fillcolor;
      delete opts.fillcolor;
   }

   opts.widgettype = haspReducedType(opts.widgettype);
   return opts;
}

// name of the (panel) widget type for the slot label; unknown dashboard types are shown as 'Value'

function haspTypeName(widgettype)
{
   let types = haspPages.widgettypes ? haspPages.widgettypes : {};

   for (let name in types)
      if (types[name] == widgettype)
         return name;

   return 'Value';
}

function initHaspSetup()
{
   prepareSetupMenu();
   haspDeletedPages = [];
   haspNewPageId = -1;

   showControlContainer();
   $('#container').removeClass('hidden');

   // tool panel on the left like the sensor setup

   // one button per row, icon plus text (the tool panel stays narrow)

   let toolButton = function(icon, text, title, onClick) {
      return $('<div></div>').append($('<button></button>')
                                     .addClass('rounded-border tool-button mdi mdi-' + icon)
                                     .html(' ' + text)
                                     .attr('title', title)
                                     .css('width', '100%')
                                     .click(onClick));
   };

   $("#controlContainer")
      .empty()
      .append(toolButton('content-save', 'Speichern', 'Seiten speichern und an das Panel senden',
                         function() { storeHaspPages(); }))
      .append($('<div></div>')
              .addClass('button-group-spacing'))
      .append(toolButton('send', 'An Panel senden', 'Den gespeicherten Stand erneut an das Panel senden (z.B. nach einem Neustart des Panels), speichert nicht',
                         function() { haspSendStored(); }))
      .append(toolButton('lightbulb-on-outline', 'Panel wecken', 'Display einschalten und den Leerlauf-Zyklus (Dimmen / Aus) neu starten (sendet idle off)',
                         function() { socket.send({ "event": "hasppages", "object": { "action": "wake" } }); }))
      .append(toolButton('camera', 'Screenshot', 'Aktuelles Bild des Panels anzeigen (vom Web-Server des Panels)',
                         function() { haspScreenshot(); }))
      .append(toolButton('restart', 'Panel booten', 'Das Panel rebooten, die Seiten werden danach automatisch neu gesendet',
                         function() { if (confirm('Das Panel jetzt neu starten?')) socket.send({ "event": "hasppages", "object": { "action": "reboot" } }); }))
      .append(toolButton('upload', 'Dateien hochladen', 'Die Skripte (idle_*.cmd) und die MDI Schrift (mdi.ttf) auf das Panel laden, das Display ist währenddessen aus, danach startet das Panel neu',
                         function() { haspUploadFiles(); }))

      .append($('<div></div>')
              .addClass('button-group-spacing'))
      .append($('<button></button>')
              .addClass('rounded-border tool-button')
              .html('+ Seite')
              .click(function() { haspAddPage(); }))
      .append($('<div></div>')
              .addClass('button-group-spacing'))
      .append($('<div></div>')
              .css({ 'display': 'flex', 'gap': '6px', 'align-items': 'center', 'margin-bottom': '4px' })
              .append($('<input></input>')
                      .attr('id', 'haspIdleShort')
                      .attr('type', 'number').attr('min', 0).attr('step', 1)
                      .attr('title', 'kurz: nach dieser Zeit ohne Berührung dunkelt das Panel ab (Skript idle_short.cmd im Panel), 0 = aus')
                      .addClass('rounded-border input')
                      .css('width', '70px')
                      .val(haspPages.idleShort != null ? haspPages.idleShort : 0))
              .append($('<span></span>').html('abdunkeln [s]')))
      .append($('<div></div>')
              .css({ 'display': 'flex', 'gap': '6px', 'align-items': 'center', 'margin-bottom': '8px' })
              .append($('<input></input>')
                      .attr('id', 'haspIdleLong')
                      .attr('type', 'number').attr('min', 0).attr('step', 1)
                      .attr('title', 'lang: nach dieser Zeit ohne Berührung schaltet das Panel das Display aus (Skript idle_long.cmd im Panel), 0 = aus')
                      .addClass('rounded-border input')
                      .css('width', '70px')
                      .val(haspPages.idleLong != null ? haspPages.idleLong : 0))
              .append($('<span></span>').html('Display aus [s]')))
      .append($('<div></div>')
              .addClass('labelB1')
              .html('Filter'))
      .append($('<input></input>')
              .attr('id', 'haspFilter')
              .attr('placeholder', 'Name oder Typ ...')
              .attr('type', 'search')
              .addClass('input rounded-border clearableOD')
              .css('width', '-webkit-fill-available')
              .css('width', '-moz-available')
              .css('margin-bottom', '8px')
              .val(sessionStorage.getItem(storagePrefix + 'haspFilter') || '')     // kept for the browser session only
              .on('input', function() { sessionStorage.setItem(storagePrefix + 'haspFilter', $(this).val()); haspApplyFilter(); }))
      .append($('<div></div>')
              .addClass('button-group-spacing'))
      .append($('<button></button>')
              .addClass('rounded-border tool-button')
              .html('Hilfe')
              .attr('title', 'Beschreibung im README')
              .click(function() { showHelp('doc/hasp.md#hasp-panel'); }));

   if (!haspPages.topic || haspPages.topic == '')
      $('#controlContainer').append($('<div></div>').addClass('labelB1').css('color', 'var(--peakColor)')
                                    .html('Kein MQTT Topic konfiguriert (Konfiguration -> HASP Panel)'));

   if (haspPages.fontOutdated)
      $('#controlContainer').append($('<div></div>').addClass('labelB1').css({ 'color': 'var(--peakColor)', 'white-space': 'normal' })
                                    .attr('title', 'Die Seiten verwenden MDI Symbole, die in der Schrift auf dem Panel fehlen - sie bleiben dort unsichtbar')
                                    .html('Neue Symbole - bitte Dateien hochladen'));

   $('#controlContainer')
      .append($('<div></div>')
              .addClass('button-group-spacing'))
      .append($('<div></div>').attr('id', 'haspMemory'));

   $('#container').empty()
      .append($('<div></div>').attr('id', 'haspLog'))
      .append($('<div></div>').attr('id', 'haspPages'));

   let pages = haspPages.pages ? haspPages.pages : {};
   let ids = Object.keys(pages).sort(function(a, b) { return pages[a].order - pages[b].order; });

   for (let i = 0; i < ids.length; i++)
      haspRenderPage(ids[i], pages[ids[i]]);

   haspSavedState = JSON.stringify(haspCollectPages());
   haspRenderStatus();

   $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('footer') - sab - 10);
   window.onresize = function() {
      $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('footer') - sab - 10);
   };
}

// one page: header (title, move, delete, + Zeile) and the rows

function haspRenderPage(id, page)
{
   // .inputTableConfig is 'display: table' and gives its direct child divs 'inline-grid; width: 48%',
   // therefore block display for the page and full width for header and rows container

   let div = $('<div></div>')
       .attr('id', 'haspPage_' + id)
       .attr('data-id', id)
       .addClass('rounded-border inputTableConfig haspPage')
       .css({ 'display': 'block', 'margin': '8px 0' });

   let header = $('<div></div>').css({ 'display': 'flex', 'width': '100%', 'box-sizing': 'border-box', 'align-items': 'center', 'gap': '8px', 'flex-wrap': 'wrap', 'white-space': 'nowrap' });

   header
      .append($('<input></input>')
              .attr('id', 'haspTitle_' + id)
              .addClass('rounded-border input')
              .attr('type', 'text')
              .css('width', '200px')
              .val(page.title))
      .append($('<span></span>').html('Kachelfarbe').css('color', 'var(--neutral4)'))
      .append($('<input></input>')
              .attr('id', 'haspCardColor_' + id)
              .attr('type', 'color')
              .attr('title', 'Farbe der Kacheln dieser Seite auf dem Panel')
              .css({ 'width': '40px', 'height': '28px', 'padding': '0', 'border': 'none', 'background': 'none', 'cursor': 'pointer' })
              .val(page.cardColor ? page.cardColor : '#272727')
              .on('input change', function() { $('#haspPage_' + id + ' .haspSlot').css('background', $(this).val()); }))
      .append($('<button></button>').addClass('rounded-border buttonOptions').html('&#9650;').attr('title', 'Seite nach oben')
              .click(function() { let p = $('#haspPage_' + id); p.prev('.haspPage').before(p); }))
      .append($('<button></button>').addClass('rounded-border buttonOptions').html('&#9660;').attr('title', 'Seite nach unten')
              .click(function() { let p = $('#haspPage_' + id); p.next('.haspPage').after(p); }))
      .append($('<button></button>').addClass('rounded-border buttonOptions').html('+ Zeile').css('margin-left', 'auto')
              .click(function() {
                 if ($('#haspPage_' + id + ' .haspRow').length < haspMaxRows)
                    haspRenderRow(id, $('#haspPage_' + id + ' .haspRow').length, 3, {});
              }))
      .append($('<button></button>').addClass('rounded-border buttonOptions mdi mdi-delete-outline').attr('title', 'Seite löschen').css('font-size', 'large')
              .click(function() {
                 if (confirm('Seite "' + $('#haspTitle_' + id).val() + '" löschen?')) {
                    if (id >= 0)
                       haspDeletedPages.push(id);
                    $('#haspPage_' + id).remove();
                 }
              }));

   div.append(header);
   div.append($('<div></div>').attr('id', 'haspRows_' + id).css({ 'display': 'block', 'width': '100%', 'box-sizing': 'border-box' }));
   $('#haspPages').append(div);

   let layout = page.layout && page.layout.length ? page.layout : [3, 3];
   let widgets = page.widgets ? page.widgets : {};

   for (let r = 0; r < layout.length && r < haspMaxRows; r++)
      haspRenderRow(id, r, layout[r], widgets);

   $('#haspPage_' + id + ' .haspSlot').css('background', $('#haspCardColor_' + id).val());   // the cards in the color of the page
}

// one row: number of columns select, delete button and the slots

function haspRenderRow(pageId, row, cols, widgets)
{
   let rowDiv = $('<div></div>')
       .addClass('haspRow')
       .css({ 'display': 'flex', 'align-items': 'flex-start', 'gap': '8px', 'margin-top': '6px' });

   // select and delete button stacked with the same width

   let ctrl = $('<div></div>').css({ 'display': 'flex', 'flex-direction': 'column', 'gap': '4px', 'width': '70px', 'flex': '0 0 70px', 'margin-right': '6px', 'align-self': 'center' })
       .append(haspSelect(null, { '1': 1, '2': 2, '3': 3 }, cols, function() {
          let current = haspCollectRowWidgets(rowDiv);
          haspRenderSlots(rowDiv, parseInt($(this).val()), current);
       }).addClass('haspCols').attr('title', 'Anzahl Widgets in dieser Zeile').css({ 'width': '100%', 'box-sizing': 'border-box', 'margin': '0' }))
       .append($('<button></button>').addClass('rounded-border buttonOptions mdi mdi-delete-outline').attr('title', 'Zeile löschen')
               .css({ 'font-size': 'large', 'width': '100%', 'min-width': '0', 'box-sizing': 'border-box', 'margin': '0' })
               .click(function() { rowDiv.remove(); }));

   rowDiv.append(ctrl);
   rowDiv.append($('<div></div>').addClass('haspSlots').css({ 'display': 'flex', 'gap': '8px', 'flex': '1' }));
   $('#haspRows_' + pageId).append(rowDiv);

   // widgets of this row from the stored config (slot = row * 10 + col)

   let rowWidgets = {};
   for (let c = 0; c < haspMaxCols; c++) {
      let w = widgets[row * 10 + c];
      if (w) {
         let opts = JSON.parse(JSON.stringify(w));
         delete opts.key;
         rowWidgets[c] = { 'key': w.key, 'opts': haspCompleteOpts(w.key, opts) };
      }
   }

   haspRenderSlots(rowDiv, cols, rowWidgets);
}

// the slots of a row: sensor select, type label and the 'Setup' button (widget dialog)

function haspRenderSlots(rowDiv, cols, rowWidgets)
{
   let slots = rowDiv.find('.haspSlots');
   let cardColor = rowDiv.closest('.haspPage').find('input[type=color]').val() || '#272727';
   slots.empty();

   for (let c = 0; c < cols; c++) {
      let widget = rowWidgets[c] ? rowWidgets[c] : {};
      let slot = $('<div></div>')
          .addClass('haspSlot')
          .attr('data-col', c)
          .css({ 'flex': '1', 'padding': '8px', 'background': cardColor, 'border': '1px solid rgb(137 104 4)', 'border-radius': '6px',
                 'box-shadow': '1px 1px 2px 0px var(--shadow)', 'display': 'flex', 'flex-direction': 'column', 'gap': '6px', 'min-height': '70px' });

      slot.data('opts', widget.opts ? widget.opts : (widget.key ? haspSeedOpts(widget.key) : null));

      let keySel = haspSensorSelect(widget.key ? widget.key : '').addClass('haspKey');
      let titleLabel = $('<div></div>').addClass('haspTitleLabel')
          .css({ 'color': '#ffffff', 'font-size': '1.1em', 'white-space': 'nowrap', 'overflow': 'hidden', 'text-overflow': 'ellipsis', 'min-height': '1.3em' });
      let typeLabel = $('<span></span>').addClass('haspTypeLabel').css('color', 'var(--neutral4)');
      let keyLabel = $('<span></span>').addClass('haspKeyLabel').css({ 'color': 'var(--neutral3)', 'margin-left': 'auto', 'margin-right': '8px' });
      let setupBtn = $('<button></button>').addClass('rounded-border buttonOptions').html('Setup');
      let line = $('<div></div>').css({ 'display': 'flex', 'align-items': 'center', 'gap': '6px' })
          .append(typeLabel).append(keyLabel).append(setupBtn);

      let refresh = function() {
         let opts = slot.data('opts');
         let key = keySel.val();
         typeLabel.html(opts ? haspTypeName(opts.widgettype) : '');
         keyLabel.html(key ? key : '');
         titleLabel.text(haspCardTitle(key, opts));
         setupBtn.css('visibility', opts ? 'visible' : 'hidden');
      };

      // new sensor selected -> seed the options once (dashboard widget / defaults)

      keySel.change(function() {
         let key = $(this).val();
         slot.data('opts', key != '' ? haspSeedOpts(key) : null);
         refresh();
      });

      setupBtn.click(function() {
         let key = keySel.val();
         if (key == '' || !slot.data('opts'))
            return;
         widgetSetup(key, {
            'widget': slot.data('opts'),
            'types': haspPages.widgettypes ? haspPages.widgettypes : {},
            'onSave': function(w) { slot.data('opts', w); refresh(); },
            'onDelete': function() { keySel.val(''); slot.data('opts', null); refresh(); }
         });
      });

      refresh();
      slot.append(titleLabel).append(keySel).append(line);
      slots.append(slot);
   }
}

// select with active sensors, sorted by type then name, filtered by #haspFilter

// the title as shown on the panel: the one of the widget, otherwise the one of the sensor (like hasp.c)

function haspCardTitle(key, opts)
{
   if (!key)
      return '';

   if (opts && opts.title)
      return opts.title;

   if (key.startsWith('TIME:'))
      return 'Uhrzeit';

   if (key.startsWith('TIMER:'))
      return 'Timer';

   if (key.startsWith('WEATHER:'))
      return 'Wetter';

   let fact = valueFacts[key];

   return fact ? (fact.usrtitle ? fact.usrtitle : fact.title) : key;
}

// the weather card needs the forecast of OpenWeatherMap (sensor WEA:0x01, configuration openWeatherApiKey)

function haspWeatherAvailable()
{
   return valueFacts['WEA:0x01'] != null && valueFacts['WEA:0x01'].state;
}

function haspSensorSelect(selected)
{
   let sel = $('<select></select>').addClass('rounded-border input').css('max-width', '100%');
   haspFillSensorSelect(sel, selected);
   return sel;
}

function haspFillSensorSelect(sel, selected)
{
   let filter = $('#haspFilter').val();
   let expression = null;

   if (filter && filter != '') {
      try { expression = new RegExp(filter, 'i'); }
      catch (e) { expression = new RegExp(filter.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'i'); }   // no valid regex -> plain text
   }

   sel.empty();
   sel.append($('<option></option>').val('').html('- leer -'));
   sel.append($('<option></option>').val('TIME:0x01').html('Uhrzeit').attr('selected', selected == 'TIME:0x01'));
   sel.append($('<option></option>').val('TIMER:0x01').html('Timer').attr('selected', selected == 'TIMER:0x01'));

   if (haspWeatherAvailable() || selected == 'WEATHER:0x01')    // the card shows the forecast of WEA:0x01
      sel.append($('<option></option>').val('WEATHER:0x01').html('Wetter').attr('selected', selected == 'WEATHER:0x01'));

   let jArray = [];

   for (let key in valueFacts) {
      if (!valueFacts[key].state)   // active facts only
         continue;

      let fact = valueFacts[key];
      let title = fact.usrtitle ? fact.usrtitle : fact.title;

      if (expression && key != selected && !expression.test(title) && !expression.test(fact.type) && !expression.test(fact.name ? fact.name : ''))
         continue;

      jArray.push([key, fact, title]);
   }

   jArray.sort(function(a, b) {
      let t = a[1].type.localeCompare(b[1].type);
      if (t != 0) return t;
      return a[2].toLowerCase().localeCompare(b[2].toLowerCase());
   });

   for (let i = 0; i < jArray.length; i++) {
      let key = jArray[i][0];
      let text = jArray[i][1].type + ': ' + jArray[i][2];
      if (jArray[i][1].unit != null && jArray[i][1].unit != '')
         text += ' [' + jArray[i][1].unit + ']';
      sel.append($('<option></option>').val(key).html(text).attr('selected', key == selected));
   }

   // keep a configured sensor even if it is not active (anymore)

   if (selected != '' && valueFacts[selected] == null && !selected.startsWith('TIME:') && !selected.startsWith('TIMER:') && !selected.startsWith('WEATHER:'))
      sel.append($('<option></option>').val(selected).html(selected + ' (inaktiv)').attr('selected', true));
}

function haspApplyFilter()
{
   $('#haspPages select.haspKey').each(function() {
      haspFillSensorSelect($(this), $(this).val());
   });
}

function haspSelect(elementId, options, selected, onChange)
{
   let sel = $('<select></select>').addClass('rounded-border input');

   if (elementId)
      sel.attr('id', elementId);

   for (let name in options)
      sel.append($('<option></option>').val(options[name]).html(name).attr('selected', options[name] == selected));

   if (onChange)
      sel.change(onChange);

   return sel;
}

// memory of the panel (tool panel) and its warnings / errors (above the pages), from the
//  events 'hasppages' and 'haspstatus'. Internal RAM: WLAN, MQTT, tasks, ... (short below ~40 KB),
//  PSRAM: the LVGL objects of the pages and the fonts (firmware patch, see openHASP/README.md)

function haspRenderStatus()
{
   let mem = haspPages.memory || {};
   let kb = function(b) { return (b / 1024).toFixed(0) + ' KB'; };
   let mb = function(b) { return (b / 1024 / 1024).toFixed(2) + ' MB'; };
   let warnColor = 'var(--peakColor)';
   let box = $('#haspMemory').empty();

   let row = function(label, value, title, warn) {
      box.append($('<div></div>')
                 .css({ 'display': 'flex', 'justify-content': 'space-between', 'gap': '6px' })
                 .attr('title', title)
                 .append($('<span></span>').html(label).css('color', 'var(--neutral4)'))
                 .append($('<span></span>').html(value).css('color', warn ? warnColor : '')));
   };

   box.append($('<div></div>')
              .addClass('labelB1')
              .css({ 'display': 'flex', 'justify-content': 'space-between', 'align-items': 'center' })
              .append($('<span></span>').html('Speicher Panel'))
              .append($('<button></button>')
                      .addClass('rounded-border buttonOptions mdi mdi-refresh')
                      .attr('title', 'Aktuelle Werte beim Panel anfordern')
                      .click(function() { socket.send({ "event": "hasppages", "object": { "action": "statusupdate" } }); })));

   if (!mem.at) {
      box.append($('<div></div>').css('color', 'var(--neutral4)').html('noch keine Werte'));
      return haspRenderLog();
   }

   if (mem.heapFree != null)
      row('RAM frei', kb(mem.heapFree), 'Freier interner RAM (WLAN, MQTT, Tasks), knapp unter etwa 40 KB', mem.heapFree < 40 * 1024);
   if (mem.heapMaxBlock != null)
      row('größter Block', kb(mem.heapMaxBlock), 'Größter zusammenhängender freier Block im internen RAM', mem.heapMaxBlock < 20 * 1024);
   if (mem.heapFrag != null)
      row('Fragmentierung', mem.heapFrag + ' %', 'Fragmentierung des internen RAM', mem.heapFrag > 50);

   if (mem.psramSize != null) {
      let used = mem.psramSize - mem.psramFree;
      row('PSRAM frei', mb(mem.psramFree), 'Freier externer RAM (PSRAM: Objekte der Seiten, Schriften), belegt ' + mb(used) + ' von ' + mb(mem.psramSize),
          mem.psramFree < mem.psramSize / 10);
      box.append($('<div></div>')
                 .css({ 'height': '6px', 'background': 'var(--neutral2)', 'border-radius': '3px', 'margin': '2px 0 4px 0' })
                 .append($('<div></div>')
                         .css({ 'height': '100%', 'border-radius': '3px', 'background': 'var(--blue)',
                                'width': Math.min(100, Math.round(used * 100 / mem.psramSize)) + '%' })));
   }
   else
      box.append($('<div></div>').css({ 'color': 'var(--neutral4)', 'white-space': 'normal' })
                 .html('PSRAM: Firmware ohne Patch von homectld'));

   let up = mem.uptime != null ? mem.uptime : 0;
   let upText = up >= 86400 ? Math.floor(up / 86400) + ' d' : up >= 3600 ? Math.floor(up / 3600) + ' h' : Math.floor(up / 60) + ' min';

   box.append($('<div></div>')
              .css({ 'color': 'var(--neutral4)', 'font-size': 'smaller', 'margin-top': '2px' })
              .html('Stand ' + new Date(mem.at * 1000).toLocaleTimeString() + ', läuft seit ' + upText));

   haspRenderLog();
}

function haspRenderLog()
{
   let log = haspPages.log || [];
   let box = $('#haspLog').empty();

   if (!log.length)
      return;

   let div = $('<div></div>')
       .addClass('rounded-border inputTableConfig')
       .css({ 'display': 'block', 'margin': '8px 0' });

   div.append($('<div></div>')
              .css({ 'display': 'flex', 'align-items': 'center', 'gap': '8px', 'width': '100%', 'box-sizing': 'border-box' })
              .append($('<span></span>').addClass('labelB1').html('Meldungen des Panels (' + log.length + ')'))
              .append($('<button></button>')
                      .addClass('rounded-border buttonOptions')
                      .css('margin-left', 'auto')
                      .html('Leeren')
                      .attr('title', 'Liste leeren (nur in homectld, das Panel ist nicht betroffen)')
                      .click(function() { socket.send({ "event": "hasppages", "object": { "action": "clearlog" } }); })));

   let list = $('<div></div>').css({ 'max-height': '160px', 'overflow-y': 'auto', 'width': '100%', 'font-family': 'monospace', 'font-size': 'smaller' });

   for (let i = log.length - 1; i >= 0; i--) {         // newest first
      let e = log[i];
      list.append($('<div></div>')
                  .css({ 'white-space': 'normal', 'word-break': 'break-all', 'color': e.level == 'error' ? 'var(--peakColor)' : '#e0a030' })
                  .text(new Date(e.time * 1000).toLocaleTimeString() + '  ' + e.tag + ': ' + e.text));
   }

   box.append(div.append(list));
}

// upload the panel files (the list comes with the event 'hasppages': <confDir>/hasp/*.cmd and mdi.ttf)

function haspUploadFiles()
{
   let files = haspPages.uploadFiles || [];

   if (!files.length) {
      showInfoDialog({ 'message': 'Keine Dateien zum Hochladen gefunden (/etc/homectld/hasp/*.cmd, MDI Schrift)', 'status': -1 });
      return;
   }

   confirmDialog(function() { socket.send({ "event": "hasppages", "object": { "action": "upload" } }); },
                 'Diese Dateien auf das Panel laden?<br/><ul><li>' + files.join('</li><li>') + '</li></ul>' +
                 'Das Display ist währenddessen aus, danach startet das Panel neu.',
                 'Hochladen', 'Abbrechen');
}

// screenshot of the panel in a dialog: the image comes from the web server of the panel
//  (/screenshot?q=<n>, q only defeats the cache; a=prev / a=next switch the page on the panel
//  before the shot, like the panel's own screenshot page), the IP from its statusupdate

function haspScreenshot()
{
   if (!haspPages.ip || haspPages.ip == '') {
      socket.send({ "event": "hasppages", "object": { "action": "status" } });
      return;
   }

   let url = 'http://' + haspPages.ip + '/screenshot?';
   let img = $('<img></img>')
       .css({ 'width': '100%', 'display': 'block', 'background': '#000' })
       .attr('alt', 'Screenshot')
       .on('error', function() { note.html('Kein Bild vom Panel (' + haspPages.ip + ' nicht erreichbar' + (window.location.href.startsWith('https') ? ', oder die Seite läuft über https und das Panel nur über http' : '') + ')'); })
       .on('load', function() { note.html(''); });
   let note = $('<div></div>').css({ 'color': 'var(--neutral4)', 'margin-top': '4px', 'min-height': '1.2em' });
   let form = $('<div></div>').append(img).append(note);
   let load = function(action) {               // action 'prev' / 'next' switches the page on the panel itself
      note.html('lade ...');
      img.attr('src', url + (action ? 'a=' + action + '&' : '') + 'q=' + Date.now());
   };

   form.dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      hide: 'fade',
      width: Math.min(840, window.innerWidth - 20),
      title: 'Panel ' + haspPages.ip,
      open: function() { load(); },
      buttons: {
         '◀ Seite': function() { load('prev'); },
         'Aktualisieren': function() { load(); },
         'Seite ▶': function() { load('next'); },
         'Schließen': function() { $(this).dialog('close'); }
      },
      close: function() { $(this).dialog('destroy').remove(); }
   });
}

// widgets of one row: col -> { key, widgettype, symbol }

function haspCollectRowWidgets(rowDiv)
{
   let widgets = {};

   rowDiv.find('.haspSlot').each(function() {
      let key = $(this).find('.haspKey').val();
      if (key == '')
         return;
      widgets[$(this).data('col')] = { 'key': key, 'opts': $(this).data('opts') ? $(this).data('opts') : {} };
   });

   return widgets;
}

function haspAddPage()
{
   let id = haspNewPageId--;
   haspRenderPage(id, { 'title': 'Seite ' + ($('.haspPage').length + 1), 'layout': [3, 3], 'widgets': {} });
}

// 'An Panel senden' sends the stored pages - with unsaved changes ask to save them first
//   (saving sends them anyway)

function haspSendStored()
{
   if (haspSavedState == null || (JSON.stringify(haspCollectPages()) == haspSavedState && !haspDeletedPages.length)) {
      socket.send({ "event": "hasppages", "object": { "action": "send" } });
      return;
   }

   confirmDialog(function() { storeHaspPages(); },
                 'Es gibt ungespeicherte Änderungen, gesendet wird nur der gespeicherte Stand.<br/><br/>' +
                 'Änderungen speichern und an das Panel senden?',
                 'Speichern und senden', 'Abbrechen');
}

// the pages of the UI as object for 'storehasppages' (without the deleted ones)

function haspCollectPages()
{
   let jsonObj = {};
   let order = 0;

   $('#haspPages .haspPage').each(function() {
      let id = $(this).data('id');
      let layout = [];
      let widgets = {};

      $(this).find('.haspRow').each(function(row) {
         layout.push(parseInt($(this).find('.haspCols').val()));
         let rowWidgets = haspCollectRowWidgets($(this));
         for (let col in rowWidgets)
            widgets[row * 10 + parseInt(col)] = rowWidgets[col];
      });

      if (layout.length == 0)
         layout = [3];

      jsonObj[id] = {
         'title': $('#haspTitle_' + id).val(),
         'cardColor': $('#haspCardColor_' + id).val(),
         'layout': layout,
         'order': order++,
         'widgets': widgets
      };
   });

   jsonObj['config'] = { 'idleShort': parseInt($('#haspIdleShort').val()) || 0, 'idleLong': parseInt($('#haspIdleLong').val()) || 0 };

   return jsonObj;
}

window.storeHaspPages = function()
{
   let jsonObj = haspCollectPages();
   haspSavedState = JSON.stringify(jsonObj);

   for (let i = 0; i < haspDeletedPages.length; i++)
      jsonObj[haspDeletedPages[i]] = { 'action': 'delete' };

   socket.send({ "event": "storehasppages", "object": jsonObj });
   haspDeletedPages = [];
}
