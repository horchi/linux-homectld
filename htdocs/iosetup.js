/*
 *  iosetup.js
 *
 *  (c) 2020-2023 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

var activeSection = '';
var ioSections = {};
var changes = false;
var cvEditor = null;
var doEditor = null;

// CodeMirror (the LUA editor) is loaded on demand, it is only needed here

async function loadCodeMirror()
{
   try {
      await loadAssets(['lib/codemirror/codemirror.js', 'lib/codemirror/codemirror.css']);
      await Promise.all(['lib/codemirror/simple.js',
                         'lib/codemirror/jump-to-line.js',
                         'lib/codemirror/searchcursor.js',
                         'lib/codemirror/search.js',
                         'lib/codemirror/annotatescrollbar.js',
                         'lib/codemirror/matchesonscrollbar.js',
                         'lib/codemirror/matchesonscrollbar.css',
                         'lib/codemirror/dialog.js',
                         'lib/codemirror/dialog.css',
                         'lib/codemirror/show-hint.js',
                         'lib/codemirror/show-hint.css'].map(function(url) { return loadAssets([url]); }));
   }
   catch (e) {
      showInfoDialog({ 'status': -1, 'message': 'Laden des Editors fehlgeschlagen: ' + e.message });
      return false;
   }

   defineLuaMode();

   return true;
}

function defineLuaMode()
{
   if (CodeMirror.modes.lua)
      return;

   CodeMirror.defineSimpleMode('lua', {
      start: [
         {regex: /--\[\[/,                                          token: 'comment',  next: 'comment_ml'},
         {regex: /--.*$/,                                           token: 'comment'},
         {regex: /"(?:[^\\"]|\\.)*"?/,                             token: 'string'},
         {regex: /'(?:[^\\']|\\.)*'?/,                             token: 'string'},
         {regex: /\[\[/,                                            token: 'string',   next: 'string_ml'},
         {regex: /(?:and|break|do|else|elseif|end|false|for|function|goto|if|in|local|nil|not|or|repeat|return|then|true|until|while)\b/, token: 'keyword'},
         {regex: /[+\-*\/\^%#<>=&|~:,;.(){}\[\]]/,                token: 'operator'},
         {regex: /0x[0-9a-fA-F]+|[0-9]+\.?[0-9]*(?:[eE][+\-]?[0-9]+)?/, token: 'number'},
         {regex: /[a-zA-Z_][a-zA-Z0-9_]*/,                        token: 'variable'},
      ],
      comment_ml: [
         {regex: /.*?\]\]/,  token: 'comment', next: 'start'},
         {regex: /.*/,       token: 'comment'},
      ],
      string_ml: [
         {regex: /.*?\]\]/,  token: 'string',  next: 'start'},
         {regex: /.*/,       token: 'string'},
      ],
      meta: { lineComment: '--' }
   });
}

// ----------------------------------------------------------------
// Sensor Setup
// ----------------------------------------------------------------

function initSensorSetup()
{
   // console.log(JSON.stringify(valueFacts, undefined, 4));

   changes = false;
   hideProgressDialog();

   showControlContainer();
   $('#container').removeClass('hidden');

   prepareSetupMenu();

   // die zuletzt gewählte Sensor Kategorie wiederherstellen,
   //   sofern dieser Typ (noch) existiert

   if (activeSection == '')
      activeSection = localStorage.getItem(storagePrefix + 'activeIoSection');

   let sections = [];

   for (var i = 0; i < valueTypes.length; i++)
      sections.push(CSS.escape('io' + valueTypes[i].title.replace(/\s+/g, '')));

   if (!sections.includes(activeSection))
      activeSection = sections.length ? sections[0] : '';

   $("#controlContainer")
      .empty()
      .append($('<div></div>')
              .append($('<button></button>')
                      .attr('id', 'btnStoreIoSetup')
                      .attr('disabled', true)
                      .addClass('rounded-border tool-button')
                      .html('Speichern')
                      .click(function() { storeSensorSetup(); }))
              .append($('<button></button>')
                      .addClass('rounded-border tool-button')
                      .html('Update Scripts')
                      .click(function() {
                         socket.send({ "event" : "forcerefresh", "object" : { 'action' : 'valuefacts', 'forceScripts':true } });
                         showProgressDialog();
                      })))
      .append($('<button></button>')
              .addClass('rounded-border tool-button')
              .html('GPIO Pinout')
              .click(function() { showGpio(); }))

      .append($('<div></div>')
              .addClass('button-group-spacing'))
      .append($('<div></div>')
              .addClass('labelB1')
              .html('Filter'))
      .append($('<input></input>')
              .attr('id', 'incSearchName')
              .attr('placeholder', 'expression...')
              .attr('type', 'search')
              .addClass('input rounded-border clearableOD')
              .css('width', '-webkit-fill-available')
              .css('width', '-moz-available')
              .css('margin-bottom', '8px')
              .on('input', function() { switchTable(activeSection); }))
      .append($('<div></div>')
              .append($('<button></button>')
                      .attr('id', 'filterSensorSetup')
                      .addClass('rounded-border tool-button')
                      .html('[alle]')
                      .click(function() { filterSensorSetup(); })))
      .append($('<div></div>')
              .addClass('button-group-spacing'))
      .append($('<div></div>')
              .addClass('button-group-spacing'));

   let padding = parseInt(getComputedStyle(document.documentElement).getPropertyValue('--containerPadding'));
   var w = $('#container').innerWidth() - padding * 2 - 20;
   var h = $('#container').innerHeight() - padding * 2 - 20;

   $('#container')
      .empty()
      .append($('<div></div>')
              .attr('id', 'ioSetupContainer')
              .addClass('setupContainer'))
      .append($('<div></div>')
              .attr('id', 'gpioState')
              .css('margin', 'var(--containerPadding)')
              .css('background-color', 'var(--widgetBackground)')
              .addClass('hidden rounded-border'));

   for (var i = 0; i < valueTypes.length; i++) {
      var section = 'io' + valueTypes[i].title.replace(/\s+/g, '');
      section = CSS.escape(section);

      if (!$("#"+'btn_' + section).length) {
         $("#controlContainer").append($('<div></div>')
                                       .append($('<button></button>')
                                               .attr('id', 'btn_' + section)
                                               .addClass('rounded-border tool-button')
                                               .click({ "section" : section }, function(event) { switchTable(event.data.section); })
                                               .html(valueTypes[i].title)));

         ioSections[section] = {};
         ioSections[section].title = valueTypes[i].title;
         ioSections[section].type = valueTypes[i].type;
      }
   }

   $("#content").height($(window).height() - getTotalHeightOf('menu')- getTotalHeightOf('footer') - sab);
   $("#container").css('height', '');  // reset until all pages using the height of 'content'
   window.onresize = function() {
      $("#content").height($(window).height() - getTotalHeightOf('menu')- getTotalHeightOf('footer') - sab);
   };

   showTable(activeSection);
}

var filterActive = false;

function filterSensorSetup()
{
   function doProceed() {
      filterActive = !filterActive;
      console.log("filterSensorSetup: " + filterActive);
      $("#filterSensorSetup").html(filterActive ? "[aktive]" : "[alle]");
      showTable(activeSection);
   }

   if (changes)
      confirmDialog(doProceed, 'Changes pending, proceed and forget changes or abort?', 'Proceed', 'Abort');
   else
      doProceed();
}

function switchTable(section)
{
   function doProceed() {
      showTable(section)
   }

   if (changes)
      confirmDialog(doProceed, 'Changes pending, proceed and forget changes or abort?', 'Proceed', 'Abort');
   else
      showTable(section);
}

