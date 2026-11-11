/*
 *  p4/menu.js
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  The tab 'Service-Menü' of the page of the Fröling S 3200 (p4/page.js),
 *  the items of one level with their current values, provided by the service p4mqttd (see p4service.c).
 *  The values are read from the control on each call, this takes a moment.
 *
 *  Users with the right 'settings' can change the parameters and the time ranges - the change is
 *  written to the heating control at once! - and read the menu, the sensors and the time programs again.
 */

var p4Parent = 1;          // the level of the menu which is shown

function p4MayWrite()
{
   let rights = localStorage.getItem(storagePrefix + 'Rights');

   return (rights & 0x08) || (rights & 0x10);
}

p4RegisterTab({ id: 'menu', title: 'Service-Menü', show: p4ShowMenu });

function p4ShowMenu()
{
   p4Parent = 1;
   $('#container').empty();
   p4MenuRequest(p4Parent);
}

function p4MenuRequest(parent)
{
   p4Parent = parent;
   showProgressDialog(60000);
   socket.send({ 'event': 'p4menu', 'object': { 'parent': parent } });
}

function p4Command(what, extra = {})
{
   showProgressDialog(what == 'initmenu' ? 900000 : 180000);   // closed when the menu level comes again
   socket.send({ 'event': 'p4cmd', 'object': Object.assign({ 'what': what, 'parent': p4Parent }, extra) });
}

function p4Esc(text)
{
   return $('<div></div>').text(text == null ? '' : String(text)).html();
}

function p4MenuReceived(menu)
{
   hideProgressDialog();

   if (currentPage != 'p4menu' || p4Tab != 'menu')
      return;

   if (menu.error) {
      $('#container').empty().append($('<div></div>').addClass('p4Menu')
                                     .append($('<div></div>').addClass('p4MenuError rounded-border').text(menu.error)));
      setupContainerHeight();
      return;
   }

   // the same structure and classes as the service menu of the p4d (menuButtonContainer, menuButton, ...)

   let $menu = $('<div></div>').attr('id', 'menuButtons').addClass('menuButtonContainer');

   // the way back (the title is the one of this level)

   if (menu.last > 0 || (menu.parent != 1 && menu.title)) {
      $menu.append($('<div></div>')
                   .addClass('menuButtonCaption rounded-border')
                   .text(menu.title)
                   .click(function() { p4MenuRequest(menu.last > 0 ? menu.last : 1); }));
   }

   for (const item of (menu.items || [])) {
      let $elem = $('<div></div>');
      let html = '<div style="display:flex;">' + p4Esc(item.title);

      if (item.child) {
         $elem.addClass('menuButton rounded-border')
            .click(function() { p4MenuRequest(item.child); });
      }
      else if (item.error) {
         html += ': <span style="color:var(--orange);">nicht lesbar</span>';
         $elem.addClass('menuButtonValue rounded-border');
      }
      else if (item.value != null) {
         if (item.type == 0x08) {                           // boolean parameter
            html += '<input id="chkState_' + item.address + '" class="input rounded-border" type="checkbox"' +
               (parseBool(item.value) ? ' checked' : '') + ' disabled><label for="chkState_' + item.address + '"></label>';
         }
         else {
            html += ': ' + p4Esc(item.value) + ' ' + p4Esc(item.unit);
         }

         $elem.addClass('menuButtonValue rounded-border');

         if (item.editable && p4MayWrite()) {
            $elem.removeClass().addClass('menuButtonValue menuButtonValueEditable rounded-border')
               .attr('title', 'Adresse 0x' + (item.address >>> 0).toString(16) + ', Typ 0x' + item.type.toString(16))
               .click(function() { p4Edit(item, menu.parent); });
         }
      }
      else {                                                // head of a group (e.g. a weekday of a time program)
         $elem.addClass('menuButtonHead rounded-border');
      }

      $elem.append(html + '</div>');
      $menu.append($elem);
   }

   if (!(menu.items || []).length)
      $menu.append($('<div></div>').addClass('menuButtonHead rounded-border').text('Keine Einträge'));

   // read again from the control

   if (p4MayWrite() && menu.parent == 1) {
      $menu.append($('<div></div>').addClass('p4MenuActions')
                   .append($('<button></button>').addClass('rounded-border button1')
                           .text('Menü und Sensoren neu lesen')
                           .attr('title', 'Liest die Menüstruktur und die Sensoren aus der Steuerung (dauert einige Zeit, z. B. nach einem Firmware Update)')
                           .click(function() {
                              confirmDialog(function() { p4Command('initmenu'); },
                                            'Menüstruktur und Sensoren neu aus der Steuerung lesen? Das dauert einige Zeit.', 'Lesen', 'Abbrechen');
                           }))
                   .append($('<button></button>').addClass('rounded-border button1')
                           .text('Zeitprogramme neu lesen')
                           .attr('title', 'Übernimmt die Zeitprogramme aus der Steuerung (wenn sie dort geändert wurden)')
                           .click(function() { p4Command('inittimes'); })));
   }

   $('#container').empty().append($menu);
   setupContainerHeight();
}

