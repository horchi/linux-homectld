/*
 *  list.js
 *
 *  (c) 2020-2024 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

function initList()
{
   if (allSensors == null)
   {
      console.log("Fatal: Missing widgets!");
      return;
   }

   $('#container').removeClass('hidden');
   $('#container').empty();

   $('#container')
      .append($('<div></div>')
              .attr('id', 'stateContainer')
              .addClass('stateInfo'))
      .append($('<div></div>')
              .attr('id', 'listContainer')
              .addClass('rounded-border listContainer'));

   // state

   document.getElementById("stateContainer").innerHTML =
      '<div id="stateContainerDaemon" class="rounded-border daemonState"></div>';

   // daemon state

   var rootState = document.getElementById("stateContainerDaemon");

   if (daemonState.state != null && daemonState.state == 0)
   {
      html =  '<div id="aStateOk"><span style="text-align: center;">' + config.instanceName + ' ONLINE</span></div>';
      html +=  '<br/>\n';
      html +=  '<div style="display:flex;"><span style="width:30%;display:inline-block;">Läuft seit:</span><span display="inline-block">' + daemonState.runningsince + '</span></div>\n';
      html +=  '<div style="display:flex;"><span style="width:30%;display:inline-block;">Version:</span> <span display="inline-block">' + daemonState.version + '</span></div>\n';
      html +=  '<div style="display:flex;"><span style="width:30%;display:inline-block;">CPU-Last:</span><span display="inline-block">' + daemonState.average0 + " " + daemonState.average1 + ' '  + daemonState.average2 + ' ' + '</span></div>\n';
   }
   else
   {
      html = '<div id="aStateFail">ACHTUNG:<br/>' + config.instanceName + ' OFFLINE</div>\n';
   }

   rootState.innerHTML = html;

   // build page content

   for (var key in allSensors) {
      var sensor = allSensors[key];
      var elemId = key.replace(':', '_'); // don't know why but : not working for image :o
      var fact = valueFacts[key];

      if (fact == null) {
         console.log("Fact for sensor '" + key + "' not found, ignoring");
         continue;
      }

      var title = fact.usrtitle != '' && fact.usrtitle != null ? fact.usrtitle : fact.title;
      var html = "";

      if (fact.widget.widgettype == 0) {                                             // Symbol
         if (localStorage.getItem(storagePrefix + 'Rights') & fact.rights)
            html += '   <div class="listFirstCol" onclick="toggleIo(' + fact.address + ",'" + fact.type + '\')"><img id="widget' + elemId + '"/></div>\n';
         else
            html += '   <div class="listFirstCol"><img id="widget' + elemId + '"/></div>\n';
      }
      else if (fact.widget.widgettype == 2 || fact.widget.widgettype == 8) {         // Text, Choice
         html += '<div class="listFirstCol" id="widget' + elemId + '"></div>';
      }
      else {
         html += '<span class="listFirstCol" id=widget' + elemId + '">' + (sensor.value ? sensor.value.toFixed(2) : '-') + '&nbsp;' + fact.widget.unit;
         html += '&nbsp; <p style="display:inline;font-size:12px;font-style:italic;">(' + (sensor.peak != null ? sensor.peak.toFixed(2) : '  ') + ')</p>';
         html += '</span>';
      }

      html += '<span class="listSecondCol listText" >' + title + '</span>';

      $('#listContainer').append($('<div></div>')
                                 .addClass('listRow')
                                 .html(html));
   }

   updateList();

   // calc container size

   $("#container").height($(window).height() - getTotalHeightOf('menu'));
   window.onresize = function() {
      $("#container").height($(window).height() - getTotalHeightOf('menu'));
   };
}

function updateList()
{
   for (var key in allSensors) {
      var sensor = allSensors[key];
      var fact = valueFacts[key];

      if (!fact) {
         console.log("Fact for widget '" + key + "' not found, ignoring");
         continue;
      }

      var elemId = "#widget" + key.replace(':', '_');

      if (fact.widget.widgettype == 1 || fact.widget.widgettype == 3 || fact.widget.widgettype == 5) {
         var peak = sensor.peak != null ? sensor.peak.toFixed(2) : "  ";
         $(elemId).html(sensor.value.toFixed(2) + "&nbsp;" + fact.widget.unit +
                        "&nbsp; <p style=\"display:inline;font-size:12px;font-style:italic;\">(" + peak + ")</p>");
      }
      else if (fact.widget.widgettype == 0) {   // Symbol
         var image = sensor.value != 0 ? fact.widget.imgon : fact.widget.imgoff;
         $(elemId).attr("src", image);
         // $("#container").height($(window).height() - getTotalHeightOf('menu'));
      }
      else if (fact.widget.widgettype == 2 || fact.widget.widgettype == 7 || fact.widget.widgettype == 12) {   // Text, PlainText
         $(elemId).html(sensor.text);
      }
      else {
         if (!sensor.value)
            console.log("Missing value for " + key);
         else
            $(elemId).html(sensor.value.toFixed(0));
      }
   }
}