function showTable(section)
{
   changes = false;
   $('#btnStoreIoSetup').attr('disabled', true)
   $('#gpioState').addClass('hidden');
   $('#ioSetupContainer').removeClass('hidden');

   activeSection = section;

   if (activeSection)
      localStorage.setItem(storagePrefix + 'activeIoSection', activeSection);

   $('button[id^="btn_io"]').each(function () {
      $(this).css('background-color', '');
   });

   $('#btn_' + activeSection).css('background-color', 'slategray');

   let headRow = $('<tr>')
      .append($('<td>').css('width', '22%').text('Name'))
      .append($('<td>').css('min-width', '25vw').text('Titel'))
      .append($('<td>').css({width: '7%', 'min-width': '70px'}).text('Einheit'))
      .append($('<td>').css('width', '5%').text('Aktiv'))
      .append($('<td>').css('width', '5%').text('Aufz.'))
      .append($('<td>').css('width', '12%').text('ID'))
      .append($('<td>').css('width', '7%').text('Batterie'));

   if (activeSection == 'ioGPIO')
      headRow.append($('<td>').css('width', '10%').text('Funktion'));

   headRow.append($('<td>').css('width', '6%'));

   let tbody = $('<tbody>').attr('id', section);

   let table = $('<table>')
       .addClass('tableMultiCol')
       .append($('<thead>')
               .append(headRow))
       .append(tbody);

   // Add (+) button for CV

   if (ioSections[section].type == 'CV') {
      table
         .append($('<tfoot>')
                 .append($('<tr>')
                         .append($('<td>').attr('colspan', '8').css('background-color', '#333333')
                                 .append($('<button>')
                                         .addClass('buttonOptions rounded-border')
                                         .text('+')
                                         .on('click', function() {
                                            socket.send({ "event" : "storesensorsetup", "object" : {
                                               'type' : 'CV',
                                               'name' : 'Calc Sensor',
                                               'action' : 'add'
                                            }});
                                         })
                                        ))));
   }

   $('#ioSetupContainer').empty()
      .append(table)
      .on('change input', 'input, select', function() {
         $(this).closest('tr').data('changed', true);
         $('#btnStoreIoSetup').attr('disabled', false);
         changes = true;
      });

   let filterSensorExpression = null;

   if ($("#incSearchName").val() != "")
      filterSensorExpression = new RegExp($("#incSearchName").val());

   // sort value facts

   let valueFactsSorted = Object.keys(valueFacts).sort(function(a, b) {
      if (valueFacts[a].type != valueFacts[b].type)
         return valueFacts[a].type.localeCompare(valueFacts[b].type);
      if (isNaN(valueFacts[a].name[0]))
         return valueFacts[a].name.localeCompare(valueFacts[b].name);
      return parseInt(valueFacts[a].name) - parseInt(valueFacts[b].name);
   });

   for (let k = 0; k < valueFactsSorted.length; ++k) {
      let key = valueFactsSorted[k];
      let item = valueFacts[key];
      let usrtitle = item.usrtitle != null ? item.usrtitle : "";

      if (!item.state && filterActive)
         continue;

      if (filterSensorExpression && !filterSensorExpression.test(item.title) && !filterSensorExpression.test(usrtitle))
         continue;

      let sectionId = '';

      for (var i = 0; i < valueTypes.length; i++) {
         if (valueTypes[i].type == item.type) {
            sectionId = 'io' + valueTypes[i].title.replace(/\s+/g, '');
            sectionId = CSS.escape(sectionId);
         }
      }

      let root = $('#' + sectionId);

      if (!root.length)
         continue;

      let titleParts = item.title.split("\n");
      let id = item.type + item.address;

      let tr = $('<tr>')
          .append($('<td>')
                  .attr('id', 'row_' + id)
                  .data('address', item.address)
                  .data('type', item.type)
                  .append($('<div>')
                          .text(titleParts[0]))
                  .append($('<div>')
                          .css({fontSize: 'smaller', color: 'darkgray'})
                          .text(titleParts.length > 1 ? titleParts[1] : '')))
          .append($('<td>').addClass('tableMultiColCell')
                  .append($('<input>')
                          .attr({id: 'usrtitle_' + id, type: 'search', value: usrtitle})
                          .addClass('inputSetting rounded-border')
                         ))
          .append($('<td>').addClass('tableMultiColCell')
                  .append($('<input>')
                          .attr({id: 'unit_' + id, type: 'search', value: item.unit})
                          .addClass('inputSetting rounded-border')
                         ))
          .append($('<td>')
                  .append($('<input>')
                          .attr({id: 'state_' + id, type: 'checkbox'})
                          .prop('checked', !!item.state)
                          .addClass('rounded-border input'))
                  .append($('<label>')
                          .attr('for', 'state_' + id)))
          .append($('<td>')
                  .append($('<input>').attr({id: 'record_' + id, type: 'checkbox'})
                          .prop('checked', !!item.record)
                          .addClass('rounded-border input'))
                  .append($('<label>').attr('for', 'record_' + id)))
          .append($('<td>')
                  .append($('<div>').text(key))
                  .append($('<div>').css({fontSize: 'smaller', color: 'darkgray'}).text(item.mac || '')))   // deCONZ MAC
          .append($('<td>').css('white-space', 'nowrap')        // battery devices (deCONZ sensors, ...)
                  .append(allSensors[key] && allSensors[key].battery != null && allSensors[key].battery >= 0
                          ? [batterySymbol(allSensors[key].battery), $('<span></span>').text(' ' + allSensors[key].battery + ' %')]
                          : []));

      let gpioFct = "deactivated";

      if (item.type == 'GPIO') {
         gpioFct = (item.settings && item.settings.fct && item.settings.fct != '') ? item.settings.fct : "deactivated";
         tr.append(
            $('<td>').addClass('tableMultiColCell').append(
               $('<select>').attr('id', 'function_' + id).addClass('inputSetting rounded-border')
                  .append($('<option>').val('deactivated').text('Off'))
                  .append($('<option>').val('occupied').text('Occupied'))
                  .append($('<option>').val('in').text('In'))
                  .append($('<option>').val('out').text('Out'))
                  .val(gpioFct))
         );
      }

      if (item.type == 'DO' || item.type.startsWith('MCPO') ||
          item.type == 'DI' || item.type.startsWith('MCPI') ||
          item.type == 'SC' || item.type == 'GPIO' ||
          item.type == 'CV' || item.type == 'AI' ||
          item.type == 'DZL' || item.type == 'DZLG' || item.type == 'HMB' || item.type == 'DZS' ||
          item.type.startsWith('ADS')) {
         tr.append($('<td>').append($('<button>')
                                    .attr('id', 'btnSensorSetup_' + id)
                                    .attr('disabled', item.type == 'GPIO' ? gpioFct == 'deactivated' || gpioFct == 'occupied' : false)
                                    .addClass('buttonOptions rounded-border')
                                    .text('Setup')
                                    .on('click', () => sensorSetupDialog(item.type, item.address))));
      }

      if (item.type == 'SC' && item.parameter && item.parameter.cloneable)
         tr.append($('<td>').append($('<button>')
                                    .addClass('buttonOptions rounded-border')
                                    .text('Clone')
                                    .on('click', () => sensorScClone(item.type, item.address))));

      if (item.type != 'WEA' && item.type != 'BMS' && item.type != 'MOPEKA'
          && item.type != 'VIC' && !item.type.startsWith('VOTRO') )
         tr.append($('<td>').append($('<button>')
                                    .addClass('buttonOptions rounded-border')
                                    .text('Löschen')
                                    .on('click', () => deleteValueFact(item.type, item.address))));
      root.append(tr);
   }
}

var calSensorType = '';
var calSensorAddress = -1;

function updateSensorSetupValue()
{
   var key = toKey(calSensorType, calSensorAddress);

   if (calSensorType != '' && allSensors[key] != null && allSensors[key].plain != null) {
      $('#actuaCalValue').val(allSensors[key].value + ' ' + valueFacts[key].unit + ' (' + allSensors[key].plain + ')');
   }
}

function sensorSetupDialog(type, address)
{
   function doProceed() {
      showTable(activeSection);

      if (type == 'GPIO')
         sensorGpioSetup(type, address);
      else if (type == 'AI' || type.startsWith('ADS'))
         sensorAiSetup(type, address);
      else if (type == 'CV')
         sensorCvSetup(type, address);
      else if (type == 'DO' || type.startsWith('MCPO'))
         sensorDoSetup(type, address);
      else if (type == 'DI' || type.startsWith('MCPI'))
         sensorDiSetup(type, address);
      else if (type == 'SC')
         sensorScSetup(type, address);
      else if (type == 'DZL' || type == 'DZLG' || type == 'HMB')
         sensorActorSetup(type, address);
      else if (type == 'DZS')
         sensorRulesSetup(type, address);
   }

   if (changes)
      confirmDialog(doProceed, 'Changes pending, proceed and forget changes or abort?', 'Proceed', 'Abort');
   else
      doProceed();
}

