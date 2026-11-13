/*
 *  alerts.js
 *
 *  (c) 2020-2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  The setup page 'Alerts': a card per sensor alert, the sensor is selected from the
 *  value facts (type and sensor), the styles are in alerts.css
 */

// the placeholders of subject and body (sensorAlertCheck / performAlertCheck of the daemon)

var alertPlaceholders = [ '%title%', '%value%', '%unit%', '%sensorid%', '%min%', '%max%', '%minv%', '%maxv%',
                          '%delta%', '%range%', '%repeat%', '%time%', '%weburl%' ];

function initAlerts(alerts)
{
   $('#container').removeClass('hidden');
   $('#container').empty();

   prepareSetupMenu();

   $("#navMenu")
      .append($('<div></div>')
              .attr('id', 'confirmDiv')
              .addClass('confirmDiv')
              .append($('<button></button>')
                      .addClass('rounded-border buttonOptions')
                      .html('Speichern')
                      .click(function() { storeAlerts(); })))

   let list = $('<div></div>').addClass('alrList');

   $('#container').append($('<div></div>')
                          .addClass('alrPage')
                          .append($('<div></div>').addClass('alrPageHead')
                                  .append($('<span></span>').addClass('mdi mdi-bell-ring-outline'))
                                  .append($('<span></span>').text('Sensor Alerts'))
                                  .append($('<span></span>').addClass('alrCount').text(alerts.length)))
                          .append(list));

   for (let i = 0; i < alerts.length; i++)
      list.append(alertCard(alerts[i]));

   list.append(alertCard({ "id": -1, "type": "", "address": "", "state": "D", "min": "", "max": "", "rangem": "",
                           "delta": "", "maddress": "", "msubject": "", "mbody": "", "maxrepeat": "" }));

   // calc container size

   $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('footer') - sab - 10);
   window.onresize = function() {
      $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('footer') - sab - 10);
   };
}

// the sensor types with at least one value fact, title of the value type

function alertSensorTypes()
{
   let types = {};

   for (let key in valueFacts)
      types[valueFacts[key].type] = valueFacts[key].type;

   for (let vt of valueTypes)
      if (types[vt.type])
         types[vt.type] = vt.title + ' (' + vt.type + ')';

   return Object.keys(types).sort(function(a, b) { return types[a].localeCompare(types[b]); })
      .map(function(t) { return { type: t, title: types[t] }; });
}

function alertSensorTitle(fact)
{
   return (fact.usrtitle ? fact.usrtitle : fact.title).split('\n')[0];
}

// one alert; the inputs carry the 'name' of the column, storeAlerts() collects them per card ('row_<id>')

