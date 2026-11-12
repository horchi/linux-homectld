/*
 *  schema.js
 *
 *  (c) 2020 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

var oTop = 0;
var oLeft = 0;
var lastSchemadata = null;
var schemaDirty = false;    // unsaved changes in the edit mode
var schemaPlacingEnd = null; // ends the active placing by the mouse, see schemaEndPlacing()
var schemaSet = '';          // '' -> the schema of the main menu, 'p4' -> the schema of the heating (p4/schema.js)
var schemaScale = 1;         // the picture is fitted into the page (schemaFit), all positions are stored in pixel of the picture
var schemaZoom = 1;          // zoom above the fitted size (pinch on the phone, Ctrl + wheel), 1 -> fitted
var schemaZoomMax = 6;

// the elements of the heating schema have the prefix P4 like its sensors (the table schemaconf is keyed by type and address)

function schemaTypeUC()  { return schemaSet == 'p4' ? 'P4UC' : 'UC'; }       // user constant (text)
function schemaTypeCN()  { return schemaSet == 'p4' ? 'P4CN' : 'CN'; }       // connection (line)
function isSchemaUC(type)  { return type == 'UC' || type == 'P4UC'; }
function isSchemaCN(type)  { return type == 'CN' || type == 'P4CN'; }

function schemaImage()
{
   return schemaSet == 'p4' ? 'img/p4/schema/schema-' + config.p4Schema + '.png'
                            : 'img/schema/schema-' + config.schema + '.png';
}
var properties = [ "top", "left", "color", "font-size", "color", "background-color", "border", "border-radius", "z-index"];


// an element is part of the schema as soon as it has a position, the sensors without one are offered by 'Sensor hinzufügen'

function isSchemaPlaced(schemaDef)
{
   if (schemaDef.properties == null)
      return false;

   if (isSchemaCN(schemaDef.type))
      return schemaLinePoints(schemaDef).length >= 2;

   return schemaDef.properties['top'] != null && schemaDef.properties['top'] !== '';
}

function schemaElementId(schemaDef)
{
   return schemaDef.type + ((schemaDef.address)>>>0).toString(10);
}

function initSchema(schemaData)
{
   // console.log(JSON.stringify(schemaData, undefined, 4));
   schemaEndPlacing();                  // a still active placing by the mouse ends with the rebuild
   $(document).off('.schemaLineDrag');
   lastSchemadata = schemaData;

   schemaEditMenu();
   schemaZoom = 1;

   // the schema keeps the size of the picture and is scaled by a CSS transform into the outer box,
   //  so the elements and lines keep their coordinates (pixel of the picture), only the mouse positions are converted

   $('#container').empty()
      .removeClass('hidden')
      .append($('<div></div>')
              .attr('id', 'schemaScaler')
              .addClass('schemaScaler')
              .append($('<div></div>')
                      .attr('id', 'schemaContainer')
                      .addClass('rounded-border schemaBox')
                      .on('drop', function(event) { dropSValue(event); })       // on the container, so also above other elements and lines
                      .on('dragover', function(event) { return false; })
                      .append($('<img></img>')
                              .attr('src', schemaImage())
                              .attr('draggable', false)
                              .on('load', schemaFit))
                     ));

   for (let i = 0; i < schemaData.length; i++) {
      if (!lastSchemadata[i].deleted && isSchemaPlaced(lastSchemadata[i]))
         initSchemaElement(schemaData[i], i);
   }

   updateSchema();
   schemaZoomEvents();

   $("#container").height($(window).height() - getTotalHeightOf('menu') - 8);
   schemaFit();

   window.onresize = function() {
      $("#container").height($(window).height() - getTotalHeightOf('menu') - 8);
      schemaFit();
   };
}

// fit the schema into the page keeping the aspect ratio (also enlarged), called after loading the picture and on resize

function schemaFit()
{
   let box = document.getElementById('schemaContainer');
   let scaler = document.getElementById('schemaScaler');
   let img = box ? box.querySelector('img') : null;

   if (!box || !scaler || !img || !img.naturalWidth)
      return;

   let container = document.getElementById('container');
   let margin = 8;                                   // margin of the outer box (base.css, schemaScaler)
   let width = box.offsetWidth;                      // the size without the transform
   let height = box.offsetHeight;
   let scale = Math.min((container.clientWidth - margin) / width, (container.clientHeight - margin) / height);

   if (!isFinite(scale) || scale <= 0)
      scale = 1;

   schemaScale = scale * schemaZoom;
   scale = schemaScale;
   box.style.transform = 'scale(' + scale + ')';
   scaler.style.width = Math.floor(width * scale) + 'px';
   scaler.style.height = Math.floor(height * scale) + 'px';
}

function dropSValue(event)
{
   event.preventDefault();
   let id = event.originalEvent.dataTransfer.getData('id');
   let schemaDef = getSchemDef(id);

   if (!schemaDef)
      return;

   let pos = schemaMousePos(event.originalEvent);
   let top = Math.max(0, pos.y - oTop) + 'px';
   let left = Math.max(0, pos.x - oLeft) + 'px';

   schemaDef.properties["top"] = top;
   schemaDef.properties["left"] = left;
   $('#' + id).css({ 'top' : top, 'left' : left });
   schemaDirty = true;
}

//***************************************************************************
// Edit Mode
//  - the menu line below the menu, shown only in the edit mode
//***************************************************************************

function schemaEditMenu()
{
   $('#schemaEditMenu').remove();

   if (!schemaEditActive)
      return;

   let addButton = function(icon, title, hint, fct) {
      $('#schemaEditMenu').append($('<button></button>')
                                  .addClass('rounded-border button2 mdi ' + icon)
                                  .attr('title', hint)
                                  .html(' ' + title)
                                  .click(fct));
   };

   $('#navMenu').append($('<div></div>')
                        .attr('id', 'schemaEditMenu')
                        .addClass('setupMenu schemaEditMenu'));

   addButton('mdi-thermometer-plus', 'Sensor hinzufügen', 'Einen Sensor in das Schema aufnehmen', schemaAddSensor);
   addButton('mdi-format-text', 'Template hinzufügen', 'Ein Template (Text, Bild oder JavaScript Funktion ohne eigenen Sensor) hinzufügen', schemaAddItem);
   addButton('mdi-vector-line', 'Leitung hinzufügen', 'Eine Leitung zeichnen: Klick setzt Start und Eckpunkte, Doppelklick beendet', schemaAddLine);
   addButton('mdi-content-save', 'Speichern', 'Das Schema speichern', schemaStore);
   addButton('mdi-close', 'Beenden', 'Den Edit Mode verlassen', schemaLeaveEdit);

   if (typeof initScrollStrip == 'function')
      initScrollStrip('schemaEditMenu');
}

// 'Setup Schema' / 'Stop Setup' of the burger menu

function setupSchema()
{
   $("#burgerPopup").dialog("close");

   if (schemaEditActive)
      return schemaLeaveEdit();

   schemaEditActive = true;
   schemaDirty = false;
   initSchema(lastSchemadata || []);
}

function schemaLeaveEdit()
{
   let leave = function() {
      schemaEditActive = false;
      initSchema(lastSchemadata);
   };

   if (!schemaDirty)
      return leave();

   $('<div></div>')
      .append($('<div></div>')
              .addClass('dialog-content')
              .html('Das Schema wurde geändert, die Änderungen speichern?'))
      .dialog({
         modal: true,
         title: 'Schema',
         width: 'auto',
         resizable: false,
         hide: 'fade',
         buttons: {
            'Speichern': function() {
               $(this).dialog('close');
               schemaStore();
               leave();
            },
            'Verwerfen': function() {
               $(this).dialog('close');
               schemaEditActive = false;
               schemaDirty = false;
               schemaEditMenu();
               socket.send({ 'event' : 'schema', 'object' : { 'set' : schemaSet } });   // reload the stored one
            },
            'Abbrechen': function() { $(this).dialog('close'); }
         },
         close: function() { $(this).dialog('destroy').remove(); }
      });
}

function getSchemDef(id)
{
   for (let i = 0; i < lastSchemadata.length; i++) {
      if (lastSchemadata[i].type + ((lastSchemadata[i].address)>>>0).toString(10) == id)
         return lastSchemadata[i];
   }
}

function getItemDef(id)
{
   let idPar = id.split(':');

   if (idPar.length == 2) {
      for (let key in valueFacts) {
         if (idPar[0] == valueFacts[key].type && parseInt(idPar[1]) == valueFacts[key].address) {
            return valueFacts[key];
         }
      }
   }

   return { name: "-", title :  "-", unit : "", usrtitle : "--" };
}

function getItemById(id)
{
   for (let key in allSensors) {
      if (allSensors[key].type + ((allSensors[key].address)>>>0).toString(10) == id)
         return allSensors[key];
   }
}

function getItem(id)
{
   let idPar = id.split(':');

   if (idPar.length == 2) {
      for (let key in allSensors) {
         if (idPar[0] == allSensors[key].type && parseInt(idPar[1]) == allSensors[key].address) {
            return allSensors[key];
         }
      }
   }

   return { 'value' : 'unkown id', 'title' : 'unkown', 'name' : 'unkown', type : 'NN', address : -1 };
}

function initSchemaElement(item, tabindex)
{
   if (isSchemaCN(item.type))
      return schemaLineRender(item);

   let id = item.type + (item.address >>> 0).toString(10);

   $('#schemaContainer')
      .append($('<div></div>')
              .attr('id', id)
              .attr('tabindex', tabindex)
              .addClass('schemaDiv')
              .css('visibility', schemaEditActive || item.state == 'A' ? 'visible' : 'hidden')
              .css('cursor', schemaEditActive ? 'move' : 'default')
              .css('position', 'absolute')
              .data('type', item.type)
              .data('changedAt', '#new')              // force the first update of the content
              .data('address', (item.address >>> 0).toString(10))
             );

   setAttributeStyleByProperties('#'+id, item.properties);

   if (schemaEditActive) {
      if (item.state != 'A')
         $('#' + id).addClass('schemaDivHidden');     // not displayed outside of the edit mode

      $('#' + id).click(function() { editSchemaValue(item.type, item.address); });
      $('#' + id).attr('draggable', true);
      $('#' + id).on('dragstart', function(event) {
         event.originalEvent.dataTransfer.setData('id', item.type + (item.address >>> 0).toString(10));
      });

      $('#' + id).on('mousedown', function(event) {
         oTop = event.offsetY;
         oLeft = event.offsetX;
      });

      // arrow keys move the element (with Ctrl by 10 pixel)

      $('#' + id).on('keydown', function(event) {
         if (event.keyCode < 37 || event.keyCode > 40)
            return;

         event.preventDefault();

         let schemaDef = getSchemDef($(this).attr('id'));
         let pixel = event.ctrlKey ? 10 : 1;
         let top = parseInt($(this).css('top')) + (event.keyCode == 38 ? -pixel : event.keyCode == 40 ? pixel : 0);
         let left = parseInt($(this).css('left')) + (event.keyCode == 37 ? -pixel : event.keyCode == 39 ? pixel : 0);

         schemaDef.properties["top"] = top + 'px';
         schemaDef.properties["left"] = left + 'px';
         $(this).css('top', top + 'px');
         $(this).css('left', left + 'px');
         schemaDirty = true;
      });
   }
}

function updateSchema()
{
   if (!lastSchemadata)
      return;

   for (let i = 0; i < lastSchemadata.length; i++) {
      let schemaDef = lastSchemadata[i];

      if (!schemaDef || schemaDef.deleted || !isSchemaPlaced(schemaDef))
         continue;

      if (isSchemaCN(schemaDef.type)) {
         schemaLineUpdate(schemaDef);         // the flow of the line
         continue;
      }

      let id = schemaDef.type + (schemaDef.address >>> 0).toString(10);
      var item = getItemById(id);
      let key = toKey(schemaDef.type, schemaDef.address);
      let fact = valueFacts[key];

      if (item && $("#"+id).data('changedAt') == item.changedAt)
         continue;

      // if (!item)
      //   console.log("no item for", id, key);

      $("#"+id).data('changedAt', item ? item.changedAt : '');

      let theText = '';

      if (schemaDef.fct != null && schemaDef.fct != '') {
         try {
            theText += eval(schemaDef.fct);
         }
         catch (err) {
            showInfoDialog({'message' : "Fehler in JS Funktion: '" + err.message + ", " + schemaDef.fct + "'", 'status': -1});
            schemaDef.fct = "";  // delete buggy function
         }
         // console.log("result of '" + schemaDef.fct + "' is " + result);
      }
      else if (schemaDef.usrtext != null)
         theText += schemaDef.usrtext;

      let html = '';

      // the title is part of the valuefacts, the sensor data (item) don't have it

      let sensorTitle = (fact && (fact.usrtitle || fact.title)) || (item != null && item.title) || '';

      if (schemaDef.showtitle && sensorTitle != '')
         html += sensorTitle + ":&nbsp;";

      if (schemaDef.kind == 2) {
         if (theText == '' && item != null)
            theText = item.image;
         // else if (item == null)
         //    console.log("Missing item for " + JSON.stringify(schemaDef, undefined, 4));

         if (theText != '') {
            let style = 'width:' + (schemaDef.width ? schemaDef.width : 40) + 'px;';
            if (schemaDef.height)
               style = 'height:' + schemaDef.height + 'px';

            html += '<img style="' + style + '" src="' + theText + '"></img>';
         }
      }
      else {
         html += theText;
      }

      if ((schemaDef.fct == null || schemaDef.fct == '') && item != null) {
         if (schemaDef.kind == 0)
            html += item.value;
         else if (schemaDef.kind == 1)
            html += item.text || '';

         html += schemaDef.showunit ? (fact && fact.unit) || "" : "";
      }
      else {
         if(schemaDef.fct == null && schemaDef.fct == "" && item == null)
             html += " item: '" + id + "' is null";
      }

      $("#"+id).html(html == "" ? "&nbsp;" : html);

      $("#"+id).attr("title", schemaElementTitle(schemaDef));
   }
}

// the title of an element for the mouse hint and the edit dialog: '<title> - <name of the sensor> (<id>)'

function schemaElementTitle(schemaDef)
{
   let title = "";

   if (isSchemaUC(schemaDef.type))
      title = "Template";
   else if (isSchemaCN(schemaDef.type))
      title = "Leitung";
   else {
      let item = getItemById(schemaElementId(schemaDef));
      let fact = valueFacts[toKey(schemaDef.type, schemaDef.address)];

      title = (item != null && item.title) || (fact && (fact.usrtitle || fact.title)) || '';

      if (fact && fact.name && fact.name != title)
         title += (title != '' ? ' - ' : '') + fact.name;
   }

   return (title != '' ? title + ' ' : '') + "(" + schemaDef.type + ":0x" + ((schemaDef.address)>>>0).toString(16) + ")";
}

// store, the elements without a position (rows created automatically by older versions) and the deleted ones are removed

function schemaStore()
{
   let data = [];

   for (let i = 0; i < lastSchemadata.length; i++) {
      let schemaDef = Object.assign({}, lastSchemadata[i]);

      if (!isSchemaPlaced(schemaDef))
         schemaDef.deleted = true;

      data.push(schemaDef);
   }

   socket.send({ "event" : "storeschema", "object" : data });
   schemaDirty = false;
}

// zoom to 'zoom', the point of the schema below clientX / clientY stays there (the container scrolls)

function schemaZoomAt(zoom, clientX, clientY)
{
   let container = document.getElementById('container');
   let scaler = document.getElementById('schemaScaler');

   if (!container || !scaler)
      return;

   let cRect = container.getBoundingClientRect();
   let sRect = scaler.getBoundingClientRect();
   let offX = sRect.left - cRect.left + container.scrollLeft;      // position of the schema in the scrolled area
   let offY = sRect.top - cRect.top + container.scrollTop;
   let x = (clientX - sRect.left) / schemaScale;                   // the point in pixel of the picture
   let y = (clientY - sRect.top) / schemaScale;

   schemaZoom = Math.min(schemaZoomMax, Math.max(1, zoom));
   schemaFit();

   container.scrollLeft = offX + x * schemaScale - (clientX - cRect.left);
   container.scrollTop = offY + y * schemaScale - (clientY - cRect.top);
}

// pinch with two fingers zooms (one finger scrolls the container as usual), double tap zooms in / back to the
//  fitted size (not in the edit mode, the double click is used there), Ctrl + wheel (also the pinch of a touchpad)

function schemaZoomEvents()
{
   let scaler = document.getElementById('schemaScaler');
   let pinch = null;
   let lastTap = 0;

   let dist = function(t) { return Math.hypot(t[0].clientX - t[1].clientX, t[0].clientY - t[1].clientY); };

   scaler.addEventListener('touchstart', function(event) {
      if (event.touches.length == 2) {
         pinch = { dist: dist(event.touches), zoom: schemaZoom };
         event.preventDefault();
         return;
      }

      if (event.touches.length != 1 || schemaEditActive)
         return;

      let now = Date.now();

      if (now - lastTap < 300) {
         let t = event.touches[0];
         schemaZoomAt(schemaZoom > 1 ? 1 : 2.5, t.clientX, t.clientY);
         event.preventDefault();
         now = 0;
      }

      lastTap = now;
   }, { passive: false });

   scaler.addEventListener('touchmove', function(event) {
      if (!pinch || event.touches.length != 2)
         return;

      event.preventDefault();
      let t = event.touches;
      schemaZoomAt(pinch.zoom * dist(t) / pinch.dist, (t[0].clientX + t[1].clientX) / 2, (t[0].clientY + t[1].clientY) / 2);
   }, { passive: false });

   scaler.addEventListener('touchend', function(event) {
      if (event.touches.length < 2)
         pinch = null;
   });

   scaler.addEventListener('gesturestart', function(event) { event.preventDefault(); });   // Safari: no zoom of the page

   scaler.addEventListener('wheel', function(event) {
      if (!event.ctrlKey)
         return;

      event.preventDefault();
      schemaZoomAt(schemaZoom * Math.exp(-event.deltaY / 200), event.clientX, event.clientY);
   }, { passive: false });
}

// the position of a new element: top left of the visible area, shifted if there is already one

function schemaNewPosition()
{
   let top = Math.round($('#container').scrollTop() / schemaScale) + 20;
   let left = Math.round($('#container').scrollLeft() / schemaScale) + 20;
   let used = function() {
      return lastSchemadata.some(function(d) {
         return !d.deleted && isSchemaPlaced(d) && parseInt(d.properties['top']) == top && parseInt(d.properties['left']) == left;
      });
   };

   while (used()) {
      top += 20;
      left += 20;
   }

   return { top: top + 'px', left: left + 'px' };
}

// first address of the type not used by an element (also not by a deleted one, it is still in the list until the reload)

function schemaFreeAddress(type)
{
   let addr = 0;

   while (getSchemDef(type + addr))
      addr++;

   return addr;
}

// the list of the active sensors not yet in the schema: select one (click and 'Hinzufügen' or double click),
//  the dialog is closed and the sensor follows the mouse until it is placed by a click

function schemaAddSensor()
{
   let facts = [];

   for (let key in valueFacts) {
      let fact = valueFacts[key];

      if (!fact.state || (schemaSet == 'p4') != fact.type.startsWith('P4'))
         continue;

      let schemaDef = getSchemDef(schemaElementId(fact));

      if (schemaDef && !schemaDef.deleted && isSchemaPlaced(schemaDef))
         continue;

      facts.push(fact);
   }

   let title = function(fact) { return fact.usrtitle || fact.title || fact.name || ''; };

   facts.sort(function(a, b) { return title(a).localeCompare(title(b)); });

   let list = $('<div></div>').addClass('schemaSensorList');
   let selected = null;

   let add = function() {
      if (!selected)
         return;

      $('#schemaSensorDlg').dialog('close');
      schemaPlaceSensor(selected);
   };

   for (const fact of facts) {
      let id = fact.type + ':0x' + ((fact.address)>>>0).toString(16);

      list.append($('<div></div>')
                  .data('search', (title(fact) + ' ' + (fact.name || '') + ' ' + id).toLowerCase())
                  .append($('<span></span>').text(title(fact)))
                  .append($('<span></span>').text(fact.name && fact.name != title(fact) ? fact.name : ''))
                  .append($('<span></span>').text(id))
                  .click(function() {
                     selected = fact;
                     list.children().removeClass('selected');
                     $(this).addClass('selected');
                  })
                  .dblclick(function() {
                     selected = fact;
                     add();
                  }));
   }

   if (!facts.length)
      list.append($('<div></div>').append($('<span></span>').text('Alle aktiven Sensoren sind bereits im Schema')));

   $('<div></div>')
      .attr('id', 'schemaSensorDlg')
      .append($('<input></input>')
              .attr('type', 'search')
              .attr('placeholder', 'Suche (Titel, Name, ID) ...')
              .addClass('rounded-border input schemaSensorFilter')
              .on('input', function() {
                 let words = $(this).val().toLowerCase().split(' ').filter(function(w) { return w != ''; });

                 list.children().each(function() {
                    let search = $(this).data('search');
                    let show = search == null || words.every(function(w) { return search.includes(w); });

                    $(this).toggle(show);

                    if (!show && $(this).hasClass('selected')) {     // never add a sensor which is not visible
                       $(this).removeClass('selected');
                       selected = null;
                    }
                 });
              }))
      .append(list)
      .dialog({
         modal: true,
         title: 'Sensor hinzufügen',
         width: Math.min(700, $(window).width() - 20),
         hide: 'fade',
         buttons: {
            'Hinzufügen': add,
            'Abbrechen': function() { $(this).dialog('close'); }
         },
         close: function() { $(this).dialog('destroy').remove(); }
      });
}

function schemaPlaceSensor(fact)
{
   let id = schemaElementId(fact);
   let schemaDef = getSchemDef(id);

   if (!schemaDef) {
      schemaDef = { type: fact.type, address: fact.address };
      lastSchemadata.push(schemaDef);
   }

   let pos = schemaNewPosition();

   schemaDef.deleted = false;
   schemaDef.state = 'A';
   schemaDef.kind = 0;
   schemaDef.showtitle = 1;
   schemaDef.showunit = 1;
   schemaDef.usrtext = '';
   schemaDef.fct = '';
   schemaDef.properties = { top: pos.top, left: pos.left };

   $('#' + id).remove();
   initSchemaElement(schemaDef, lastSchemadata.indexOf(schemaDef));
   updateSchema();
   schemaDirty = true;
   schemaPlaceByMouse(schemaDef);
}

// the element follows the mouse until it is placed by a click (or Escape, then it stays where it is),
//  'onPlaced' (optional) is called afterwards

function schemaPlaceByMouse(schemaDef, onPlaced)
{
   let element = $('#' + schemaElementId(schemaDef));
   let container = $('#schemaContainer');

   let done = function() {
      schemaPlacingEnd = null;
      container.off('.schemaPlace').removeClass('schemaPlacing');
      $(document).off('.schemaPlace');
      element.removeClass('schemaDivPlacing');
   };

   let move = function(event) {
      let pos = schemaMousePos(event);
      let top = pos.y;
      let left = pos.x;

      schemaDef.properties['top'] = top + 'px';
      schemaDef.properties['left'] = left + 'px';
      element.css({ 'top' : top + 'px', 'left' : left + 'px' });
   };

   schemaEndPlacing();                        // a still active placing ends here
   schemaPlacingEnd = done;
   element = $('#' + schemaElementId(schemaDef));
   element.addClass('schemaDivPlacing');      // without pointer events, the click goes to the container
   container.addClass('schemaPlacing');

   container.on('mousemove.schemaPlace', move);
   container.on('click.schemaPlace', function(event) {
      move(event);
      done();
      element.focus();                        // fine tuning with the arrow keys

      if (onPlaced)
         onPlaced();
   });
   $(document).on('keydown.schemaPlace', function(event) {
      if (event.keyCode != 27)
         return;

      done();

      if (onPlaced)
         onPlaced();
   });
}

function schemaAddItem()
{
   let pos = schemaNewPosition();

   let schemaDef = { type: schemaTypeUC(),      // User Constant
                     address: schemaFreeAddress(schemaTypeUC()),
                     state: "A",
                     properties: { top: pos.top, left: pos.left },
                     kind: 0,
                     showtitle: 1,
                     showunit: 0 };

   lastSchemadata.push(schemaDef);
   initSchemaElement(schemaDef, lastSchemadata.length);
   updateSchema();
   schemaDirty = true;

   // place it by the mouse, then the dialog for the content ('Abbrechen' there removes the new template)

   schemaPlaceByMouse(schemaDef, function() { editSchemaValue(schemaDef.type, schemaDef.address, true); });
}

function setAttributeStyleByProperties(id, json)
{
   if (json && json['border'] != null)
      $(id).css('border', json['border']);      // the shorthand first, it resets border-color and border-width

   for (let key in json) {
      if (key != 'border')
         $(id).css(key, json[key]);
   }
}

//***************************************************************************
// Lines (type CN / P4CN)
//  - a polyline with any number of points, drawn as SVG above the picture, so one line can go around corners
//  - properties: points 'x,y x,y ...' (pixel of the picture), color, flowcolor, width, speed, z-index
//  - kind: the direction of the flow (3 start -> end, 4 end -> start, 5 no flow)
//  - fct (optional): JavaScript condition of the flow (also in the edit mode), false/0 stops it, a negative number reverses it
//  - the flow is a dash pattern animated by CSS (base.css, schemaLineFlow)
//***************************************************************************

var schemaLineSpeeds = { 1: 15, 2: 35, 3: 70 };     // pixel per second
var schemaLineSnap = 8;                             // pixel, snap to the horizontal / vertical of the neighbour points

// the style of the next new line, the one of the last edited

var schemaLineStyle = { 'color': 'rgb(70, 130, 180)', 'flowcolor': 'rgb(255, 255, 255)', 'width': 6, 'speed': 2, 'z-index': '50' };

function schemaLinePoints(schemaDef)
{
   let str = (schemaDef.properties && schemaDef.properties['points']) || '';

   return str.split(' ').filter(function(p) { return p != ''; }).map(function(p) {
      let xy = p.split(',');
      return { x: parseInt(xy[0]) || 0, y: parseInt(xy[1]) || 0 };
   });
}

function schemaLineSetPoints(schemaDef, points)
{
   schemaDef.properties['points'] = points.map(function(p) { return p.x + ',' + p.y; }).join(' ');
}

// the position of the mouse in the picture (pixel of the picture, the schema is scaled, see schemaFit)

function schemaMousePos(event)
{
   let offset = $('#schemaContainer').offset();

   return { x: Math.max(0, Math.round((event.pageX - offset.left) / schemaScale)),
            y: Math.max(0, Math.round((event.pageY - offset.top) / schemaScale)) };
}

// snap to the horizontal / vertical of the neighbour points, so straight pipes are easy to draw

function schemaLineSnapPoint(pos, prev, next)
{
   let point = { x: pos.x, y: pos.y };

   for (const n of [prev, next]) {
      if (!n)
         continue;

      if (Math.abs(pos.x - n.x) <= schemaLineSnap)
         point.x = n.x;
      if (Math.abs(pos.y - n.y) <= schemaLineSnap)
         point.y = n.y;
   }

   return point;
}

// draw the line, 'points' (optional) instead of the stored ones while it is created

function schemaLineRender(schemaDef, points)
{
   let id = schemaElementId(schemaDef);
   let svg = document.getElementById(id);
   let props = schemaDef.properties || {};
   let attr = function(value) { return String(value).replace(/["<>&]/g, ''); };

   if (!points)
      points = schemaLinePoints(schemaDef);

   if (!svg) {
      svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
      svg.setAttribute('id', id);
      svg.setAttribute('class', 'schemaLine');
      document.getElementById('schemaContainer').appendChild(svg);

      if (schemaEditActive)
         schemaLineEditEvents(svg, schemaDef);
   }

   let width = Math.max(1, parseInt(props['width']) || schemaLineStyle['width']);
   let dash = Math.max(3, width * 1.5);
   let period = dash * 2.5;
   let duration = period / (schemaLineSpeeds[props['speed']] || schemaLineSpeeds[2]);
   let str = points.map(function(p) { return p.x + ',' + p.y; }).join(' ');

   let html = '<polyline class="schemaLinePipe" points="' + str + '" stroke="' + attr(props['color'] || schemaLineStyle['color']) + '"' +
       ' stroke-width="' + width + '"/>' +
       '<polyline class="schemaLineFlow" points="' + str + '" stroke="' + attr(props['flowcolor'] || schemaLineStyle['flowcolor']) + '"' +
       ' stroke-width="' + Math.max(1, width * 0.4) + '" stroke-dasharray="' + dash + ' ' + (period - dash) + '"' +
       ' style="--schemaDash: ' + period + 'px; animation-duration: ' + duration.toFixed(2) + 's"/>';

   if (schemaEditActive) {
      // the area to grab the line, the handles of the points and (smaller) in the middle of the segments to add a point

      html += '<polyline class="schemaLineHit" points="' + str + '" stroke-width="' + Math.max(16, width + 8) + '">' +
         '<title>' + attr(schemaElementTitle(schemaDef)) + '</title></polyline>';

      for (let i = 0; i < points.length - 1; i++) {
         let a = points[i], b = points[i+1];

         if (Math.abs(a.x - b.x) + Math.abs(a.y - b.y) > 30)
            html += '<circle class="schemaLineMid" data-seg="' + i + '" cx="' + (a.x + b.x) / 2 + '" cy="' + (a.y + b.y) / 2 + '" r="4">' +
               '<title>Ziehen fügt einen Eckpunkt ein</title></circle>';
      }

      for (let i = 0; i < points.length; i++)
         html += '<circle class="schemaLineHandle' + (i == 0 ? ' start' : '') + '" data-idx="' + i + '" cx="' + points[i].x + '" cy="' + points[i].y + '" r="6">' +
            '<title>' + (i == 0 ? 'Start der Leitung. ' : '') + 'Ziehen verschiebt den Punkt, Doppelklick entfernt ihn</title></circle>';
   }

   svg.innerHTML = html;
   svg.style.zIndex = props['z-index'] || schemaLineStyle['z-index'];
   svg.style.visibility = schemaEditActive || schemaDef.state == 'A' ? 'visible' : 'hidden';
   svg.classList.toggle('schemaLineHidden', schemaEditActive && schemaDef.state != 'A');

   schemaLineUpdate(schemaDef);
}

// the state of the flow, called with every update of the sensor data

function schemaLineUpdate(schemaDef)
{
   let flow = document.querySelector('#' + schemaElementId(schemaDef) + ' .schemaLineFlow');

   if (!flow)
      return;

   let direction = schemaDef.kind == 5 ? 0 : (schemaDef.kind == 4 ? -1 : 1);

   if (direction != 0 && schemaDef.fct != null && schemaDef.fct != '' && schemaDef.fctFailed != schemaDef.fct) {
      let result = 0;

      try {
         result = eval(schemaDef.fct);
      }
      catch (err) {
         showInfoDialog({'message' : "Fehler im JavaScript der Leitung: '" + err.message + ", " + schemaDef.fct + "'", 'status': -1});
         schemaDef.fctFailed = schemaDef.fct;   // reported once and ignored until it is changed, it is not deleted (edit mode!)
         result = 1;
      }

      if (!result)
         direction = 0;
      else if (typeof result == 'number' && result < 0)
         direction = -direction;
   }

   flow.style.display = direction == 0 ? 'none' : '';
   flow.style.animationDirection = direction < 0 ? 'reverse' : 'normal';
}

// edit mode: move the line, move / add / remove points with the mouse, a click opens the dialog

function schemaLineEditEvents(svg, schemaDef)
{
   let render = function(points) {
      schemaLineSetPoints(schemaDef, points);
      schemaLineRender(schemaDef);
   };

   // drag with the mouse: 'move(pos, delta)' while dragging, 'end(moved)' at the end

   let drag = function(event, move, end) {
      let start = schemaMousePos(event);
      let moved = false;

      event.preventDefault();
      event.stopPropagation();

      $(document).on('mousemove.schemaLineDrag', function(e) {
         let pos = schemaMousePos(e);

         if (!moved && Math.abs(pos.x - start.x) + Math.abs(pos.y - start.y) < 3)
            return;

         moved = true;
         move(pos, { x: pos.x - start.x, y: pos.y - start.y });
      });

      $(document).on('mouseup.schemaLineDrag', function() {
         $(document).off('.schemaLineDrag');
         end(moved);
      });
   };

   let dragPoint = function(event, points, idx, inserted) {
      drag(event, function(pos) {
         points[idx] = schemaLineSnapPoint(pos, points[idx-1], points[idx+1]);
         render(points);
      }, function(moved) {
         if (!moved)
            return;

         // dropped on a neighbour point -> they are merged

         for (const n of [points[idx-1], points[idx+1]]) {
            if (n && points.length > 2 && Math.abs(points[idx].x - n.x) + Math.abs(points[idx].y - n.y) <= schemaLineSnap) {
               points.splice(idx, 1);
               break;
            }
         }

         render(points);
         schemaDirty = true;
      });
   };

   $(svg).on('mousedown', '.schemaLineHit', function(event) {
      if (event.button != 0)
         return;

      let org = schemaLinePoints(schemaDef);

      drag(event, function(pos, delta) {
         let dx = Math.max(delta.x, -Math.min.apply(null, org.map(function(p) { return p.x; })));      // stay inside
         let dy = Math.max(delta.y, -Math.min.apply(null, org.map(function(p) { return p.y; })));

         render(org.map(function(p) { return { x: p.x + dx, y: p.y + dy }; }));
      }, function(moved) {
         if (moved)
            schemaDirty = true;
         else
            editSchemaLine(schemaDef);
      });
   });

   $(svg).on('mousedown', '.schemaLineHandle', function(event) {
      if (event.button == 0)
         dragPoint(event, schemaLinePoints(schemaDef), parseInt($(this).attr('data-idx')), false);
   });

   $(svg).on('dblclick', '.schemaLineHandle', function(event) {
      let points = schemaLinePoints(schemaDef);

      event.preventDefault();
      event.stopPropagation();

      if (points.length <= 2)
         return;

      points.splice(parseInt($(this).attr('data-idx')), 1);
      render(points);
      schemaDirty = true;
   });

   $(svg).on('mousedown', '.schemaLineMid', function(event) {
      if (event.button != 0)
         return;

      let points = schemaLinePoints(schemaDef);
      let idx = parseInt($(this).attr('data-seg')) + 1;

      points.splice(idx, 0, schemaMousePos(event));       // stored with the first move
      dragPoint(event, points, idx, true);
   });
}

// a hint in the menu line of the edit mode

function schemaEditHint(text)
{
   $('#schemaEditHint').remove();

   if (text)
      $('#schemaEditMenu').append($('<span></span>')
                                  .attr('id', 'schemaEditHint')
                                  .addClass('schemaEditHint')
                                  .text(text));
}

// end a still active placing by the mouse (sensor, template or line)

function schemaEndPlacing()
{
   let end = schemaPlacingEnd;

   schemaPlacingEnd = null;

   if (end)
      end();
}

// a new line: every click sets a point (start, corners, end), double click or Enter ends, Escape cancels

function schemaAddLine()
{
   schemaEndPlacing();

   let container = $('#schemaContainer');
   let points = [];

   let schemaDef = { type: schemaTypeCN(),
                     address: schemaFreeAddress(schemaTypeCN()),
                     state: "A",
                     kind: 3,
                     showtitle: 0,
                     showunit: 0,
                     fct: '',
                     properties: Object.assign({ 'points': '' }, schemaLineStyle) };

   let finish = function(cancel) {
      schemaPlacingEnd = null;
      container.off('.schemaPlace').removeClass('schemaPlacing');
      $(document).off('.schemaPlace');
      schemaEditHint('');

      if (cancel || points.length < 2) {
         $('#' + schemaElementId(schemaDef)).remove();
         lastSchemadata.splice(lastSchemadata.indexOf(schemaDef), 1);
         return;
      }

      schemaLineSetPoints(schemaDef, points);
      schemaLineRender(schemaDef);
      schemaDirty = true;
   };

   lastSchemadata.push(schemaDef);
   schemaPlacingEnd = function() { finish(false); };
   container.addClass('schemaPlacing');
   schemaEditHint('Leitung: Klick setzt Start und Eckpunkte, Doppelklick oder Enter beendet, Esc bricht ab');

   container.on('click.schemaPlace', function(event) {
      let last = points[points.length-1];
      let pos = schemaLineSnapPoint(schemaMousePos(event), last);

      if (last && Math.abs(pos.x - last.x) + Math.abs(pos.y - last.y) < 4)     // the second click of a double click
         return;

      points.push(pos);
      schemaLineRender(schemaDef, points);
   });

   container.on('mousemove.schemaPlace', function(event) {
      if (points.length)
         schemaLineRender(schemaDef, points.concat([schemaLineSnapPoint(schemaMousePos(event), points[points.length-1])]));
   });

   container.on('dblclick.schemaPlace', function() { finish(false); });

   $(document).on('keydown.schemaPlace', function(event) {
      if (event.keyCode == 13)
         finish(false);
      else if (event.keyCode == 27)
         finish(true);
   });
}

// an example for the condition of the flow of a line: a switched output of the schema (pump, ...) if there is one

function schemaLineFctExample()
{
   let example = null;

   for (let key in valueFacts) {
      let fact = valueFacts[key];

      if (!fact.state || (schemaSet == 'p4') != fact.type.startsWith('P4') || !fact.type.endsWith('DO'))
         continue;

      if (!example || /pumpe/i.test(fact.usrtitle || fact.title || ''))
         example = fact;

      if (/pumpe/i.test(fact.usrtitle || fact.title || ''))
         break;
   }

   if (!example)
      return "// Beispiel: Fluss nur wenn der Ausgang eingeschaltet ist\ngetItem('TYP:0xNN').state";

   return "// Beispiel: Fluss nur wenn '" + (example.usrtitle || example.title) + "' eingeschaltet ist\n" +
      "getItem('" + toKey(example.type, example.address) + "').state";
}

// the dialog of a line

function editSchemaLine(schemaDef)
{
   let id = schemaElementId(schemaDef);
   let props = schemaDef.properties;
   let fctEditor = null;                    // CodeMirror of the condition, without it the plain textarea
   let backup = JSON.stringify(schemaDef);  // to take back a preview, restored on close without 'Ok'
   let backupDirty = schemaDirty;
   let accepted = false;

   let select = function(selectId, options, value) {
      let sel = $('<select></select>')
          .addClass('rounded-border inputSetting')
          .attr('id', selectId);

      for (let k in options)
         sel.append($('<option></option>').val(k).html(options[k]));

      return sel.val(options[value] != null ? value : Object.keys(options)[0]);
   };

   let content = $('<div></div>').addClass('settingsDialogContent');
   let form = $('<div></div>')
       .attr('id', 'settingsForm')
       .append(content);

   content.append(schemaDlgRow('Einblenden', schemaDlgCheckbox('showIt', schemaDef.state == 'A')))
      .append(schemaDlgRow('Fluss', select('lineKind', { 3: 'Start -> Ende', 4: 'Ende -> Start', 5: 'kein Fluss' }, schemaDef.kind)))
      .append(schemaDlgRow2('Farbe', $('<input></input>').attr('id', 'lineColor').attr('type', 'text').val(props['color'] || schemaLineStyle['color']),
                            'Fluss Farbe', $('<input></input>').attr('id', 'lineFlowColor').attr('type', 'text').val(props['flowcolor'] || schemaLineStyle['flowcolor'])))
      .append(schemaDlgRow2('Breite', schemaDlgInput('lineWidth', 'number', parseInt(props['width']) || schemaLineStyle['width']).attr('min', 1).attr('step', 1),
                            'Tempo', select('lineSpeed', { 1: 'langsam', 2: 'mittel', 3: 'schnell' }, props['speed'])))
      .append(schemaDlgRow('Layer', schemaDlgInput('zIndex', 'number', props['z-index'] || schemaLineStyle['z-index']).attr('step', 1)))
      .append($('<div></div>')
              .addClass('textarea-row schemaFctRow')
              .append($('<span></span>')
                      .html('JavaScript (Bedingung für den Fluss, optional)'))
              .append($('<span></span>')
                      .append($('<div></div>')
                              .addClass('lua-cm-editor')
                              .append($('<textarea></textarea>')
                                      .addClass('rounded-border inputSetting inputSettingScript')
                                      .attr('id', 'function')
                                      .attr('placeholder', schemaLineFctExample())
                                      .val(schemaDef.fct || '')))))
      .append($('<div></div>')
              .addClass('schemaDlgHint')
              .html('Der Start der Leitung ist der grüne Punkt. Ohne JavaScript fließt es immer. Liefert das JavaScript false oder 0 ' +
                    'steht der Fluss, bei einem negativen Wert läuft er entgegen der gewählten Richtung, z.B. ' +
                    'getItem(\'TYP:0xNN\').state.'));

   let fctValue = function() { return fctEditor ? fctEditor.getValue() : $('#function').val(); };

   let check = function() {
      try {
         eval(fctValue());
         return true;
      }
      catch (err) {
         showInfoDialog({ 'message' : "Fehler im JavaScript: '" + err.message + ", " + fctValue() + "'", 'status': -1 });
         return false;
      }
   };

   let apply = function() {
      if (!check())
         return -1;

      props['color'] = $('#lineColor').spectrum("get").toRgbString();
      props['flowcolor'] = $('#lineFlowColor').spectrum("get").toRgbString();
      props['width'] = Math.max(1, parseInt($('#lineWidth').val()) || schemaLineStyle['width']);
      props['speed'] = parseInt($('#lineSpeed').val());
      props['z-index'] = $('#zIndex').val();
      schemaDef.kind = parseInt($('#lineKind').val());
      schemaDef.state = $('#showIt').is(":checked") ? "A": "D";
      schemaDef.fct = fctValue();

      schemaLineRender(schemaDef);
      schemaDirty = true;

      return 0;
   };

   // take back the changes of a preview

   let restore = function() {
      if (schemaDef.deleted || JSON.stringify(schemaDef) == backup)
         return;

      for (let prop in schemaDef)
         delete schemaDef[prop];

      Object.assign(schemaDef, JSON.parse(backup));
      props = schemaDef.properties;
      schemaDirty = backupDirty;
      schemaLineRender(schemaDef);
   };

   $(form).dialog({
      modal: true,
      title: schemaElementTitle(schemaDef),
      width: Math.min(700, $(window).width() - 20),
      hide: "fade",
      buttons: {
         'Check': function () {
            if (check())
               showInfoDialog({ 'message' : 'JavaScript ist gültig', 'status': 0 });
         },
         'Löschen': function () {
            schemaDef.deleted = true;
            $('#' + id).remove();
            schemaDirty = true;
            $(this).dialog('close');
         },
         'Abbrechen': function () {
            $(this).dialog('close');
         },
         'Vorschau': function () {
            apply();                        // shown in the schema, taken back if the dialog is closed without 'Ok'
         },
         'Ok': function () {
            if (apply() == 0) {
               accepted = true;

               for (let prop in schemaLineStyle)       // the style of the next new line
                  schemaLineStyle[prop] = props[prop];

               $(this).dialog('close');
            }
         }
      },
      open: function() {
         $(this).closest('.ui-dialog').find(".ui-dialog-buttonpane button:contains('Löschen')").attr('style', 'color:#ff5757');
         $('#lineColor').spectrum(schemaSpectrumOptions(false));
         $('#lineFlowColor').spectrum(schemaSpectrumOptions(false));
         schemaInitFctEditor(function(editor) { fctEditor = editor; });
      },
      close: function() {
         if (!accepted)
            restore();

         $(this).dialog('destroy').remove();
      }
   });
}

//***************************************************************************
// Common parts of the dialogs of the elements
//***************************************************************************

// a row of the dialog: label and input like the other settings dialogs

function schemaDlgRow(label, content, rowId)
{
   return $('<div></div>')
      .attr('id', rowId || null)
      .append($('<span></span>').html(label))
      .append($('<span></span>').append(content));
}

// two fields side by side

function schemaDlgRow2(label1, content1, label2, content2)
{
   return schemaDlgRow(label1, content1)
      .addClass('schemaRow2')
      .append($('<span></span>').html(label2))
      .append($('<span></span>').append(content2));
}

function schemaDlgInput(inputId, inputType, value)
{
   return $('<input></input>')
      .addClass('rounded-border inputSetting')
      .attr('id', inputId)
      .attr('type', inputType)
      .val(value);
}

function schemaDlgCheckbox(inputId, checked)
{
   return [ $('<input></input>')
            .addClass('rounded-border inputSetting')
            .attr('id', inputId)
            .attr('type', 'checkbox')
            .prop('checked', !!checked),
            $('<label></label>')
            .prop('for', inputId) ];
}

function schemaSpectrumOptions(allowEmpty)
{
   return {
      type : "color",
      showPalette: true,
      showSelectionPalette: true,
      palette: [],
      localStorageKey: "homectrl",
      togglePaletteOnly: false,
      appendTo: '#settingsForm',
      hideAfterPaletteSelect : true,
      showInput : true,
      showButtons : false,
      allowEmpty : allowEmpty
   };
}

// CodeMirror (iosetup.js) for the textarea '#function' of the dialog, without it the plain textarea stays

async function schemaInitFctEditor(setEditor)
{
   let area = document.getElementById('function');

   if (!area || typeof loadCodeMirror != 'function' || !await loadCodeMirror() || !document.body.contains(area))
      return;

   if (area.getAttribute('placeholder')) {
      try {
         await loadAssets(['lib/codemirror/placeholder.js']);    // the placeholder text of the empty field
      }
      catch (e) {
         console.log("Loading the CodeMirror placeholder addon failed: " + e.message);
      }

      if (!document.body.contains(area))
         return;
   }

   defineSchemaJsMode();

   let editor = CodeMirror.fromTextArea(area, {
      mode:           'jssimple',
      placeholder:    area.getAttribute('placeholder') || '',
      lineNumbers:    true,
      indentUnit:     3,
      tabSize:        3,
      indentWithTabs: false,
      lineWrapping:   true
   });

   // resizable like a textarea (css 'resize'), CodeMirror has to measure the new size

   new ResizeObserver(function() { editor.refresh(); }).observe(area.parentNode);
   setTimeout(function() { editor.refresh(); }, 50);
   setEditor(editor);
}

// JavaScript mode of the editor of the function (CodeMirror simple mode, lib/codemirror/simple.js)

function defineSchemaJsMode()
{
   if (CodeMirror.modes.jssimple)
      return;

   CodeMirror.defineSimpleMode('jssimple', {
      start: [
         {regex: /\/\/.*$/,                               token: 'comment'},
         {regex: /\/\*/,                                  token: 'comment', next: 'comment'},
         {regex: /"(?:[^\\"]|\\.)*"?/,                    token: 'string'},
         {regex: /'(?:[^\\']|\\.)*'?/,                    token: 'string'},
         {regex: /`(?:[^\\`]|\\.)*`?/,                    token: 'string'},
         {regex: /\b(?:let|const|var|function|return|if|else|for|while|do|switch|case|break|continue|default|new|typeof|in|of|try|catch|finally|throw|true|false|null|undefined)\b/, token: 'keyword'},
         {regex: /0x[0-9a-fA-F]+|\b\d+(?:\.\d+)?\b/,      token: 'number'},
         {regex: /[-+\/*=<>!&|?:%]+/,                     token: 'operator'},
         {regex: /[A-Za-z_$][\w$]*/,                      token: 'variable'}
      ],
      comment: [
         {regex: /.*?\*\//,                               token: 'comment', next: 'start'},
         {regex: /.*/,                                    token: 'comment'}
      ]
   });
}

function editSchemaValue(type, address, newUC)
{
   // console.log("editSchemaValue of id: " + id);

   let key = toKey(type, address);
   let id = type + ((address)>>>0).toString(10);

   let schemaDef = getSchemDef(id);
   let isUC = isSchemaUC(schemaDef.type);
   let item = allSensors[key];
   let fact = valueFacts[key];              // like 'item' available for the function ('Check' and 'Ok' evaluate it here)
   let fctEditor = null;                    // CodeMirror of the function, without it the plain textarea
   let backup = JSON.stringify(schemaDef);  // to take back a preview, restored on close without 'Ok'
   let backupDirty = schemaDirty;
   let accepted = false;

   let row = schemaDlgRow, row2 = schemaDlgRow2, input = schemaDlgInput, checkbox = schemaDlgCheckbox;

   let kinds = { 0: 'Wert', 1: 'Text', 2: 'Bild' };

   let selKind = $('<select></select>')
       .addClass('rounded-border inputSetting')
       .attr('id', 'schemaKind')
       .change(function() { selectKind(parseInt($(this).val())); });

   for (let k in kinds)
      selKind.append($('<option></option>').val(k).html(kinds[k]));

   selKind.val(kinds[schemaDef.kind] != null ? schemaDef.kind : Object.keys(kinds)[0]);

   let content = $('<div></div>').addClass('settingsDialogContent');
   let form = $('<div></div>')
       .attr('id', 'settingsForm')
       .append(content);

   content.append(row('Einblenden', checkbox('showIt', schemaDef.state == 'A')))
      .append(row('Typ', selKind));

   if (!isUC) {
      content.append(row('Bezeichnung', checkbox('showTitle', schemaDef.showtitle)))
         .append(row('Einheit', checkbox('showUnit', schemaDef.showunit)));
   }

   content.append(row2('Farbe', $('<input></input>').attr('id', 'colorFg').attr('type', 'text').val(schemaDef.properties["color"] || "white"),
                       'Hintergrund', $('<input></input>').attr('id', 'colorBg').attr('type', 'text').val(schemaDef.properties["background-color"] || "transparent")));

   content.append(row('Schriftgröße', input('fontSize', 'search', schemaDef.properties["font-size"] || "16px")));

   content.append(row2('Rahmen', checkbox('showBorder', schemaDef.properties['border'] != 'none'),
                       'Rahmen Farbe', $('<input></input>').attr('id', 'colorBorder').attr('type', 'text').val(schemaDef.properties["border-color"] || "")))
      .append(row2('Rahmen Breite', input('borderWidth', 'number', parseInt(schemaDef.properties["border-width"]) || 1).attr('min', 1).attr('step', 1),
                   'Radius', input('borderRadius', 'search', schemaDef.properties["border-radius"] || "3px")));

   content.append(row('Layer', input('zIndex', 'number', schemaDef.properties["z-index"] || "100").attr('step', 1)));

   {
      content.append(row('Bild Breite', input('imgWidth', 'number', schemaDef.width)).addClass('schemaImgRow'))
         .append(row('Bild Höhe', input('imgHeight', 'number', schemaDef.height)).addClass('schemaImgRow'))
         .append(row('', $('<span></span>')
                     .addClass('schemaDlgHint')
                     .html('Um das vom Sensor gelieferte Bild zu verwenden (' + ((item && item.image) || '-') + ') Image URL und JavaScript leer lassen!'))
                 .addClass('schemaImgRow'))
         .append(row('', input('usrText', 'search', schemaDef.usrtext || '').attr('maxlength', 100)))
         .append($('<div></div>')
                 .addClass('textarea-row schemaFctRow')
                 .append($('<span></span>')
                         .attr('id', 'labelFunction'))
                 .append($('<span></span>')
                         .append($('<div></div>')
                                 .addClass('lua-cm-editor')
                                 .append($('<textarea></textarea>')
                                         .addClass('rounded-border inputSetting inputSettingScript')
                                         .attr('id', 'function')
                                         .val(schemaDef.fct || '')))))
         .append($('<div></div>')
                 .addClass('schemaDlgHint')
                 .html('JavaScript zum Berechnen des angezeigten Wertes. Die Daten des Sensors liefern item.value und item.text, ' +
                       'Titel und Einheit fact.title und fact.unit. Andere Sensoren erhält man mit getItem(\'TYP:0xNN\')'));

      content.find('#usrText').closest('div').children('span').first().attr('id', 'labelUsrText');
   }

   let fctValue = function() { return fctEditor ? fctEditor.getValue() : $('#function').val(); };

   let spectrumOptions = schemaSpectrumOptions;

   let buttons = {
      'Check': function () {
         try {
            eval(fctValue());
            showInfoDialog({ 'message' : 'JavaScript ist gültig', 'status': 0 });
         }
         catch (err) {
            showInfoDialog({ 'message' : "Fehler im JavaScript: '" + err.message + ", " + fctValue() + "'", 'status': -1 });
         }
      },
      'Löschen': function () {              // a deleted sensor is offered again by 'Sensor hinzufügen'
         if (deleteUC(id) == 0)
            $(this).dialog('close');
      },
      'Abbrechen': function () {
         if (newUC)
            deleteUC(id);
         $(this).dialog('close');
      },
      'Vorschau': function () {
         apply("#"+id);                     // shown in the schema, taken back if the dialog is closed without 'Ok'
      },
      'Ok': function () {
         if (apply("#"+id) == 0) {
            accepted = true;
            $(this).dialog('close');
         }
      }
   };

   $(form).dialog({
      modal: true,
      title: schemaElementTitle(schemaDef),
      width: Math.min(700, $(window).width() - 20),
      hide: "fade",
      buttons: buttons,
      open: function() {
         $(this).closest('.ui-dialog').find(".ui-dialog-buttonpane button:contains('Löschen')").attr('style', 'color:#ff5757');
         $('#colorFg').spectrum(spectrumOptions(false));
         $('#colorBg').spectrum(spectrumOptions(false));
         $('#colorBorder').spectrum(spectrumOptions(true));       // empty -> the color of the theme

         selectKind(schemaDef.kind);
         schemaInitFctEditor(function(editor) { fctEditor = editor; });
      },
      close: function() {
         if (!accepted)
            restore();

         $(this).dialog('destroy').remove();
      }
   });

   // take back the changes of a preview

   function restore() {
      if (schemaDef.deleted || JSON.stringify(schemaDef) == backup)
         return;

      for (let prop in schemaDef.properties)
         $('#' + id).css(prop, '');

      for (let prop in schemaDef)
         delete schemaDef[prop];

      Object.assign(schemaDef, JSON.parse(backup));
      schemaDirty = backupDirty;

      setAttributeStyleByProperties('#' + id, schemaDef.properties);
      $('#' + id).css('position', 'absolute')
         .toggleClass('schemaDivHidden', schemaDef.state != 'A')
         .data('changedAt', '#changed');
      updateSchema();
   }

   function selectKind(kind) {
      let image = kind == 2;

      $('#labelUsrText').html(image ? "Image URL" : (isUC ? "Text" : "Zusatz Text"));
      $('#labelFunction').html(image ? "JavaScript (liefert die Image URL)" : "JavaScript");
      $('.schemaImgRow').css('display', image ? 'flex' : 'none');
   }

   function deleteUC(id) {
      for (let i = 0; i < lastSchemadata.length; i++) {
         if (lastSchemadata[i].type + ((lastSchemadata[i].address)>>>0).toString(10) == id) {
            lastSchemadata[i].deleted = true;
            $("#" + id).remove();
            schemaDirty = true;
            return 0;
         }
      }
      return -1;
   }

   function apply(id) {
      let fct = fctValue();

      try {
         eval(fct);
      }
      catch (err) {
         showInfoDialog({'message' : "Fehler im JavaScript: '" + err.message + ", " + fct + "'", 'status': -1});
         return -1;
      }

      let borderColor = $('#colorBorder').spectrum("get");

      schemaDef.properties["color"] = $('#colorFg').spectrum("get").toRgbString();
      schemaDef.properties["background-color"] = $('#colorBg').spectrum("get").toRgbString();
      schemaDef.properties["border"] = $('#showBorder').is(":checked") ? "" : "none";
      schemaDef.properties["border-color"] = borderColor ? borderColor.toRgbString() : "";
      schemaDef.properties["border-width"] = (parseInt($('#borderWidth').val()) || 1) + 'px';
      schemaDef.properties["border-radius"] = $('#borderRadius').val();
      schemaDef.properties["z-index"] = $('#zIndex').val();
      schemaDef.kind = parseInt($('#schemaKind').val());
      schemaDef.state = $('#showIt').is(":checked") ? "A": "D";

      schemaDef.properties["font-size"] = $('#fontSize').val();
      schemaDef.usrtext = $('#usrText').val();
      schemaDef.width = parseInt($('#imgWidth').val()) || 0;
      schemaDef.height = parseInt($('#imgHeight').val()) || 0;
      schemaDef.fct = fct;

      if (!isUC) {
         schemaDef.showtitle = $('#showTitle').is(":checked") ? 1 : 0;
         schemaDef.showunit = $('#showUnit').is(":checked") ? 1 : 0;
      }

      $(id).toggleClass('schemaDivHidden', schemaDef.state != 'A');
      schemaDirty = true;

      setAttributeStyleByProperties(id, schemaDef.properties);
      $(id).data('changedAt', '#changed');           // force the update, else it is skipped until the sensor changes
      updateSchema();

      return 0;
   }
}
