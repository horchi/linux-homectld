/*
 *  list.js
 *
 *  (c) 2020 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

function initList(update = false)
{
   if (!allWidgets)
   {
      console.log("Fatal: Missing payload!");
      return;
   }

   if (!update) {
      $('#stateContainer').removeClass('hidden');
      $('#container').removeClass('hidden');

      // ...
   }

   document.getElementById("container").innerHTML = '<div id="listContainer" class="rounded-border listContainer"</div>>';
   var root = document.getElementById("listContainer");

   // deamon state

   var rootState = document.getElementById("stateContainer");
   var html = "";

   if (daemonState.state != null && daemonState.state == 0)
   {
      html +=  "<div id=\"aStateOk\"><span style=\"text-align: center;\">Pool Control ONLINE</span></div><br/>\n";
      html +=  "<div><span>Läuft seit:</span><span>" + daemonState.runningsince + "</span>       </div>\n";
      html +=  "<div><span>Version:</span> <span>" + daemonState.version + "</span></div>\n";
      html +=  "<div><span>CPU-Last:</span><span>" + daemonState.average0 + " " + daemonState.average1 + " "  + daemonState.average2 + " " + "</span>           </div>\n";
   }
   else
   {
      html += "<div id=\"aStateFail\">ACHTUNG:<br/>Pool Control OFFLINE</div>\n";
   }

   rootState.innerHTML = "";
   var elem = document.createElement("div");
   elem.className = "rounded-border daemonState";
   elem.innerHTML = html;
   rootState.appendChild(elem);

   // clean page content

   root.innerHTML = "";

   var elem = document.createElement("div");
   elem.className = "chartTitle rounded-border";
   elem.innerHTML = "<center id=\"refreshTime\" \>";
   root.appendChild(elem);

   // build page content

   for (var i = 0; i < allWidgets.length; i++)
   {
      var html = "";
      var widget = allWidgets[i];
      var id = "id=\"widget" + widget.type + widget.address + "\"";
      var fact = valueFacts[widget.type + ":" + widget.address];

      if (fact == null || fact == undefined) {
         console.log("Fact for widget '" + widget.type + ":" + widget.address + "' not found, ignoring");
         continue;
      }

      var title = fact.usrtitle != '' && fact.usrtitle != undefined ? fact.usrtitle : fact.title;

      if (fact.widgettype == 1 || fact.widgettype == 3) {      // 1 Gauge or 3 Value
         html += "<span class=\"listFirstCol\"" + id + ">" + widget.value.toFixed(2) + "&nbsp;" + fact.unit;
         html += "&nbsp; <p style=\"display:inline;font-size:12px;font-style:italic;\">(" + (widget.peak != null ? widget.peak.toFixed(2) : "  ") + ")</p>";
         html += "</span>";
      }
      else if (fact.widgettype == 0) {   // 0 Symbol
         html += "   <div class=\"listFirstCol\" onclick=\"toggleIo(" + fact.address + ",'" + fact.type + "')\"><img " + id + "/></div>\n";
      }
      else {   // 2 Text
         html += "<div class=\"listFirstCol\"" + id + "></div>";
      }

      html += "<span class=\"listSecondCol listText\" >" + title + "</span>";

      var elem = document.createElement("div");
      elem.className = "listRow";
      elem.innerHTML = html;
      root.appendChild(elem);
   }

   updateList(allWidgets);
}

function updateList(widgets)
{
   var d = new Date();     // #TODO use SP:0x4 instead of Date()
   document.getElementById("refreshTime").innerHTML = "Messwerte von " + d.toLocaleTimeString();

   for (var i = 0; i < widgets.length; i++)
   {
      var widget = widgets[i];
      var fact = valueFacts[widget.type + ":" + widget.address];

      if (fact == null || fact == undefined) {
         console.log("Fact for widget '" + widget.type + ":" + widget.address + "' not found, ignoring");
         continue;
      }

      var id = "#widget" + fact.type + fact.address;

      if (fact.widgettype == 1 || fact.widgettype == 3) {
         var peak = widget.peak != null ? widget.peak.toFixed(2) : "  ";
         $(id).html(widget.value.toFixed(2) + "&nbsp;" + fact.unit +
                    "&nbsp; <p style=\"display:inline;font-size:12px;font-style:italic;\">(" + peak + ")</p>");
      }
      else if (fact.widgettype == 0) {   // Symbol
         var image = widget.value != 0 ? fact.imgon : fact.imgoff;
         $(id).attr("src", image);
      }
      else if (fact.widgettype == 2 || fact.widgettype == 7) {   // Text, PlainText
         $(id).html(widget.text);
      }
      else {
         if (widget.value == undefined)
            console.log("Missing value for " + widget.type + ":" + widget.address);
         else
            $(id).html(widget.value.toFixed(0));
      }

      // console.log(i + ") " + fact.widgettype + " : " + title + " / " + widget.value + "(" + id + ")");
   }
}