//***************************************************************************
// Edit
//***************************************************************************

function p4Edit(item, parent)
{
   if (item.range)
      return p4EditTimeRange(item, parent);

   let step = item.digits ? 1 / Math.pow(10, item.digits) : 1;
   let info = '(' + (item.default != null && item.type != 0x08 && item.type != 0x0a ? 'Standard ' + item.default + ' ' + item.unit + ', ' : '') +
       'Adresse 0x' + (item.address >>> 0).toString(16) + ', Typ 0x' + item.type.toString(16) + ')';
   let form = '<form id="p4Form"><div class="p4EditHint">Achtung: Die Änderung wird sofort in die Heizung geschrieben!</div>';

   if (item.type == 0x08) {
      form += '<div class="p4EditRow"><input id="p4Input" class="input rounded-border" type="checkbox" name="input"' +
         (parseBool(item.value) ? ' checked' : '') + '><label for="p4Input"></label></div>';
   }
   else if (item.type == 0x0a) {
      form += '<div class="p4EditRow"><input class="input rounded-border" type="time" step="60" name="input" value="' + item.value + '"/> Uhr</div>';
   }
   else {
      let value = String(item.value).replace(',', '.');
      form += '<div>Bereich: ' + item.min + ' - ' + item.max + ' ' + item.unit + '</div>' +
         '<div class="p4EditRow"><input class="input rounded-border" type="number" min="' + item.min + '" max="' + item.max +
         '" step="' + step + '" value="' + value + '" name="input"/> ' + item.unit + '</div>';
   }

   form += '<div class="p4EditInfo">' + info + '</div></form>';

   $(form).dialog({
      modal: true,
      width: "90%",
      maxWidth: 420,
      title: item.title,
      buttons: {
         'Speichern': function () {
            let value = $('input[name="input"]').val();

            if (item.type == 0x08)
               value = $('input[name="input"]').is(':checked') ? 'ja' : 'nein';

            if (value === '' || value == null)
               return;

            p4Command('parset', { 'address': item.address, 'value': String(value), 'parent': parent });
            $(this).dialog('close');
         },
         'Abbrechen': function () { $(this).dialog('close'); }
      },
      close: function() { $(this).dialog('destroy').remove(); }
   });
}

// a range of a time program: 'hh:mm - hh:mm' or 'nn:nn - nn:nn' (not set)

function p4EditTimeRange(item, parent)
{
   let unset = item.value.indexOf('nn:nn') >= 0;
   let parts = unset ? ['', ''] : item.value.split('-').map(function(s) { return s.trim(); });

   let form = '<form id="p4Form"><div class="p4EditHint">Achtung: Die Änderung wird sofort in die Heizung geschrieben!</div>' +
       '<div class="p4EditRow"><input class="input rounded-border" type="time" step="600" name="from" value="' + parts[0] + '"/>' +
       ' bis <input class="input rounded-border" type="time" step="600" name="to" value="' + parts[1] + '"/> Uhr</div>' +
       '<div class="p4EditInfo">(Adresse 0x' + (item.address >>> 0).toString(16) + ', Bereich ' + item.range +
       ', die Steuerung kennt nur 10 Minuten Schritte)</div></form>';

   function store(value, dlg) {
      p4Command('timeset', { 'address': item.address, 'range': item.range, 'value': value, 'parent': parent });
      $(dlg).dialog('close');
   }

   $(form).dialog({
      modal: true,
      width: "90%",
      maxWidth: 480,
      title: item.title,
      buttons: {
         'Speichern': function () {
            let from = $('input[name="from"]').val();
            let to = $('input[name="to"]').val();

            if (!from || !to)
               return;

            store(from + ' - ' + to, this);
         },
         'Löschen': function () { store('nn:nn - nn:nn', this); },
         'Abbrechen': function () { $(this).dialog('close'); }
      },
      close: function() { $(this).dialog('destroy').remove(); }
   });
}
