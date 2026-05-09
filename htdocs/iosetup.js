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

// ----------------------------------------------------------------
// Sensor Setup
// ----------------------------------------------------------------

function initSensorSetup()
{
   // console.log(JSON.stringify(valueFacts, undefined, 4));

   changes = false;
   hideProgressDialog();

   $('#controlContainer').removeClass('hidden');
   $('#controlToggle').removeClass('hidden');
   $('#container').removeClass('hidden');

   prepareSetupMenu();

   if (activeSection == '')
      activeSection = 'io' + valueTypes[0].title.replace(' ', '');

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
      var section = 'io' + valueTypes[i].title.replace(' ', '');

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
      .append($('<td>').css('width', '12%').text('ID'));

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
         if (valueTypes[i].type == item.type)
            sectionId = 'io' + valueTypes[i].title.replace(' ', '');
      }

      console.log("sensor", key, sectionId);

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
          .append($('<td>').text(key));

      let gpioFct = "deactivated";

      if (item.type == 'GPIO') {
         gpioFct = (item.settings && item.settings.fct && item.settings.fct != '') ? item.settings.fct : "deactivated";
         tr.append(
            $('<td>').addClass('tableMultiColCell').append(
               $('<select>').attr('id', 'function_' + id).addClass('inputSetting rounded-border')
                  .append($('<option>').val('deactivated').text('Off'))
                  .append($('<option>').val('in').text('In'))
                  .append($('<option>').val('out').text('Out'))
                  .val(gpioFct))
         );
      }

      if (item.type == 'DO' || item.type.startsWith('MCPO') ||
          item.type == 'DI' || item.type.startsWith('MCPI') ||
          item.type == 'SC' || item.type == 'VAR' ||
          item.type == 'GPIO' || item.type == 'CV' ||
          item.type == 'AI' || item.type.startsWith('ADS')) {
         tr.append($('<td>').append($('<button>')
                                    .attr('id', 'btnSensorSetup_' + id)
                                    .attr('disabled', item.type == 'GPIO' ? gpioFct == 'deactivated' : false)
                                    .addClass('buttonOptions rounded-border')
                                    .text('Setup')
                                    .on('click', () => sensorSetupDialog(item.type, item.address))));
         if (item.type == 'SC' && item.parameter && item.parameter.cloneable)
            tr.append($('<td>').append($('<button>')
                                       .addClass('buttonOptions rounded-border')
                                       .text('Clone')
                                       .on('click', () => sensorScClone(item.type, item.address))));
      }
      else if (item.type == 'W1' || item.type == 'RTL433')
         tr.append($('<td>').append($('<button>')
                                    .addClass('buttonOptions rounded-border')
                                    .text('Löschen')
                                    .on('click', () => deleteValueFact(item.type, item.address))));
      else
         tr.append($('<td>'));

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
      else if (type == 'AI')
         sensorAiSetup(type, address);
      else if (type == 'CV')
         sensorCvSetup(type, address);
      else if (type == 'DO')
         sensorDoSetup(type, address);
      else if (type == 'DI')
         sensorDiSetup(type, address);
      else if (type == 'SC')
         sensorScSetup(type, address);
      else if (type == 'VAR')
         sensorVarSetup(type, address);
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
   console.log("sensor delete ", type, address);

   socket.send({ "event" : "storesensorsetup", "object" : {
      'type' : type,
      'address' : parseInt(address),
      'action' : 'delete'
   }});
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