function sensorAiSetup(type, address)
{
   console.log("sensorSetup ", type, address);

   calSensorType = type;
   calSensorAddress = parseInt(address);

   var key = toKey(calSensorType, calSensorAddress);
   var currentPoint = 'pointA';
   var form = document.createElement("div");

   if (valueFacts[key] == null) {
      console.log("Sensor ", key, "undefined");
      return;
   }

   $(form).append($('<div></div>')
                  .addClass('settingsDialogContent')
                  .append($('<div></div>')
                          .append($('<span></span>')
                                  .html('Kalibrieren'))
                          .append($('<span></span>')
                                  .append($('<select></select>')
                                          .attr('id', 'calPointSelect')
                                          .addClass('rounded-border inputSetting')
                                          .change(function() {
                                             if (currentPoint == 'pointA') {
                                                valueFacts[key].calPointA = parseFloat($('#calPoint').val());
                                                valueFacts[key].calPointValueA = parseFloat($('#calPointValue').val());
                                             } else {
                                                valueFacts[key].calPointB = parseFloat($('#calPoint').val());
                                                valueFacts[key].calPointValueB = parseFloat($('#calPointValue').val());
                                             }
                                             currentPoint = $(this).val();
                                             if (currentPoint == 'pointA') {
                                                $('#calPoint').val(valueFacts[key].calPointA);
                                                $('#calPointValue').val(valueFacts[key].calPointValueA);
                                             } else {
                                                $('#calPoint').val(valueFacts[key].calPointB);
                                                $('#calPointValue').val(valueFacts[key].calPointValueB);
                                             }
                                          }))
                                 ))
                  .append($('<div></div>')
                          .append($('<span></span>')
                                  .html('Aktuell'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'actuaCalValue')
                                          .addClass('rounded-border inputSetting')
                                          .css('pointer-events', 'none')
                                          .css('background-color', 'var(--yellow2)')
                                          .css('color', 'var(--neutral0)')
                                          .css('width', '60%')
                                          .val('-'))
                                  .append($('<button></button>')
                                          .addClass('buttonOptions rounded-border')
                                          .css('width', 'auto')
                                          .css('float', 'right')
                                          .html('>')
                                          .click(function() { $('#calPointValue').val(allSensors[toKey(calSensorType, calSensorAddress)].plain); }))
                                 ))
                  .append($('<div></div>')
                          .append($('<span></span>')
                                  .html('bei Wert'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'calPoint')
                                          .attr('type', 'number')
                                          .attr('step', 0.1)
                                          .addClass('rounded-border inputSetting')
                                          .val(valueFacts[key].calPointA)
                                         )))
                  .append($('<div></div>')
                          .append($('<span></span>')
                                  .html('-> Sensor Wert'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'calPointValue')
                                          .attr('type', 'number')
                                          .attr('step', 0.1)
                                          .addClass('rounded-border inputSetting')
                                          .val(valueFacts[key].calPointValueA)
                                         )))
                  .append($('<br></br>'))
                  .append($('<div></div>')
                          .append($('<span></span>')
                                  .html('Runden'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'round')
                                          .attr('type', 'number')
                                          .attr('step', 0.1)
                                          .addClass('rounded-border inputSetting')
                                          .val(valueFacts[key].round)
                                         )))
                  .append($('<div></div>')
                          .append($('<span></span>')
                                  .html('Abschneiden unter')
                                 )
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'cutBelow')
                                          .attr('type', 'number')
                                          .attr('step', 0.1)
                                          .addClass('rounded-border inputSetting')
                                          .val(valueFacts[key].cutBelow)
                                         )))
                 );

   var title = valueFacts[key].usrtitle != '' ? valueFacts[key].usrtitle : valueFacts[key].title;

   $(form).dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      hide: "fade",
      width: "500px",
      title: "Sensor '" + title + "' kalibrieren",
      open: function() {
         calSensorType = type;
         calSensorAddress = parseInt(address);

         $('#calPointSelect').append($('<option></option>').val('pointA').html('Punkt 1'));
         $('#calPointSelect').append($('<option></option>').val('pointB').html('Punkt 2'));

         if (allSensors[key] != null)
            $('#actuaCalValue').val(allSensors[key].value + ' ' + valueFacts[key].unit + ' (' + (allSensors[key].plain != null ? allSensors[key].plain : '-') + ')');
      },
      buttons: {
         'Abbrechen': function () {
            $(this).dialog('close');
         },
         'Speichern': function () {
            valueFacts[key].cutBelow = parseFloat($('#cutBelow').val());
            valueFacts[key].round = parseInt($('#round').val());

            if (currentPoint == 'pointA') {
               valueFacts[key].calPointA = parseFloat($('#calPoint').val());
               valueFacts[key].calPointValueA = parseFloat($('#calPointValue').val());
            } else {
               valueFacts[key].calPointB = parseFloat($('#calPoint').val());
               valueFacts[key].calPointValueB = parseFloat($('#calPointValue').val());
            }

            console.log("store AI sensor settings", valueFacts[key].calPointA, '/', valueFacts[key].calPointValueA, '|', valueFacts[key].calPointB, '/', valueFacts[key].calPointValueB);

            socket.send({ "event" : "storesensorsetup", "object" : {
               'type':     calSensorType,
               'address':  calSensorAddress,
               'settings': {
                  'round':    valueFacts[key].round,
                  'cutBelow': valueFacts[key].cutBelow,
                  'pointA':   valueFacts[key].calPointA,
                  'valueA':   valueFacts[key].calPointValueA,
                  'pointB':   valueFacts[key].calPointB,
                  'valueB':   valueFacts[key].calPointValueB
               }
            }});

            $(this).dialog('close');
         }
      },
      close: function() {
         calSensorType = '';
         calSensorAddress = -1;
         $(this).dialog('destroy').remove();
      }
   });
}

function deleteValueFact(type, address)
{
   function doDelete()
   {
      console.log("sensor delete ", type, address);

      socket.send({ "event" : "storesensorsetup", "object" : {
         'type' : type,
         'address' : parseInt(address),
         'action' : 'delete'
      }});
   }

   confirmDialog(doDelete, 'Delete Sensor?', 'Delete', 'Cancel');
}

//***************************************************************************
// Time ranges (tab 'Zeiten' of the outputs, time control of the actors)
//   start / end: fixed time 'HH:MM' or sun event (sunRefs), each with an offset [min];
//   with allowEmpty a side can be 'keine' (actors switch only at the limits)
//***************************************************************************

function createTimeSide(label, cls, ref, offset, allowEmpty = false) {
   let isSun = sunRefs[ref] != null;
   let isEmpty = allowEmpty && !ref;
   let $select = $('<select></select>').addClass('rounded-border do-timer-ref ' + cls + '-ref');

   if (allowEmpty)
      $select.append($('<option></option>').val('').text('keine'));

   $select.append($('<option></option>').val('time').text('Uhrzeit'));

   for (const [key, title] of Object.entries(sunRefs))
      $select.append($('<option></option>').val(key).text(title));

   $select.val(isEmpty ? '' : isSun ? ref : 'time');

   let $time = $('<input></input>').attr('type', 'time').addClass('do-timer-input ' + cls).val(isSun || isEmpty ? '' : ref);
   let $offset = $('<input></input>').attr('type', 'number').attr('step', '1').attr('title', 'Offset [min]')
       .addClass('do-timer-input do-timer-offset ' + cls + '-offset').val(offset ? offset : 0);
   let $unit = $('<span>min</span>');
   let $resolved = $('<span></span>').addClass('do-timer-resolved');

   let update = function() {
      let sel = $select.val();
      $time.toggle(sel == 'time');
      $offset.toggle(sel != '');
      $unit.toggle(sel != '');
      let resolved = sel != '' ? resolveTimeRef(sel != 'time' ? sel : $time.val(), $offset.val()) : null;
      $resolved.text(resolved ? '(' + resolved + ')' : '');
   };

   $select.on('change', update);
   $time.on('change', update);
   $offset.on('input', update);
   update();

   return $('<div></div>').addClass('do-timer-side')
      .append($('<span></span>').text(label).addClass('do-timer-label'))
      .append($select)
      .append($time)
      .append($offset)
      .append($unit)
      .append($resolved);
}

// optional date range of a time range ('DD.MM', also over the turn of the year), selected by day and month

const monthNames = ['Jan', 'Feb', 'Mär', 'Apr', 'Mai', 'Jun', 'Jul', 'Aug', 'Sep', 'Okt', 'Nov', 'Dez'];

function createDateSide(t) {
   let $date = (cls, value) => {
      let [d, m] = (value || '').split('.');
      let $day = $('<select></select>').addClass('rounded-border do-timer-ref ' + cls + '-day').append($('<option></option>').val('').text('--'));
      let $month = $('<select></select>').addClass('rounded-border do-timer-ref ' + cls + '-month').append($('<option></option>').val('').text('--'));

      for (let i = 1; i <= 31; i++)
         $day.append($('<option></option>').val(String(i).padStart(2, '0')).text(i + '.'));
      for (let i = 1; i <= 12; i++)
         $month.append($('<option></option>').val(String(i).padStart(2, '0')).text(monthNames[i-1]));

      $day.val(d || '');
      $month.val(m || '');

      return $('<span></span>').addClass('do-timer-date').append($day).append($month);
   };

   return $('<div></div>').addClass('do-timer-side').attr('title', 'leer = ganzjährig')
      .append($('<span></span>').text('Gültig').addClass('do-timer-label'))
      .append($date('timer-date-from', t.dateFrom))
      .append($('<span>–</span>'))
      .append($date('timer-date-to', t.dateTo));
}

function dateSideValue($row, cls) {      // 'DD.MM' or '' (day and month both needed)
   let d = $row.find('.' + cls + '-day').val();
   let m = $row.find('.' + cls + '-month').val();

   return d && m ? d + '.' + m : '';
}