function alertCard(item)
{
   let isNew = item.id == -1;
   let key = item.type != '' && item.address !== '' ? toKey(item.type, parseInt(item.address)) : null;
   let fact = key ? valueFacts[key] : null;
   let known = !key || fact != null;

   let field = function(label, input, unit = null, cls = '') {
      return $('<label></label>').addClass('alrField ' + cls)
         .append($('<span></span>').addClass('alrLabel').text(label))
         .append($('<span></span>').addClass('alrInput').append(input)
                 .append(unit ? $('<span></span>').addClass('alrUnit').text(unit) : null));
   };

   let num = function(name, value, placeholder = '') {
      return $('<input></input>').attr({ type: 'number', name: name, placeholder: placeholder })
         .addClass('rounded-border input').val(value);
   };

   let text = function(name, value) {
      return $('<input></input>').attr({ type: 'text', name: name }).addClass('rounded-border input').val(value);
   };

   // sensor: type and the sensors of the type

   let typeSel = $('<select></select>').attr('name', 'type').addClass('rounded-border input');
   let addrSel = $('<select></select>').attr('name', 'address').addClass('rounded-border input');
   let types = alertSensorTypes();

   typeSel.append($('<option></option>').val('').text('– Typ –'));

   for (let t of types)
      typeSel.append($('<option></option>').val(t.type).text(t.title));

   if (item.type != '' && !types.some(function(t) { return t.type == item.type; }))
      typeSel.append($('<option></option>').val(item.type).text(item.type + ' (unbekannt)'));

   let fillSensors = function(type, address) {
      addrSel.empty().append($('<option></option>').val('').text('– Sensor –'));

      let facts = Object.values(valueFacts).filter(function(f) { return f.type == type; })
          .sort(function(a, b) { return a.address - b.address; });

      for (let f of facts)
         addrSel.append($('<option></option>').val(f.address)
                        .text(alertSensorTitle(f) + (f.unit ? ' [' + f.unit + ']' : '') + ' · ' + f.address + (f.state ? '' : ' (inaktiv)')));

      if (address !== '' && !facts.some(function(f) { return f.address == address; }))
         addrSel.append($('<option></option>').val(address).text('Adresse ' + address + ' (unbekannt)'));

      addrSel.val(address);
   };

   typeSel.val(item.type);
   fillSensors(item.type, item.address);

   // head: switch, title, the sensor id, actions

   let title = $('<span></span>').addClass('alrTitle');
   let chip = $('<span></span>').addClass('alrChip');

   let updateHead = function() {
      let t = typeSel.val();
      let a = addrSel.val();
      let f = t && a !== '' && a != null ? valueFacts[toKey(t, parseInt(a))] : null;

      title.text(f ? alertSensorTitle(f) : (isNew ? 'Neuer Alert' : 'Sensor unbekannt'));
      chip.text(t && a !== '' && a != null ? t + ':' + a : '').toggle(!!(t && a !== '' && a != null));
      card.toggleClass('alrUnknown', !isNew && !f);
   };

   let card = $('<div></div>')
       .attr('id', 'row_' + item.id)
       .attr('data-id', item.id)
       .addClass('alrCard')
       .toggleClass('alrNew', isNew)
       .toggleClass('alrOff', item.state != 'A');

   let state = $('<span></span>').addClass('alrSwitch')
       .append($('<input></input>').attr({ id: 'state_' + item.id, type: 'checkbox', name: 'state' }).prop('checked', item.state == 'A'))
       .append($('<label></label>').attr('for', 'state_' + item.id).attr('title', 'aktiv'));

   let actions = $('<span></span>').addClass('alrActions');

   if (!isNew) {
      actions
         .append($('<button></button>').attr('type', 'button').addClass('rounded-border buttonOptions alrAction')
                 .append($('<span></span>').addClass('mdi mdi-email-fast-outline')).append($('<span></span>').text('Test Mail'))
                 .click(function() { sendAlertMail(item.id); }))
         .append($('<button></button>').attr('type', 'button').addClass('rounded-border buttonOptions alrAction alrDelete')
                 .append($('<span></span>').addClass('mdi mdi-trash-can-outline')).append($('<span></span>').text('Löschen'))
                 .click(function() {
                    if (confirm("Alert '" + title.text() + "' löschen?"))
                       deleteAlert(item.id);
                 }));
   }

   card.append($('<div></div>').addClass('alrHead')
               .append(state)
               .append($('<span></span>').addClass('alrIcon mdi ' + (isNew ? 'mdi-bell-plus-outline' : 'mdi-bell-outline')))
               .append(title)
               .append(chip)
               .append(actions));

   if (!known)
      card.append($('<div></div>').addClass('alrWarn')
                  .append($('<span></span>').addClass('mdi mdi-alert-outline'))
                  .append($('<span></span>').text('Den Sensor ' + item.type + ':' + item.address + ' gibt es nicht (mehr), der Alert wird nie geprüft - bitte einen Sensor wählen.')));

   // the sections

   card.append($('<div></div>').addClass('alrSection')
               .append($('<div></div>').addClass('alrSectionTitle').text('Sensor'))
               .append($('<div></div>').addClass('alrFields')
                       .append(field('Typ', typeSel, null, 'alrWide'))
                       .append(field('Sensor', addrSel, null, 'alrWide'))));

   card.append($('<div></div>').addClass('alrSection')
               .append($('<div></div>').addClass('alrSectionTitle').text('Bedingung'))
               .append($('<div></div>').addClass('alrFields')
                       .append(field('Minimum', num('min', item.min)))
                       .append(field('Maximum', num('max', item.max)))
                       .append(field('Änderung', num('delta', item.delta), '%'))
                       .append(field('im Zeitraum', num('rangem', item.rangem), 'min'))
                       .append(field('Wiederholen nach', num('maxrepeat', item.maxrepeat), 'min'))));

   card.append($('<div></div>').addClass('alrSection')
               .append($('<div></div>').addClass('alrSectionTitle').text('Mail'))
               .append($('<div></div>').addClass('alrFields')
                       .append(field('Empfänger', text('maddress', item.maddress), null, 'alrFull'))
                       .append(field('Betreff', text('msubject', item.msubject), null, 'alrFull'))
                       .append(field('Inhalt', $('<textarea></textarea>').attr({ name: 'mbody', rows: 5 })
                                     .addClass('rounded-border input').val(item.mbody), null, 'alrFull')))
               .append($('<div></div>').addClass('alrHint')
                       .text('Platzhalter: ' + alertPlaceholders.join(' '))));

   typeSel.on('change', function() { fillSensors(typeSel.val(), ''); updateHead(); });
   addrSel.on('change', updateHead);
   card.on('input change', 'input, select, textarea', function() { card.addClass('alrChanged'); });
   card.on('change', 'input[name="state"]', function() { card.toggleClass('alrOff', !this.checked); });

   updateHead();

   return card;
}

function storeAlerts()
{
   var jsonArray = [];
   var container = document.getElementById("container");

   console.log("storeAlerts");

   var elements = container.querySelectorAll("[id^='row_']");
   var n = 0;

   for (var e = 0; e < elements.length; e++) {
      var jsonObj = {};
      var id = $(elements[e]).data("id");
      var inputs = elements[e].querySelectorAll("[name]");

      jsonObj["id"] = id;

      for (var i = 0; i < inputs.length; i++) {
         var name = $(inputs[i]).attr('name');
         var value = $(inputs[i]).val();

         if (name == "state")
            jsonObj[name] = $(inputs[i]).is(":checked") ? "A" : "D";
         else
            jsonObj[name] = !isNaN(value) ? parseInt(value) : value;
      }

      var address = jsonObj["address"];

      if (id != -1 || !isNaN(address))
         jsonArray[n++] = jsonObj;
   }

   socket.send({ "event" : "storealerts", "object" : { "action" : "store", "alerts" : jsonArray } });
}

function deleteAlert(alertId)
{
   socket.send({ "event" : "storealerts", "object" : { "action" : "delete", "alertid" : alertId } });
}

function sendAlertMail(alertId)
{
   socket.send({ "event" : "command", "object" :
                 {
                    "what" : "testmail",
                    "alertid" : alertId
                 }});
}
