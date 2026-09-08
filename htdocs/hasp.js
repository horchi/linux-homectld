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
      case 2: case 7: case 8:           return 2;    // Text, PlainText, Choice
      case 4: case 5:                   return 5;    // Gauge, Meter -> Meter
      case 6: case 13:                  return 6;    // MeterLevel, ChartBar -> MeterLevel
      case 9: case 12:                  return 9;    // SymbolValue, SymbolText
      case 11:                          return 11;   // Time
      case 15:                          return 15;   // Level
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

   $("#controlContainer")
      .empty()
      .append($('<div></div>')
              .append($('<button></button>')
                      .addClass('rounded-border tool-button')
                      .html('Speichern')
                      .click(function() { storeHaspPages(); }))
              .append($('<button></button>')
                      .addClass('rounded-border tool-button')
                      .html('An Panel senden')
                      .click(function() { socket.send({ "event": "hasppages", "object": { "action": "send" } }); })))
      .append($('<button></button>')
              .addClass('rounded-border tool-button')
              .html('+ Seite')
              .click(function() { haspAddPage(); }))
      .append($('<div></div>')
              .addClass('button-group-spacing'))
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
              .on('input', function() { haspApplyFilter(); }))
      .append($('<div></div>')
              .addClass('button-group-spacing'))
      .append($('<button></button>')
              .addClass('rounded-border tool-button')
              .html('Hilfe')
              .attr('title', 'Beschreibung im README')
              .click(function() { showHelp('hasp-panel'); }));

   if (!haspPages.topic || haspPages.topic == '')
      $('#controlContainer').append($('<div></div>').addClass('labelB1').css('color', 'var(--peakColor)')
                                    .html('Kein MQTT Topic konfiguriert (Konfiguration -> HASP Panel)'));

   $('#container').empty().append($('<div></div>').attr('id', 'haspPages'));

   let pages = haspPages.pages ? haspPages.pages : {};
   let ids = Object.keys(pages).sort(function(a, b) { return pages[a].order - pages[b].order; });

   for (let i = 0; i < ids.length; i++)
      haspRenderPage(ids[i], pages[ids[i]]);

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
   slots.empty();

   for (let c = 0; c < cols; c++) {
      let widget = rowWidgets[c] ? rowWidgets[c] : {};
      let slot = $('<div></div>')
          .addClass('haspSlot')
          .attr('data-col', c)
          .css({ 'flex': '1', 'padding': '8px', 'background': '#272727', 'border': '1px solid rgb(137 104 4)', 'border-radius': '6px',
                 'box-shadow': '1px 1px 2px 0px var(--shadow)', 'display': 'flex', 'flex-direction': 'column', 'gap': '6px', 'min-height': '70px' });

      slot.data('opts', widget.opts ? widget.opts : (widget.key ? haspSeedOpts(widget.key) : null));

      let keySel = haspSensorSelect(widget.key ? widget.key : '').addClass('haspKey');
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
      slot.append(keySel).append(line);
      slots.append(slot);
   }
}

// select with active sensors, sorted by type then name, filtered by #haspFilter

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

   if (selected != '' && valueFacts[selected] == null && !selected.startsWith('TIME:'))
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

window.storeHaspPages = function()
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
         'layout': layout,
         'order': order++,
         'widgets': widgets
      };
   });

   for (let i = 0; i < haspDeletedPages.length; i++)
      jsonObj[haspDeletedPages[i]] = { 'action': 'delete' };

   socket.send({ "event": "storehasppages", "object": jsonObj });
}