function createTimeRow(t = { 'start': '08:00', 'end': '17:00' }, labels = ['Von', 'Bis'], allowEmpty = false) {
   let $row = $('<div></div>').addClass('do-timer-row');
   $row.append($('<div></div>').addClass('do-timer-sides')
               .append(createTimeSide(labels[0], 'timer-start', t.start, t.startOffset, allowEmpty))
               .append(createTimeSide(labels[1], 'timer-end', t.end, t.endOffset, allowEmpty))
               .append(createDateSide(t)))
       .append($('<button></button>').text('X').addClass('do-timer-delete').on('click', function() { $row.remove(); }));
   return $row;
}

// the rows of a container -> [{ start, startOffset, end, endOffset [, dateFrom, dateTo] }]

function collectTimeRows($container, allowEmpty = false) {
   let timesArray = [];

   $container.find('.do-timer-row').each(function() {
      let side = (cls) => {
         let ref = $(this).find('.' + cls + '-ref').val();
         return ref == '' ? '' : ref != 'time' ? ref : $(this).find('input.' + cls).val();
      };
      let start = side('timer-start');
      let end = side('timer-end');

      if (allowEmpty ? (start || end) : (start && end)) {
         let range = { 'start': start, 'startOffset': parseInt($(this).find('.timer-start-offset').val()) || 0,
                       'end': end, 'endOffset': parseInt($(this).find('.timer-end-offset').val()) || 0 };
         let dateFrom = dateSideValue($(this), 'timer-date-from');
         let dateTo = dateSideValue($(this), 'timer-date-to');

         if (dateFrom && dateTo) {         // both or none
            range.dateFrom = dateFrom;
            range.dateTo = dateTo;
         }

         timesArray.push(range);
      }
   });

   return timesArray;
}

function sensorGpioSetup(type, address)
{
   let key = toKey(type, parseInt(address));

   if (!valueFacts[key].settings)
      valueFacts[key].settings = {};

   if (!valueFacts[key].settings.fct || valueFacts[key].settings.fct == '')
      valueFacts[key].settings.fct = 'deactivated';

   if (valueFacts[key].settings.fct == 'in')
      sensorDiSetup(type, address);
   else if (valueFacts[key].settings.fct == 'out')
      sensorDoSetup(type, address);
}

async function sensorDoSetup(type, address)
{
   if (!await loadCodeMirror())
      return;

   calSensorType = type;
   calSensorAddress = address;

   let key = toKey(calSensorType, parseInt(calSensorAddress));
   let form = document.createElement("div");

   if (valueFacts[key] == null) {
      console.log("Sensor ", key, "undefined");
      return;
   }

   let currentOutputMode = (valueFacts[key].settings && valueFacts[key].settings.outputModes !== undefined)
                           ? valueFacts[key].settings.outputModes
                           : 1;

   let isManualChecked = (currentOutputMode & 0x01) === 0x01;
   let isAutoChecked = (currentOutputMode & 0x02) === 0x02;

   // Struktur bleibt 1:1 identisch, wir schalten die Zeilen später einfach per ID sichtbar/unsichtbar

   $(form).append($('<div></div>')
                  .addClass('settingsDialogContent')
                  .append($('<div></div>')
                          .attr('id', 'row-modus')
                          .append($('<span></span>').html('Modus'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'modeManual')
                                          .attr('type', 'checkbox')
                                          .addClass('rounded-border inputSetting')
                                          .prop('checked', isManualChecked))
                                  .append($('<label></label>').prop('for', 'modeManual').text('Manuell '))
                                  .append($('<input></input>')
                                          .attr('id', 'modeAuto')
                                          .attr('type', 'checkbox')
                                          .addClass('rounded-border inputSetting')
                                          .css('margin-left', '15px')
                                          .prop('checked', isAutoChecked))
                                  .append($('<label></label>').prop('for', 'modeAuto').text('Auto'))))
                  .append($('<div></div>')
                          .attr('id', 'row-invert')
                          .append($('<span></span>').html('Invertieren'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'invertDo')
                                          .attr('type', 'checkbox')
                                          .addClass('rounded-border inputSetting')
                                          .prop('checked', valueFacts[key].settings ? valueFacts[key].settings.invert : true))
                                  .append($('<label></label>').prop('for', 'invertDo'))))

                  .append($('<div></div>')
                          .attr('id', 'row-ask')
                          .attr('title', 'Rückfrage beim manuellen schalten')
                          .append($('<span></span>').html('Rückfrage'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'askDo')
                                          .attr('type', 'checkbox')
                                          .addClass('rounded-border inputSetting')
                                          .prop('checked', valueFacts[key].settings ? valueFacts[key].settings.ask : false))
                                  .append($('<label></label>').prop('for', 'askDo'))))

                  .append($('<div></div>')
                          .attr('id', 'row-impulse')
                          .append($('<span></span>').html('Impuls (50ms)'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'impulseDo')
                                          .attr('type', 'checkbox')
                                          .addClass('rounded-border inputSetting')
                                          .prop('checked', valueFacts[key].settings ? valueFacts[key].settings.impulse : false))
                                  .append($('<label></label>').prop('for', 'impulseDo'))))
                  .append($('<div></div>')
                          .attr('id', 'row-feedback')
                          .append($('<span></span>').html('Feedback Input'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'feedbackIo')
                                          .attr('type', 'search')
                                          .addClass('rounded-border inputSetting')
                                          .val(valueFacts[key].settings && valueFacts[key].settings.feedbackInType && valueFacts[key].settings.feedbackInAddress?
                                               valueFacts[key].settings.feedbackInType + ':0x' + valueFacts[key].settings.feedbackInAddress.toString(16) :
                                               ''))))
                  .append($('<div></div>')
                          .attr('id', 'row-zeiten')
                          .append($('<span></span>')
                                  .append($('<button></button>').text('+').addClass('rounded-border button2')
                                          .on('click', function() { $('#doTimerListContainer').append(createTimeRow()); }))
                                  .append($('<div></div>').attr('id', 'doTimerListContainer').addClass('do-timer-list'))))
                  .append($('<div></div>')
                          .attr('id', 'row-script')
                          .addClass('textarea-row')
                          .append($('<span></span>').html('Skript'))
                          .append($('<span></span>')
                                  .append($('<div></div>').addClass('lua-cm-editor'))))
                  .append($('<div></div>')
                          .attr('id', 'luaCheckResult')
                          .css('font-size', 'large')
                          .css('margin-top', '8px')
                          .addClass('luaCheckResult'))
                 );

   let existingTimes = (valueFacts[key].settings && Array.isArray(valueFacts[key].settings.times)) ? valueFacts[key].settings.times : [];

   existingTimes.forEach(t => { $(form).find('#doTimerListContainer').append(createTimeRow(t)); });
   let doScript = valueFacts[key].settings && valueFacts[key].settings.script ? valueFacts[key].settings.script : '';
   let title = valueFacts[key].usrtitle != '' ? valueFacts[key].usrtitle : valueFacts[key].title;

   $(form).dialog({
      modal: true,
      resizable: true,
      closeOnEscape: true,
      hide: "fade",
      width: Math.round($(window).width() * 0.8),
      height: Math.round($(window).height() * 0.6),
      title: "DO settings '" + title + "'",
      open: function() {
         calSensorType = type;
         calSensorAddress = address;

         // Titelzeile holen und Navigations-Container mit ID erstellen

         let $titlebar = $(this).dialog('widget').find('.ui-dialog-titlebar');
         let $tabNav = $('<div></div>').attr('id', 'sensor-tabs-nav').css({'float': 'right', 'margin-right': '40px', 'display': 'flex', 'gap': '5px'})
            .append($('<button>Allgemein</button>').addClass('rounded-border tool-button active-tab').on('click', function() {
               // Visuelles Umschalten der aktiven Klasse
               $('#sensor-tabs-nav button').removeClass('active-tab');
               $(this).addClass('active-tab');

               $('#row-modus, #row-invert, #row-impulse, #row-feedback, #row-ask').show();
               $('#row-zeiten, #row-script, #luaCheckResult').hide();
            }))
            .append($('<button>Zeiten</button>').addClass('rounded-border tool-button').on('click', function() {
               $('#sensor-tabs-nav button').removeClass('active-tab');
               $(this).addClass('active-tab');

               $('#row-zeiten').show();
               $('#row-modus, #row-invert, #row-impulse, #row-feedback, #row-script, #luaCheckResult, #row-ask').hide();
            }))
            .append($('<button>Skript</button>').addClass('rounded-border tool-button').on('click', function() {
               $('#sensor-tabs-nav button').removeClass('active-tab');
               $(this).addClass('active-tab');

               $('#row-script, #luaCheckResult').show();
               $('#row-modus, #row-invert, #row-impulse, #row-feedback, #row-zeiten, #row-ask').hide();
               doEditor.refresh();
            }));
         $titlebar.append($tabNav);

         // Initiale Ansicht beim Starten (Allgemein aktiv)
         $('#row-modus, #row-invert, #row-impulse, #row-feedback, #row-ask').show();
         $('#row-zeiten, #row-script, #luaCheckResult').hide();

         doEditor = CodeMirror($(form).find('.lua-cm-editor')[0], {
            value:          doScript,
            mode:           'lua',
            lineNumbers:    true,
            indentUnit:     3,
            tabSize:        3,
            indentWithTabs: false,
            lineWrapping:   true,
            autofocus:      true,
            extraKeys:      { 'Esc': () => $(form).dialog('close') },
         });
         doEditor.setSize('100%', '220px');
      },
      buttons: {
         'Abbrechen': function () {
            $(this).dialog('close');
         },
         'Syntax prüfen': function () {
            // Beim automatischen Wechsel zum Skript auch den Button als aktiv markieren
            $('#sensor-tabs-nav button').removeClass('active-tab');
            $('#sensor-tabs-nav button').slice(2, 3).addClass('active-tab');

            $('#row-script, #luaCheckResult').show();
            $('#row-modus, #row-invert, #row-impulse, #row-feedback, #row-zeiten').hide();
            doEditor.refresh();
            $('#luaCheckResult').text('...').css('color', '');
            socket.send({ "event" : "checkluascript", "object" : { 'lua': doEditor.getValue() }});
         },
         'Speichern': function () {
            let targetMode = 0;
            if ($('#modeManual').is(':checked')) targetMode |= 0x01;
            if ($('#modeAuto').is(':checked')) targetMode |= 0x02;

            if (targetMode === 0) {
               alert('Es muss mindestens ein Modus (Manuell oder Auto) ausgewählt sein!');
               return;
            }

            let timesArray = collectTimeRows($(form));

            let splitVal = $('#feedbackIo').val().split(":");
            let fbType = splitVal ? splitVal : '';
            let addr = (splitVal && splitVal) ? parseInt(splitVal) : 0;

            socket.send({ "event" : "storesensorsetup", "object" : {
               'type': calSensorType,
               'address': parseInt(calSensorAddress),
               'settings': {
                  'outputModes': targetMode,
                  'invert': $('#invertDo').is(':checked'),
                  'ask': $('#askDo').is(':checked'),
                  'impulse': $('#impulseDo').is(':checked'),
                  'script': doEditor.getValue(),
                  'feedbackInType': fbType,
                  'feedbackInAddress': addr,
                  'times': timesArray,
                  'fct' : valueFacts[key].settings ? valueFacts[key].settings.fct : ''
               }
            }});
            $(this).dialog('close');
         }
      },
      close: function() {
         doEditor = null;
         calSensorType = '';
         calSensorAddress = -1;
         $(this).dialog('destroy').remove();
      }
   });
}