function sensorVarSetup(type, address)
{
   calSensorType = type;
   calSensorAddress = address;

   let key = toKey(calSensorType, parseInt(calSensorAddress));
   let form = document.createElement("div");

   if (valueFacts[key] == null) {
      console.log("Sensor ", key, "undefined");
      return;
   }

   $(form).append($('<div></div>')
                  .addClass('settingsDialogContent')
                  .append($('<span></span>')
                          .css('width', 'auto')
                          .html('Skript'))
                  .append($('<span></span>')
                          .append($('<textarea></textarea>')
                                  .attr('id', 'scriptVar')
                                  .addClass('rounded-border inputSetting inputSettingScript')
                                  .css('height', '100px')
                                  .val(valueFacts[key].settings ? valueFacts[key].settings.script : '')
                                 )));
   var title = valueFacts[key].usrtitle != '' ? valueFacts[key].usrtitle : valueFacts[key].title;

   $(form).dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      width: "500px",
      hide: "fade",
      title: "DO settings '" + title + '"',
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
               'type': calSensorType,
               'address': parseInt(calSensorAddress),
               'settings': {
                  'script': $('#scriptVar').val()
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

function sensorDoSetup(type, address)
{
   calSensorType = type;
   calSensorAddress = address;

   let key = toKey(calSensorType, parseInt(calSensorAddress));
   let form = document.createElement("div");

   if (valueFacts[key] == null) {
      console.log("Sensor ", key, "undefined");
      return;
   }

   $(form).append($('<div></div>')
                  .addClass('settingsDialogContent')
                  .append($('<div></div>')
                          .append($('<span></span>')
                                  .html('Invertieren'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'invertDo')
                                          .attr('type', 'checkbox')
                                          .addClass('rounded-border inputSetting')
                                          .prop('checked', valueFacts[key].settings ? valueFacts[key].settings.invert : true))
                                  .append($('<label></label>')
                                          .prop('for', 'invertDo'))))
                  .append($('<div></div>')
                          .append($('<span></span>')
                                  .html('Impuls (50ms)'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'impulseDo')
                                          .attr('type', 'checkbox')
                                          .addClass('rounded-border inputSetting')
                                          .prop('checked', valueFacts[key].settings ? valueFacts[key].settings.impulse : false))
                                  .append($('<label></label>')
                                          .prop('for', 'impulseDo'))))
                  .append($('<div></div>')
                          .append($('<span></span>')
                                  .html('Feedback Input'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'feedbackIo')
                                          .attr('type', 'search')
                                          .addClass('rounded-border inputSetting')
                                          .val(valueFacts[key].settings && valueFacts[key].settings.feedbackInType ?
                                               valueFacts[key].settings.feedbackInType
                                               + ':0x'
                                               + valueFacts[key].settings.feedbackInAddress.toString(16) : ''))))
                  .append($('<div></div>')
                          .append($('<span></span>')
                                  .css('width', 'auto')
                                  .html('Skript'))
                          .append($('<span></span>')
                                  .append($('<textarea></textarea>')
                                          .attr('id', 'scriptDo')
                                          .addClass('rounded-border inputSetting inputSettingScript')
                                          .css('height', '100px')
                                          .val(valueFacts[key].settings ? valueFacts[key].settings.script : '')
                                         )))
                         );

   var title = valueFacts[key].usrtitle != '' ? valueFacts[key].usrtitle : valueFacts[key].title;

   $(form).dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      width: "500px",
      hide: "fade",
      title: "DO settings '" + title + '"',
      open: function() {
         calSensorType = type;
         calSensorAddress = address;
      },
      buttons: {
         'Abbrechen': function () {
            $(this).dialog('close');
         },
         'Speichern': function () {

            let addr = parseInt($('#feedbackIo').val().split(":")[1]);
            let type = $('#feedbackIo').val().split(":")[0];

            socket.send({ "event" : "storesensorsetup", "object" : {
               'type': calSensorType,
               'address': parseInt(calSensorAddress),
               'settings': {
                  'invert': $('#invertDo').is(':checked'),
                  'impulse': $('#impulseDo').is(':checked'),
                  'script': $('#scriptDo').val(),
                  'feedbackInType': type,
                  'feedbackInAddress': addr,
                  'fct' : valueFacts[key].settings ? valueFacts[key].settings.fct : ''
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
      console.log("Sensor ", key, "undefined");
      return;
   }

   $(form).append($('<div></div>')
                  .addClass('settingsDialogContent')
                  .append($('<div></div>')
                          .append($('<span></span>')
                                  .css('width', 'auto')
                                  .html('Argumente (JSON)'))
                          .append($('<span></span>')
                                  .append($('<textarea></textarea>')
                                          .attr('id', 'settings')
                                          .addClass('rounded-border inputSetting inputSettingScript')
                                          .css('height', '100px')
                                          .val(JSON.stringify(valueFacts[key].settings, undefined, 3))
                                         )))
                 );

   var title = valueFacts[key].usrtitle != '' ? valueFacts[key].usrtitle : valueFacts[key].title;

   $(form).dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      hide: "fade",
      width: "80%",
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
               'settings': $('#settings').val().replace(/\s\s+/g, ' ')
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

function sensorCvSetup(type, address)
{
   calSensorType = type;
   calSensorAddress = parseInt(address);

   var key = toKey(calSensorType, calSensorAddress);
   var form = document.createElement("div");

   if (valueFacts[key] == null) {
      console.log("Sensor ", key, "undefined");
      return;
   }

   $(form).append($('<div></div>')
                  .addClass('settingsDialogContent')
                  .append($('<div></div>')
                          .append($('<span></span>')
                                  .css('width', 'auto')
                                  .html('Skript'))
                          .append($('<span></span>')
                                  .append($('<textarea></textarea>')
                                          .attr('id', 'settings')
                                          .addClass('rounded-border inputSetting inputSettingScript')
                                          .css('height', '100px')
                                          .val((valueFacts[key].settings && valueFacts[key].settings.lua) ? valueFacts[key].settings.lua : '')
                                         )))
                 );

   var title = valueFacts[key].usrtitle != '' ? valueFacts[key].usrtitle : valueFacts[key].title;

   $(form).dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      hide: "fade",
      width: "80%",
      title: "LUA Skript für '" + title + "'",
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
               'type':    calSensorType,
               'address': calSensorAddress,
               'settings': {
                  'lua': $('#settings').val()
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
      jsonObj["settings"] = JSON.stringify(valueFacts[key].settings);
      // jsonObj["groupid"] = parseInt($("#group_" + type + address).val());

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
