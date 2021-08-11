/*
 *  setup.js
 *
 *  (c) 2020 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

function initConfig(configuration)
{
   $('#container').removeClass('hidden');

   $("#container").height($(window).height() - $("#menu").height() - 8);

   window.onresize = function() {
      $("#container").height($(window).height() - $("#menu").height() - 8);
   };

   document.getElementById("container").innerHTML =
      '<div id="setupContainer" class="rounded-border inputTableConfig">';

   var root = document.getElementById("setupContainer");
   var lastCat = "";
   root.innerHTML = "";

   // console.log(JSON.stringify(configuration, undefined, 4));

   configuration.sort(function(a, b) {
      return a.category.localeCompare(b.category);
   });

   for (var i = 0; i < configuration.length; i++)
   {
      var item = configuration[i];
      var html = "";

      if (lastCat != item.category)
      {
         html += "<div class=\"rounded-border seperatorTitle1\">" + item.category + "</div>";
         var elem = document.createElement("div");
         elem.innerHTML = html;
         root.appendChild(elem);
         html = "";
         lastCat = item.category;
      }

      html += "    <span>" + item.title + ":</span>\n";

      if (item.descrtiption == "")
         item.descrtiption = "&nbsp;";  // on totally empty the line height not fit :(

      switch (item.type) {
      case 0:     // integer
         html += "    <span><input id=\"input_" + item.name + "\" class=\"rounded-border inputNum\" type=\"number\" value=\"" + item.value + "\"/></span>\n";
         html += "    <span class=\"inputComment\">" + item.descrtiption + "</span>\n";
         break;

      case 1:     // number (float)
         html += "    <span><input id=\"input_" + item.name + "\" class=\"rounded-border inputFloat\" type=\"number\" step=\"0.1\" value=\"" + item.value + "\"/></span>\n";
         html += "    <span class=\"inputComment\">" + item.descrtiption + "</span>\n";
         break;

      case 2:     // string

         html += "    <span><input id=\"input_" + item.name + "\" class=\"rounded-border input\" type=\"text\" value=\"" + item.value + "\"/></span>\n";
         html += "    <span class=\"inputComment\">" + item.descrtiption + "</span>\n";
         break;

      case 3:     // boolean
         html += "    <span><input id=\"checkbox_" + item.name + "\" class=\"rounded-border input\" style=\"width:auto;\" type=\"checkbox\" " + (item.value == 1 ? "checked" : "") + "/>" +
            '<label for="checkbox_' + item.name + '"></label></span></span>\n';
         html += "    <span class=\"inputComment\">" + item.descrtiption + "</span>\n";
         break;

      case 4:     // range
         var n = 0;

         if (item.value != "")
         {
            var ranges = item.value.split(",");

            for (n = 0; n < ranges.length; n++)
            {
               var range = ranges[n].split("-");
               var nameFrom = item.name + n + "From";
               var nameTo = item.name + n + "To";

               if (!range[1]) range[1] = "";
               if (n > 0) html += "  <span/>  </span>\n";
               html += "   <span>\n";
               html += "     <input id=\"range_" + nameFrom + "\" type=\"text\" class=\"rounded-border inputTime\" value=\"" + range[0] + "\"/> -";
               html += "     <input id=\"range_" + nameTo + "\" type=\"text\" class=\"rounded-border inputTime\" value=\"" + range[1] + "\"/>\n";
               html += "   </span>\n";
               html += "   <span></span>\n";
            }
         }

         var nameFrom = item.name + n + "From";
         var nameTo = item.name + n + "To";

         if (n > 0) html += "  <span/>  </span>\n";
         html += "  <span>\n";
         html += "    <input id=\"range_" + nameFrom + "\" value=\"\" type=\"text\" class=\"rounded-border inputTime\" /> -";
         html += "    <input id=\"range_" + nameTo + "\" value=\"\" type=\"text\" class=\"rounded-border inputTime\" />\n";
         html += "  </span>\n";
         html += "  <span class=\"inputComment\">" + item.descrtiption + "</span>\n";

         break;

      case 5:    // choice
         html += '<span>\n';
         html += '  <select id="input_' + item.name + '" class="rounded-border input" name="style">\n';

         for (var o = 0; o < item.options.length; o++) {
            var option = item.options[o];
            var sel = item.value == option ? 'SELECTED' : '';
            html += '    <option value="' + option + '" ' + sel + '>' + option + '</option>\n';
         }

         html += '  </select>\n';
         html += '</span>\n';
         break;
      }

      var elem = document.createElement("div");
      elem.innerHTML = html;
      root.appendChild(elem);
   }
}

function storeConfig()
{
   var jsonObj = {};
   var rootConfig = document.getElementById("container");

   console.log("storeSettings");

   var elements = rootConfig.querySelectorAll("[id^='input_']");

   for (var i = 0; i < elements.length; i++) {
      var name = elements[i].id.substring(elements[i].id.indexOf("_") + 1);
      jsonObj[name] = elements[i].value;
   }

   var elements = rootConfig.querySelectorAll("[id^='checkbox_']");

   for (var i = 0; i < elements.length; i++) {
      var name = elements[i].id.substring(elements[i].id.indexOf("_") + 1);
      jsonObj[name] = (elements[i].checked ? "1" : "0");
   }

   var elements = rootConfig.querySelectorAll("[id^='range_']");

   for (var i = 0; i < elements.length; i++) {
      var name = elements[i].id.substring(elements[i].id.indexOf("_") + 1);
      if (name.match("0From$")) {
         name = name.substring(0, name.indexOf("0From"));
         jsonObj[name] = toTimeRangesString("range_" + name);
         console.log("value: " + jsonObj[name]);
      }
   }

   // console.log(JSON.stringify(jsonObj, undefined, 4));

   socket.send({ "event" : "storeconfig", "object" : jsonObj });
}

function toTimeRangesString(base)
{
   var times = "";

   for (var i = 0; i < 10; i++) {
      var id = "#" + base + i;

      if ($(id + "From") && $(id + "From").val() && $(id + "To") && $(id + "To").val()) {
         if (times != "") times += ",";
         times += $(id + "From").val() + "-" + $(id + "To").val();
      }
      else {
         break;
      }
   }

   return times;
}

window.resetPeaks = function()
{
   socket.send({ "event" : "resetpeaks", "object" : { "what" : "all" } });
}

var filterActive = false;

function filterIoSetup()
{
   filterActive = !filterActive;
   console.log("filterIoSetup: " + filterActive);

   $("#filterIoSetup").html(filterActive ? "[aktive]" : "[alle]");
   initIoSetup(valueFacts);
   // socket.send({ "event" : "iosetup", "object" : { "filter" : filterActive } });
}

function tableHeadline(title, id)
{
   return '  <div class="rounded-border seperatorTitle1">' + title + '</div>' +
      '  <table class="tableMultiCol">' +
      '    <thead>' +
      '      <tr>' +
      '        <td style="width:20%;">Name</td>' +
      '        <td style="width:25%;">Bezeichnung</td>' +
      '        <td style="width:3%;">Aktiv</td>' +
      '        <td style="width:8%;">ID</td>' +
      '        <td style="width:8%;">Widget' +
      '      </tr>' +
      '    </thead>' +
      '    <tbody id="' + id + '">' +
      '    </tbody>' +
      '  </table>';
}

function initIoSetup(valueFacts)
{
   $('#container').removeClass('hidden');

   document.getElementById("container").innerHTML =
      '<div id="ioSetupContainer">' +
      tableHeadline('Digitale Ausgänge', 'ioDigitalOut') +
      tableHeadline('One Wire Sensoren', 'ioOneWire') +
      tableHeadline('Skripte', 'ioScripts') +
      tableHeadline('Analog Eingänge (arduino)', 'ioAnalog') +
      tableHeadline('Weitere Sensoren', 'ioOther') +
      '</div>';

   var root = document.getElementById("ioSetupContainer");

   // console.log(JSON.stringify(valueFacts, undefined, 4));

   document.getElementById("ioDigitalOut").innerHTML = "";
   document.getElementById("ioOneWire").innerHTML = "";
   document.getElementById("ioOther").innerHTML = "";
   document.getElementById("ioAnalog").innerHTML = "";
   document.getElementById("ioScripts").innerHTML = "";

   for (var key in valueFacts) {
      var item = valueFacts[key];

      if (!item.state && filterActive)
         continue;

      var root = null;
      var usrtitle = item.usrtitle != null ? item.usrtitle : "";

      var html = "<td id=\"row_" + item.type + item.address + "\" data-address=\"" + item.address + "\" data-type=\"" + item.type + "\" >" + item.title + "</td>";
      html += "<td class=\"tableMultiColCell\">";
      html += "  <input id=\"usrtitle_" + item.type + item.address + "\" class=\"rounded-border inputSetting\" type=\"text\" value=\"" + usrtitle + "\"/>";
      html += "</td>";

      html += "<td><input id=\"state_" + item.type + item.address + "\" class=\"rounded-border inputSetting\" type=\"checkbox\" " + (item.state ? "checked" : "") + ' /><label for="state_' + item.type + item.address + '"></label></td>';
      html += "<td>" + item.type + ":0x" + item.address.toString(16).padStart(2, '0') + "</td>";
      html += '<td><button class="buttonOptions rounded-border" onclick="widgetSetup(\'' + key + '\')">Edit</button></td>';

      switch (item.type) {
         case 'DO': root = document.getElementById("ioDigitalOut"); break
         case 'W1': root = document.getElementById("ioOneWire");    break
         case 'SP': root = document.getElementById("ioOther");      break
         case 'AI': root = document.getElementById("ioAnalog");     break
         case 'SC': root = document.getElementById("ioScripts");    break
      }

      if (root != null)
      {
         var elem = document.createElement("tr");
         elem.innerHTML = html;
         root.appendChild(elem);
      }
   }
}

function widgetSetup(key)
{
   var item = valueFacts[key];
   var form = document.createElement("div");

   $(form).append($('<div></div>')
                  .attr('id', 'widgetConfig')
                  .css('minWidth', '60vh')
                  .css('maxWidth', '80vh')
                  .css('maxHeight', '60vh')
                  .css('display', 'flex')
                  .append($('<div></div>')
                          .attr('id', 'preview')
                          .addClass('widgetContainer')
                          .css('width', '50%'))

                  .append($('<div></div>')
                          .css('width', '50%')
                          .append($('<div></div>')
                                  .css('display', 'flex')
                                  .append($('<span></span>')
                                          .css('width', '25%')
                                          .css('text-align', 'end')
                                          .css('align-self', 'center')
                                          .css('margin-right', '10px')
                                          .html('Widget'))
                                  .append($('<span></span>')
                                          .append($('<select></select>')
                                                  .addClass('rounded-border inputSetting')
                                                  .attr('id', 'widgettype')
                                                 )))

                          .append($('<div></div>')
                                  .css('display', 'flex')
                                  .append($('<span></span>')
                                          .css('width', '25%')
                                          .css('text-align', 'end')
                                          .css('align-self', 'center')
                                          .css('margin-right', '10px')
                                          .html('Einheit'))
                                  .append($('<span></span>')
                                          .append($('<input></input>')
                                                  .addClass('rounded-border inputSetting')
                                                  .attr('id', 'unit')
                                                  .val(item.unit)
                                                 )))

                          .append($('<div></div>')
                                  .css('display', 'flex')
                                  .append($('<span></span>')
                                          .css('width', '25%')
                                          .css('text-align', 'end')
                                          .css('align-self', 'center')
                                          .css('margin-right', '10px')
                                          .html('Skala Min'))
                                  .append($('<span></span>')
                                          .append($('<input></input>')
                                                  .addClass('rounded-border inputSetting')
                                                  .attr('id', 'scalemin')
                                                  .attr('type', 'number')
                                                  .val(item.scalemin)
                                                 )))

                          .append($('<div></div>')
                                  .css('display', 'flex')
                                  .append($('<span></span>')
                                          .css('width', '25%')
                                          .css('text-align', 'end')
                                          .css('align-self', 'center')
                                          .css('margin-right', '10px')
                                          .html('Skala Max'))
                                  .append($('<span></span>')
                                          .append($('<input></input>')
                                                  .addClass('rounded-border inputSetting')
                                                  .attr('id', 'scalemax')
                                                  .attr('type', 'number')
                                                  .val(item.scalemax)
                                                 )))

                          .append($('<div></div>')
                                  .css('display', 'flex')
                                  .append($('<span></span>')
                                          .css('width', '25%')
                                          .css('text-align', 'end')
                                          .css('align-self', 'center')
                                          .css('margin-right', '10px')
                                          .html('Skala Step'))
                                  .append($('<span></span>')
                                          .append($('<input></input>')
                                                  .addClass('rounded-border inputSetting')
                                                  .attr('id', 'scalestep')
                                                  .attr('type', 'number')
                                                  .attr('step', '0.1')
                                                  .val(item.scalestep)
                                                 )))

                          .append($('<div></div>')
                                  .css('display', 'flex')
                                  .append($('<span></span>')
                                          .css('width', '25%')
                                          .css('text-align', 'end')
                                          .css('align-self', 'center')
                                          .css('margin-right', '10px')
                                          .html('Skala Crit'))
                                  .append($('<span></span>')
                                          .append($('<input></input>')
                                                  .addClass('rounded-border inputSetting')
                                                  .attr('id', 'critmin')
                                                  .attr('type', 'number')
                                                  .val(item.critmin)
                                                 )))

                          .append($('<div></div>')
                                  .css('display', 'flex')
                                  .append($('<span></span>')
                                          .css('width', '25%')
                                          .css('text-align', 'end')
                                          .css('align-self', 'center')
                                          .css('margin-right', '10px')
                                          .html('Image On'))
                                  .append($('<span></span>')
                                          .append($('<select></select>')
                                                  .addClass('rounded-border inputSetting')
                                                  .attr('id', 'imgon')
                                                 )))

                          .append($('<div></div>')
                                  .css('display', 'flex')
                                  .append($('<span></span>')
                                          .css('width', '25%')
                                          .css('text-align', 'end')
                                          .css('align-self', 'center')
                                          .css('margin-right', '10px')
                                          .html('Image On'))
                                  .append($('<span></span>')
                                          .append($('<select></select>')
                                                  .addClass('rounded-border inputSetting')
                                                  .attr('id', 'imgoff')
                                                 )))
                         ));

   var widget = null;

   for (var i = 0; i < allWidgets.length; i++)
   {
      console.log("check " + allWidgets[i].type + " - " + allWidgets[i].address);
      if (allWidgets[i].type == item.type && allWidgets[i].address == item.address) {
         widget = allWidgets[i];
         break;
      }
   }

   $(form).dialog({
      modal: true,
      width: "auto",
      title: "Widget Konfiguration - " + (item.usrtitle ? item.usrtitle : item.title),
      open: function() {
         for (var wdKey in widgetTypes) {
            $('#widgettype').append($('<option></option>')
                                    .val(widgetTypes[wdKey])
                                    .html(wdKey)
                                    .attr('selected', widgetTypes[wdKey] == item.widgettype));
         }

         for (var img in images) {
            $('#imgoff').append($('<option></option>')
                                .val(images[img])
                                .html(images[img])
                                .attr('selected', item.imgoff == images[img]));
            $('#imgon').append($('<option></option>')
                               .val(images[img])
                               .html(images[img])
                               .attr('selected', item.imgon == images[img]));
         }

         if (widget != null) {
            initWidget(widget, document.getElementById('preview'));
            updateWidget(widget, false);
         }
         else
            console.log("No widget for " + item.type + " - " + item.address + " found");
      },
      buttons: {
         'Cancel': function () {
            $(this).dialog('close');
         },
         'Update': function () {
            if (widget != null) {
               var e = document.getElementById('preview');
               e.innerHTML = "";
               fact = Object.create(valueFacts[item.type + ":" + item.address]);

               if ($("#unit").length)
                  fact.unit = $("#unit").val();
               if ($("#scalemax").length)
                  fact.scalemax = parseInt($("#scalemax").val());
               if ($("#scalemin").length)
                  fact.scalemin = parseInt($("#scalemin").val());
               if ($("#scalestep").length)
                  fact.scalestep = parseFloat($("#scalestep").val());
               if ($("#critmin").length)
                  fact.critmin = parseInt($("#critmin").val());
               if ($("#imgon").length)
                  fact.imgon = $("#imgon").val();
               if ($("#imgoff").length)
                  fact.imgoff = $("#imgoff").val();
               fact.widgettype = parseInt($("#widgettype").val());

               initWidget(widget, e, fact);
               updateWidget(widget, true, fact);
            }
         },
         'Ok': function () {
            var jsonObj = {};
            jsonObj["type"] = item.type;
            jsonObj["address"] = item.address;
            if ($("#unit").length)
               jsonObj["unit"] = parseInt($("#unit").val());
            if ($("#scalemax").length)
               jsonObj["scalemax"] = parseInt($("#scalemax").val());
            if ($("#scalemin").length)
               jsonObj["scalemin"] = parseInt($("#scalemin").val());
            if ($("#scalestep").length)
               jsonObj["scalestep"] = parseFloat($("#scalestep").val());
            if ($("#critmin").length)
               jsonObj["critmin"] = parseInt($("#critmin").val());
            if ($("#imgon").length)
               jsonObj["imgon"] = $("#imgon").val();
            if ($("#imgoff").length)
               jsonObj["imgoff"] = $("#imgoff").val();
            jsonObj["widgettype"] = parseInt($("#widgettype").val());

            var jsonArray = [];
            jsonArray[0] = jsonObj;
            socket.send({ "event" : "storeiosetup", "object" : jsonArray });

            $(this).dialog('close');
         }
      },
      close: function() { $(this).dialog('destroy').remove(); }
   });
}

function storeIoSetup()
{
   var jsonArray = [];
   var rootSetup = document.getElementById("ioSetupContainer");
   var elements = rootSetup.querySelectorAll("[id^='row_']");

   console.log("storeIoSetup");

   for (var i = 0; i < elements.length; i++) {
      var jsonObj = {};
      var type = $(elements[i]).data("type");
      var address = $(elements[i]).data("address");
      jsonObj["type"] = type;
      jsonObj["address"] = address;
      jsonObj["usrtitle"] = $("#usrtitle_" + type + address).val();
      jsonObj["state"] = $("#state_" + type + address).is(":checked");
      jsonArray[i] = jsonObj;
   }

   socket.send({ "event" : "storeiosetup", "object" : jsonArray });
}