//***************************************************************************
// Time control of the actors (DZL, DZLG, HMB)
//   switched only at the limits of the ranges (see sun.c), a limit may be empty
//***************************************************************************

function sensorActorSetup(type, address)
{
   let key = toKey(type, parseInt(address));

   if (valueFacts[key] == null) {
      console.log("Sensor ", key, "undefined");
      return;
   }

   let settings = valueFacts[key].settings ? valueFacts[key].settings : {};
   let blind = type == 'HMB';
   let labels = blind ? ['Zu', 'Auf'] : ['An', 'Aus'];
   let form = document.createElement("div");

   let numberRow = (id, label, value, title) => $('<div></div>')
       .append($('<span></span>').html(label))
       .append($('<span></span>')
               .append($('<input></input>')
                       .attr('id', id)
                       .attr('type', 'number').attr('min', '0').attr('max', '100').attr('step', '1')
                       .attr('title', title)
                       .addClass('rounded-border inputSetting')
                       .val(value)));

   let dlgContent = $('<div></div>')
       .addClass('settingsDialogContent')
       .append($('<div></div>')
               .append($('<span></span>').html('Zeitsteuerung aktiv'))
               .append($('<span></span>')
                       .append($('<input></input>')
                               .attr('id', 'timesEnabled')
                               .attr('type', 'checkbox')
                               .addClass('rounded-border inputSetting')
                               .prop('checked', settings.timesEnabled != null ? settings.timesEnabled : true))
                       .append($('<label></label>').prop('for', 'timesEnabled'))));

   if (blind)
      dlgContent
         .append(numberRow('levelIn', 'Position bei \'Zu\' [%]', settings.levelIn != null ? settings.levelIn : 0, '0 = ganz zu'))
         .append(numberRow('levelOut', 'Position bei \'Auf\' [%]', settings.levelOut != null ? settings.levelOut : 100, '100 = ganz auf'));
   else
      dlgContent
         .append(numberRow('timesBri', 'Helligkeit bei \'An\' [%]', settings.bri != null && settings.bri > 0 ? settings.bri : '', 'leer = unverändert'));

   dlgContent
      .append($('<div></div>')
              .append($('<span></span>').html('Zeiten')))
      .append($('<div></div>').attr('id', 'actorTimerListContainer').addClass('do-timer-list'))
      .append($('<div></div>')
              .append($('<button></button>').text('+').attr('title', 'Zeit hinzufügen').addClass('rounded-border button2')
                      .on('click', function() { $('#actorTimerListContainer').append(createTimeRow({ 'start': '', 'end': '' }, labels, true)); })))
      .append($('<div></div>')
              .css('font-size', 'smaller').css('color', '#999').css('margin-top', '8px')
              .html('Geschaltet wird nur zu den eingestellten Zeiten, von Hand geschaltet bleibt es bis zur nächsten Zeit. ' +
                    'Eine der beiden Zeiten darf \'keine\' sein. Gültig (Tag / Monat, leer = ganzjährig): nur an diesen Tagen, ' +
                    'auch über den Jahreswechsel, das Ende über Mitternacht gehört zum Vortag.'));

   // shading of the blinds (see shade.c)

   if (blind) {
      let shade = settings.shade || {};
      let num = (id, label, value, min, max, title) => $('<div></div>')
          .append($('<span></span>').html(label))
          .append($('<span></span>')
                  .append($('<input></input>').attr('id', id).attr('type', 'number')
                          .attr('min', min).attr('max', max).attr('step', '1').attr('title', title || '')
                          .addClass('rounded-border inputSetting').val(value)));
      let sensorSelect = (id, label, selected, units) => {
         let $sel = $('<select></select>').attr('id', id).addClass('rounded-border inputSetting')
             .append($('<option></option>').val('').text('– keiner –'));
         let opts = [];

         for (const k in valueFacts) {
            let f = valueFacts[k];
            if (f.state && units.includes(f.unit))
               opts.push({ 'key': k, 'title': (f.usrtitle ? f.usrtitle : f.title) + ' (' + k + ')' });
         }

         opts.sort((a, b) => a.title.localeCompare(b.title));

         for (const o of opts)
            $sel.append($('<option></option>').val(o.key).text(o.title));

         return $('<div></div>').append($('<span></span>').html(label)).append($('<span></span>').append($sel.val(selected || '')));
      };

      let sun = sunPositionNow();

      dlgContent
         .append($('<div></div>').css('margin-top', '14px').css('font-weight', 'bold').html('Beschattung'))
         .append($('<div></div>')
                 .append($('<span></span>').html('Beschattung aktiv'))
                 .append($('<span></span>')
                         .append($('<input></input>').attr('id', 'shadeEnabled').attr('type', 'checkbox')
                                 .addClass('rounded-border inputSetting').prop('checked', shade.enabled == true))
                         .append($('<label></label>').prop('for', 'shadeEnabled'))))
         .append(num('shadeLevel', 'Position bei Beschattung [%]', shade.level != null ? shade.level : 30, 0, 100, '100 = ganz auf'))
         .append(num('shadeAzFrom', 'Sonne auf dem Fenster ab Azimut [°]', shade.azFrom != null ? shade.azFrom : 180, 0, 360, '0 = Nord, 90 = Ost, 180 = Süd, 270 = West'))
         .append(num('shadeAzTo', '... bis Azimut [°]', shade.azTo != null ? shade.azTo : 290, 0, 360))
         .append(num('shadeMinEl', 'Mindesthöhe der Sonne [°]', shade.minElevation != null ? shade.minElevation : 10, 0, 90))
         .append($('<div></div>')
                 .append($('<span></span>').html('Sonne jetzt'))
                 .append($('<span></span>').addClass('do-timer-resolved')
                         .text(sun ? 'Azimut ' + sun.azimuth.toFixed(0) + '°, Höhe ' + sun.elevation.toFixed(0) + '°' : 'unbekannt')))
         .append(sensorSelect('shadeLuxSensor', 'Helligkeits-Sensor', shade.luxSensor, ['lx', 'W', 'W/m²']))
         .append(num('shadeLuxMin', 'ab Helligkeit', shade.luxMin != null ? shade.luxMin : '', 0, 200000, 'Schwelle in der Einheit des Sensors (lx)'))
         .append(sensorSelect('shadeTempSensor', 'Außentemperatur-Sensor', shade.tempSensor, ['°C']))
         .append(num('shadeTempMin', 'ab Temperatur [°C]', shade.tempMin != null ? shade.tempMin : 22, -30, 50))
         .append(num('shadeDelayOn', 'Verzögerung Beschatten [min]', shade.delayOn != null ? shade.delayOn : 10, 0, 120, 'so lange müssen die Bedingungen erfüllt sein'))
         .append(num('shadeDelayOff', 'Verzögerung Ende [min]', shade.delayOff != null ? shade.delayOff : 20, 0, 120, 'so lange müssen die Bedingungen weg sein'))
         .append($('<div></div>')
                 .css('font-size', 'smaller').css('color', '#999').css('margin-top', '8px')
                 .html('Fährt nur nach unten auf die Beschattungsposition und danach zurück, wenn er noch dort steht. ' +
                       'Von Hand gefahren: Beschattung bis morgen aus. Die Zeitsteuerung hat Vorrang.'));
   }

   $(form).append(dlgContent);

   (Array.isArray(settings.times) ? settings.times : []).forEach(t => {
      $(form).find('#actorTimerListContainer').append(createTimeRow(t, labels, true));
   });

   let title = valueFacts[key].usrtitle != '' ? valueFacts[key].usrtitle : valueFacts[key].title;

   $(form).dialog({
      modal: true,
      resizable: true,
      closeOnEscape: true,
      hide: "fade",
      width: Math.min(Math.round($(window).width() * 0.9), 700),
      maxHeight: Math.round($(window).height() * 0.9),
      title: "Zeitsteuerung '" + title + "'",
      open: function() {
         calSensorType = type;
         calSensorAddress = address;
      },
      buttons: {
         'Abbrechen': function () {
            $(this).dialog('close');
         },
         'Speichern': function () {
            let newSettings = Object.assign({}, settings);   // keep other settings of the sensor

            newSettings.timesEnabled = $('#timesEnabled').is(':checked');
            newSettings.times = collectTimeRows($(form), true);

            if (blind) {
               newSettings.levelIn = parseInt($('#levelIn').val()) || 0;
               newSettings.levelOut = $('#levelOut').val() != '' ? parseInt($('#levelOut').val()) : 100;

               let int = (id, def) => { let v = parseInt($('#' + id).val()); return isNaN(v) ? def : v; };

               newSettings.shade = {
                  'enabled': $('#shadeEnabled').is(':checked'),
                  'level': int('shadeLevel', 30),
                  'azFrom': int('shadeAzFrom', 180),
                  'azTo': int('shadeAzTo', 290),
                  'minElevation': int('shadeMinEl', 10),
                  'luxSensor': $('#shadeLuxSensor').val(),
                  'luxMin': int('shadeLuxMin', 0),
                  'tempSensor': $('#shadeTempSensor').val(),
                  'tempMin': int('shadeTempMin', 22),
                  'delayOn': int('shadeDelayOn', 10),
                  'delayOff': int('shadeDelayOff', 20)
               };
            }
            else
               newSettings.bri = parseInt($('#timesBri').val()) || 0;

            socket.send({ "event" : "storesensorsetup", "object" : {
               'type' : type,
               'address' : parseInt(address),
               'settings' : newSettings
            }});

            $(this).dialog('close');
         }
      },
      close: function() {
         calSensorType = '';
         calSensorAddress = -1;
         $(this).dialog('destroy').remove();
      }
   });
}

//***************************************************************************
// Light rules of switches / motion sensors (DZS, see lightrules.c)
//***************************************************************************

let ruleBlockCount = 0;

function createRuleBlock(rule, targetOptions, lastButton)
{
   let $block = $('<div></div>').addClass('rule-block rounded-border');
   let row = (label, ...content) => $('<div></div>').addClass('rule-row')
       .append($('<span></span>').addClass('rule-label').html(label))
       .append($('<span></span>').addClass('rule-value').append(...content));

   let $trigger = $('<select></select>').addClass('rounded-border rule-trigger')
       .append($('<option></option>').val('button').text('Taste'))
       .append($('<option></option>').val('motion').text('Bewegung'))
       .val(rule.trigger || 'button');
   let $button = $('<input></input>').attr('type', 'number').addClass('rounded-border rule-button')
       .attr('title', 'Tastencode, z.B. 1002 kurz, 1004 doppelt')
       .val(rule.button != null ? rule.button : (lastButton || 1002));
   let $buttonHint = $('<span></span>').addClass('do-timer-resolved')
       .text(lastButton ? '(zuletzt: ' + lastButton + ')' : '');

   let $action = $('<select></select>').addClass('rounded-border rule-action')
       .append($('<option></option>').val('toggle').text('Umschalten'))
       .append($('<option></option>').val('on').text('An'))
       .append($('<option></option>').val('off').text('Aus'))
       .append($('<option></option>').val('follow').text('Folgen (an bei Bewegung, aus ohne)'))
       .val(rule.action || 'toggle');

   let $targets = $('<select></select>').attr('multiple', true).attr('size', 8).addClass('rounded-border rule-targets');

   for (const opt of targetOptions)
      $targets.append($('<option></option>').val(opt.key).text(opt.title)
                      .prop('selected', Array.isArray(rule.targets) && rule.targets.includes(opt.key)));

   let num = (cls, value, title) => $('<input></input>').attr('type', 'number').attr('min', '0')
       .attr('title', title).addClass('rounded-border rule-number ' + cls).val(value ? value : '');

   let cond = rule.condition || {};
   // the checkbox is drawn by its label (checkbox.css), needs a unique id

   let condId = 'ruleCondOn' + (++ruleBlockCount);
   let $condOn = $('<input></input>').attr('type', 'checkbox').attr('id', condId).addClass('rule-cond-on').prop('checked', rule.condition != null);
   let $condLabel = $('<label></label>').prop('for', condId);
   let $condSides = $('<div></div>').addClass('do-timer-sides rule-cond')
       .append(createTimeSide('Von', 'timer-start', cond.start || 'dusk', cond.startOffset))
       .append(createTimeSide('Bis', 'timer-end', cond.end || 'sunrise', cond.endOffset));

   let update = () => {
      let button = $trigger.val() == 'button';
      $button.toggle(button);
      $buttonHint.toggle(button);
      $condSides.toggle($condOn.is(':checked'));
   };

   $trigger.on('change', update);
   $condOn.on('change', update);

   $block
      .append(row('Auslöser', $trigger, $button, $buttonHint))
      .append(row('Aktion', $action))
      .append(row('Ziele', $targets))
      .append(row('Helligkeit [%]', num('rule-bri', rule.bri, 'leer = unverändert')))
      .append(row('Übergang [s]', num('rule-transition', rule.transition, 'leer = Standard')))
      .append(row('Aus nach [s]', num('rule-offafter', rule.offAfter, 'leer = kein Timer; jede weitere Bewegung / Tastendruck startet ihn neu')))
      .append(row('Nur im Zeitfenster', $condOn, $condLabel, $condSides))
      .append($('<div></div>').css('text-align', 'right')
              .append($('<button></button>').text('Regel löschen').addClass('rounded-border do-timer-delete')
                      .on('click', () => $block.remove())));

   update();
   return $block;
}

function collectRuleBlocks($container)
{
   let rules = [];

   $container.find('.rule-block').each(function() {
      let $b = $(this);
      let rule = {
         'trigger': $b.find('.rule-trigger').val(),
         'action': $b.find('.rule-action').val(),
         'targets': $b.find('.rule-targets').val() || []
      };

      if (rule.trigger == 'button')
         rule.button = parseInt($b.find('.rule-button').val()) || 0;

      for (const [cls, name] of [['rule-bri', 'bri'], ['rule-transition', 'transition'], ['rule-offafter', 'offAfter']]) {
         let v = parseInt($b.find('.' + cls).val());
         if (v > 0)
            rule[name] = v;
      }

      if ($b.find('.rule-cond-on').is(':checked')) {
         let side = (cls) => {
            let ref = $b.find('.' + cls + '-ref').val();
            return ref != 'time' ? ref : $b.find('input.' + cls).val();
         };
         rule.condition = { 'start': side('timer-start'), 'startOffset': parseInt($b.find('.timer-start-offset').val()) || 0,
                            'end': side('timer-end'), 'endOffset': parseInt($b.find('.timer-end-offset').val()) || 0 };
      }

      if (rule.targets.length)
         rules.push(rule);
   });

   return rules;
}

function sensorRulesSetup(type, address)
{
   let key = toKey(type, parseInt(address));

   if (valueFacts[key] == null) {
      console.log("Sensor ", key, "undefined");
      return;
   }

   let settings = valueFacts[key].settings ? valueFacts[key].settings : {};
   let sensor = allSensors[key];
   let lastButton = sensor && sensor.value > 1000 ? sensor.value : null;
   let form = document.createElement("div");

   // targets: active lights, light groups and outputs

   let targetOptions = [];

   for (const k in valueFacts) {
      let f = valueFacts[k];

      if (!f.state || !(f.type == 'DZL' || f.type == 'DZLG' || f.type == 'DO' || f.type.startsWith('MCPO') ||
                        (f.type == 'GPIO' && f.settings && f.settings.fct == 'out')))
         continue;

      targetOptions.push({ 'key': k, 'title': (f.usrtitle ? f.usrtitle : f.title) + ' (' + k + ')' });
   }

   targetOptions.sort((a, b) => a.title.localeCompare(b.title));

   let $list = $('<div></div>').attr('id', 'ruleListContainer');

   (Array.isArray(settings.rules) ? settings.rules : []).forEach(r => $list.append(createRuleBlock(r, targetOptions, lastButton)));

   $(form)
      .append($('<div></div>')
              .append($('<button></button>').text('+ Regel').addClass('rounded-border button2')
                      .on('click', () => $list.append(createRuleBlock({}, targetOptions, lastButton)))))
      .append($list)
      .append($('<div></div>')
              .css('font-size', 'smaller').css('color', '#999').css('margin-top', '8px')
              .html('Jeder Tastendruck und jede Bewegung löst aus. \'Umschalten\': ist eines der Ziele an, gehen alle aus, sonst alle an. ' +
                    'Der Aus-Timer schaltet nur aus, was die Regel selbst eingeschaltet hat. ' +
                    'Das Zeitfenster gilt nur für das Einschalten.'));

   let title = valueFacts[key].usrtitle != '' ? valueFacts[key].usrtitle : valueFacts[key].title;

   $(form).dialog({
      modal: true,
      resizable: true,
      closeOnEscape: true,
      hide: "fade",
      width: Math.min(Math.round($(window).width() * 0.95), 800),
      maxHeight: Math.round($(window).height() * 0.9),
      title: "Regeln '" + title + "'",
      buttons: {
         'Abbrechen': function () {
            $(this).dialog('close');
         },
         'Speichern': function () {
            let newSettings = Object.assign({}, settings);   // keep other settings of the sensor
            newSettings.rules = collectRuleBlocks($list);

            socket.send({ "event" : "storesensorsetup", "object" : {
               'type' : type,
               'address' : parseInt(address),
               'settings' : newSettings
            }});

            $(this).dialog('close');
         }
      },
      close: function() {
         $(this).dialog('destroy').remove();
      }
   });
}

function sensorDiSetup(type, address)
{
   console.log("sensorSetup ", type, address);

   calSensorType = type;
   calSensorAddress = address;

   let key = toKey(calSensorType, parseInt(calSensorAddress));
   let form = document.createElement("div");

   if (valueFacts[key] == null) {
      console.log("Sensor ", key, "undefined");
      return;
   }

   let dlgContent = $('<div></div>')
       .addClass('settingsDialogContent')
       .append($('<div></div>')
               .append($('<span></span>')
                       .html('Invertieren'))
               .append($('<span></span>')
                       .append($('<input></input>')
                               .attr('id', 'invertDi')
                               .attr('type', 'checkbox')
                               .addClass('rounded-border inputSetting')
                               .prop('checked', valueFacts[key].settings ? valueFacts[key].settings.invert : true))
                       .append($('<label></label>')
                               .prop('for', 'invertDi'))))
       .append($('<div></div>')
               .append($('<span></span>')
                       .html('Pull Up/Down'))
               .append($('<span></span>')
                       .append($('<select></select>')
                               .attr('id', 'pullDi')
                               .addClass('rounded-border inputSetting'))));

   $(form).append(dlgContent);

   if (type == 'DI') {
      $(dlgContent)
         .append($('<div></div>')
                 .append($('<span></span>')
                         .html('Interrupt'))
                 .append($('<span></span>')
                         .append($('<input></input>')
                                 .attr('id', 'interruptDi')
                                 .attr('type', 'checkbox')
                                 .addClass('rounded-border inputSetting')
                                 .prop('checked', valueFacts[key].settings ? valueFacts[key].settings.interrupt : false))
                         .append($('<label></label>')
                                 .attr('title', 'Neustart zum aktivieren')
                                 .prop('for', 'interruptDi'))));
   }

   var title = valueFacts[key].usrtitle != '' ? valueFacts[key].usrtitle : valueFacts[key].title;

   $(form).dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      hide: "fade",
      width: "500px",
      title: "DI settings '" + title + '"',
      open: function() {
         calSensorType = type;
         calSensorAddress = address;
      },
      buttons: {
         'Abbrechen': function () {
            $(this).dialog('close');
         },
         'Speichern': function () {
            socket.send({ "event" : "storesensorsetup", "object" : {
               'type' : calSensorType,
               'address' : parseInt(calSensorAddress),
               'settings' : {
                  'invert' : $('#invertDi').is(':checked'),
                  'interrupt' : $('#interruptDi').is(':checked'),
                  'pull' : parseInt($('#pullDi').val()),
                  'fct' : valueFacts[key].settings ? valueFacts[key].settings.fct : ''
               }
            }});

            $(this).dialog('close');
         }
      },
      open: function(){
         let pull = valueFacts[key].settings ? valueFacts[key].settings.pull : 0;
         $('#pullDi')
            .append($('<option></option>')
                    .val(0)
                    .attr('selected', pull == 0)
                    .html('None'))
            .append($('<option></option>')
                    .val(1)
                    .attr('selected', pull == 1)
                    .html('Up'))
            .append($('<option></option>')
                    .val(2)
                    .attr('selected', pull == 2)
                    .html('Down'));
      },
      close: function() {
         calSensorType = '';
         calSensorAddress = -1;
         $(this).dialog('destroy').remove();
      }
   });
}

function sensorScClone(type, address)
{
   socket.send({ "event" : "storesensorsetup", "object" : {
      'type' : type,
      'address' : parseInt(address),
      'action' : 'clone'
   }});
}

function sensorScSetup(type, address)
{
   calSensorType = type;
   calSensorAddress = parseInt(address);

   var key = toKey(calSensorType, calSensorAddress);
   var form = document.createElement("div");

   if (valueFacts[key] == null) {
      console.log("Sensor ", key, "undefined, seems not activated");
      return;
   }

   let settings = '';
   let note = '';

   if (isEmpty(valueFacts[key].settings) && valueFacts[key].parameter && valueFacts[key].parameter.options)
      settings = valueFacts[key].parameter.options.example;
   else
      settings = valueFacts[key].settings;

   if (valueFacts[key].parameter && valueFacts[key].parameter.options)
      note = valueFacts[key].parameter.options.note;

   $(form).append($('<div></div>')
                  .addClass('settingsDialogContent')
                  .append($('<span></sOptionspan>')
                          .html('Help'))
                  .append($('<span></span>')
                          .css('padding', '8px')
                          .css('white-space', 'pre-wrap')
                          .text(note))
                  .append($('<div></div>')
                          .addClass('textarea-row')
                          .append($('<span></span>')
                                  .html('Argumente (JSON)'))
                          .append($('<span></span>')
                                  .append($('<textarea></textarea>')
                                          .attr('id', 'settings')
                                          .addClass('rounded-border inputSetting inputSettingScript')
                                          .css('resize', 'none')
                                          .val(JSON.stringify(settings, undefined, 3))
                                         )))
                 );

   var title = valueFacts[key].usrtitle != '' ? valueFacts[key].usrtitle : valueFacts[key].title;

   $(form).dialog({
      modal: true,
      resizable: true,
      closeOnEscape: true,
      hide: "fade",
      width: Math.round($(window).width() * 0.8),
      height: Math.round($(window).height() * 0.5),
      title: "Skript Argumente für '" + title + "'",
      open: function() {
         calSensorType = type;
         calSensorAddress = parseInt(address);
      },
      buttons: {
         'Abbrechen': function () {
            $(this).dialog('close');
         },
         'Speichern': function () {
            socket.send({ "event" : "storesensorsetup", "object" : {
               'type':     calSensorType,
               'address':  calSensorAddress,
               'settings': JSON.parse($('#settings').val().replace(/\s\s+/g, ' '))
            }});

            $(this).dialog('close');
         }
      },
      close: function() {
         calSensorType = '';
         calSensorAddress = -1;
         $(this).dialog('destroy').remove();
      }
   });
}

async function sensorCvSetup(type, address)
{
   if (!await loadCodeMirror())
      return;

   calSensorType = type;
   calSensorAddress = parseInt(address);

   var key = toKey(calSensorType, calSensorAddress);
   var form = document.createElement("div");

   if (valueFacts[key] == null) {
      console.log("Sensor ", key, "undefined");
      return;
   }

   var luaCode = (valueFacts[key].settings && valueFacts[key].settings.lua) ? valueFacts[key].settings.lua : '';

   $(form).append($('<div></div>')
                  .addClass('settingsDialogContent')
                  .append($('<div></div>')
                          .addClass('textarea-row')
                          .append($('<span></span>')
                                  .html('Skript'))
                          .append($('<span></span>')
                                  .append($('<div></div>')
                                          .addClass('lua-cm-editor'))))
                  .append($('<div></div>')
                          .attr('id', 'luaCheckResult')
                          .css('font-size', 'large')
                          .css('margin-top', '8px')
                          .addClass('luaCheckResult'))
                 );

   var title = valueFacts[key].usrtitle != '' ? valueFacts[key].usrtitle : valueFacts[key].title;

   $(form).dialog({
      modal: true,
      resizable: true,
      closeOnEscape: true,
      hide: "fade",
      width: Math.round($(window).width() * 0.8),
      height: Math.round($(window).height() * 0.5),
      title: "LUA Skript für '" + title + "'",
      open: function() {
         calSensorType = type;
         calSensorAddress = parseInt(address);
         cvEditor = CodeMirror($(form).find('.lua-cm-editor')[0], {
            value:         luaCode,
            mode:          'lua',
            lineNumbers:   true,
            indentUnit:    3,
            tabSize:       3,
            indentWithTabs: false,
            lineWrapping:  false,
            autofocus:     true,
            extraKeys:     { 'Esc': () => $(form).dialog('close') },
         });
         cvEditor.setSize('100%', '100%');
         setTimeout(() => cvEditor.refresh(), 50);
      },
      buttons: {
         'Abbrechen': function () {
            $(this).dialog('close');
         },
         'Syntax prüfen': function () {
            $('#luaCheckResult').text('...').css('color', '');
            socket.send({ "event" : "checkluascript", "object" : {
               'lua': cvEditor.getValue()
            }});
         },
         'Speichern': function () {
            socket.send({ "event" : "storesensorsetup", "object" : {
               'type':    calSensorType,
               'address': calSensorAddress,
               'settings': {
                  'lua': cvEditor.getValue()
               }
            }});
            $(this).dialog('close');
         }
      },
      close: function() {
         cvEditor = null;
         calSensorType = '';
         calSensorAddress = -1;
         $(this).dialog('destroy').remove();
      }
   });
}

function showLuaCheckResult(obj)
{
   let div = $('#luaCheckResult');
   if (!div.length) return;

   if (cvEditor)
      cvEditor.eachLine(line => cvEditor.removeLineClass(line, 'background', 'lua-error-line'));

   if (obj.status === 0) {
      div.text('OK').css('color', 'green');
   } else {
      let msg = obj.message || 'Fehler';
      let m = msg.match(/:(\d+): (.+)/);
      if (m) {
         let line = parseInt(m[1]) - 1;
         msg = 'Zeile ' + line + ': ' + m[2];
         if (cvEditor) cvEditor.addLineClass(line, 'background', 'lua-error-line');
      }
      div.text(msg).css('color', '#ff6b6b');
   }
}

function storeSensorSetup()
{
   let jsonArray = [];
   let rootSetup = document.getElementById("ioSetupContainer");
   let elements = rootSetup.querySelectorAll("[id^='row_']");

   for (var i = 0, n = 0; i < elements.length; i++) {
      let tr = $(elements[i]).closest('tr');

      if (!$(tr).data('changed')) {
         console.log('row not changed, skipping');
         continue;
      }

      var jsonObj = {};
      var type = $(elements[i]).data("type");
      var address = $(elements[i]).data("address");
      let key = toKey(type, address);

      if (!valueFacts[key].settings)
         valueFacts[key].settings = {};

      valueFacts[key].settings.fct = $("#function_" + type + address).val();

      jsonObj["type"] = type;
      jsonObj["address"] = address;
      jsonObj["usrtitle"] = $("#usrtitle_" + type + address).val();
      jsonObj["unit"] = $("#unit_" + type + address).val();
      jsonObj["state"] = $("#state_" + type + address).is(":checked");
      jsonObj["record"] = $("#record_" + type + address).is(":checked");

      // #TODO settings needed for GPOI sensors but delete settings of all other types
      //       until valueFacts[key].settings for them are not set yet!
      //console.log("key", key, "settings:", JSON.stringify(valueFacts[key].settings));
      if (type == 'GPIO')
         jsonObj["settings"] = JSON.stringify(valueFacts[key].settings);

      jsonArray[n++] = jsonObj;
   }

   // console.log("storeSensorSetup:", JSON.stringify(jsonArray, undefined, 4));
   socket.send({ "event" : "storeiosetup", "object" : jsonArray });
}

//***************************************************************************
// GPIO 40-pin header visualization
//***************************************************************************

function showGpio()
{
   socket.send({ 'event' : "gpiodata", 'object' : { } });
}

function initGpioHeader(pins)
{
   $('#gpioState').removeClass('hidden');
   $('#ioSetupContainer').addClass('hidden');

   console.log("initGpioHeader");

   let container = $('#gpioState');
   container.empty();

   let pinMap = {};
   for (let p of pins)
      pinMap[p.pin] = p;

   let wrap = $('<div>').addClass('gpio-wrap');
   let legend = $('<div>').addClass('gpio-legend')
      .append(gpioLegendItem('gpio-pwr5',     '5V'))
      .append(gpioLegendItem('gpio-pwr33',    '3.3V'))
      .append(gpioLegendItem('gpio-gnd',      'GND'))
      .append(gpioLegendItem('gpio-usable',   'GPIO free'))
      .append(gpioLegendItem('gpio-blocked',  'GPIO used'))
      .append(gpioLegendItem('gpio-special',  'Special Functions'));

   let table = $('<table>').addClass('gpio-table');

   for (let row = 0; row < 20; row++) {
      let oddPin  = row * 2 + 1;
      let evenPin = row * 2 + 2;
      let odd  = pinMap[oddPin];
      let even = pinMap[evenPin];

      let tr = $('<tr>');

      // left side: label | dot
      tr.append(makePinLabel(odd,  'left'));
      tr.append(makePinDot(odd,  oddPin));
      tr.append(makePinDot(even, evenPin));
      tr.append(makePinLabel(even, 'right'));

      table.append(tr);
   }

   wrap.append(legend).append(table);
   container.append(wrap);
}

function gpioLegendItem(cssClass, label)
{
   return $('<div>').addClass('gpio-legend-item')
      .append($('<span>').addClass('gpio-dot ' + cssClass))
      .append($('<span>').text(label));
}

function pinCssClass(pin)
{
   if (!pin) return 'gpio-unknown';
   if (pin.name == 'GND' || pin.voltage == 'GND') return 'gpio-gnd';
   if (pin.voltage == '5V')   return 'gpio-pwr5';
   if (!pin.gpio) return pin.voltage == '3.3V' ? 'gpio-pwr33' : 'gpio-special';
   if (pin.usable)  return 'gpio-usable';
   if (pin.blocked) return 'gpio-blocked';
   return 'gpio-special';
}

function pinTooltip(pin)
{
   if (!pin) return '';
   let parts = ['Pin ' + pin.pin, pin.name];
   if (pin.description) parts.push(pin.description);
   if (pin.gpio) {
      parts.push(pin.usable ? 'frei' : (pin.blocked ? 'belegt (Kernel)' : 'nicht nutzbar'));
      if (pin.chipLabel) parts.push(pin.chipLabel + ' offset ' + pin.offset);
      if (pin.pull) parts.push('Pull up/down');
      if (pin.interrupt) parts.push('Interrupt');
   }
   parts.push(pin.voltage);
   return parts.join(' | ');
}

function makePinDot(pin, num)
{
   let cls = pinCssClass(pin);
   let tip = pinTooltip(pin);
   return $('<td>').addClass('gpio-dot-cell')
      .append($('<span>')
         .addClass('gpio-dot ' + cls)
         .attr('title', tip)
         .text(num));
}

function makePinLabel(pin, side)
{
   if (!pin) return $('<td>');
   let cls = pinCssClass(pin);
   let tip = pinTooltip(pin);
   let label = pin.description ? pin.name + ' – ' + pin.description : pin.name;
   return $('<td>')
      .addClass('gpio-pin-label gpio-pin-' + side + ' ' + cls + '-text')
      .attr('title', tip)
      .text(label);
}
