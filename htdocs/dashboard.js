/*
 *  dashboard.js
 *
 *  (c) 2020 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

var gauge = null;

function initDashboard(update = false)
{
   // console.log("initDashboard" + JSON.stringify(allWidgets, undefined, 4));

   if (!allWidgets) {
      console.log("Fatal: Missing payload!");
      return;
   }

   if (!update) {
      $('#container').removeClass('hidden');
   }

   $("#container").height($(window).height() - $("#menu").height() - 8);

   window.onresize = function() {
      $("#container").height($(window).height() - $("#menu").height() - 8);
   };

   // clean page content

   document.getElementById("container").innerHTML = '<div id="widgetContainer" class="widgetContainer"></div>';

   var root = document.getElementById("widgetContainer");

   // build page content

   for (var i = 0; i < allWidgets.length; i++)
   {
      initWidget(allWidgets[i], root, null);
   }

   updateDashboard(allWidgets, true);
}

function initWidget(widget, root, fact)
{
   if (fact == null)
      fact = valueFacts[widget.type + ":" + widget.address];

   console.log("initWidget " + widget.name);

   if (fact == null || fact == undefined) {
      console.log("Fact for widget '" + widget.type + ":" + widget.address + "' not found, ignoring");
      return;
   }

   // console.log("fact: " + JSON.stringify(fact, undefined, 4));

   var title = fact.usrtitle != '' && fact.usrtitle != undefined ? fact.usrtitle : fact.title;
   var elem = document.createElement("div");
   root.appendChild(elem);
   elem.setAttribute('id', 'div_' + fact.type + ":0x" + fact.address.toString(16).padStart(2, '0'));
   elem.setAttribute('draggable', true);
   elem.dataset.droppoint = true;
   elem.addEventListener('dragstart', function(event) {dragWidget(event)}, false);
   elem.addEventListener('dragover', function(event) {event.preventDefault()}, false);
   elem.addEventListener('drop', function(event) {dropWidget(event)}, false);

   switch (fact.widgettype) {
      case 0:           // Symbol
         var html = "";
         html += "  <button class=\"widget-title\" type=\"button\" onclick=\"toggleMode(" + fact.address + ", '" + fact.type + "')\">" + title + "</button>\n";
         html += "  <button class=\"widget-main\" type=\"button\" onclick=\"toggleIo(" + fact.address + ",'" + fact.type + "')\" >\n";
         html += '    <img id="widget' + fact.type + fact.address + '" draggable="false")/>';
         html += "   </button>\n";
         html += "<div id=\"progress" + fact.type + fact.address + "\" class=\"widget-progress\">";
         html += "   <div id=\"progressBar" + fact.type + fact.address + "\" class=\"progress-bar\" style=\"visible\"></div>";
         html += "</div>";

         elem.className = "widget rounded-border";
         elem.innerHTML = html;
         document.getElementById("progress" + fact.type + fact.address).style.visibility = "hidden";
         break;

      case 1:          // Chart
         var html = "  <div class=\"widget-title\">" + title + "</div>";

         html += "<div id=\"peak" + fact.type + fact.address + "\" class=\"chart-peak\"></div>";
         html += "<div id=\"value" + fact.type + fact.address + "\" class=\"chart-value\"></div>";
         html += "<div class=\"chart-canvas-container\"><canvas id=\"widget" + fact.type + fact.address + "\" class=\"chart-canvas\"></canvas></div>";

         elem.className = "widgetChart rounded-border";
         elem.setAttribute("onclick", "toggleChartDialog('" + fact.type + "'," + fact.address + ")");
         elem.innerHTML = html;
         break;

      case 4:           // Gauge
         var html = "  <div class=\"widget-title\">" + title + "</div>";

         html += "  <svg class=\"widget-main-gauge\" viewBox=\"0 0 1000 600\" preserveAspectRatio=\"xMidYMin slice\">\n";
         html += "    <path id=\"pb" + fact.type + fact.address + "\"/>\n";
         html += "    <path class=\"data-arc\" id=\"pv" + fact.type + fact.address + "\"/>\n";
         html += "    <path class=\"data-peak\" id=\"pp" + fact.type + fact.address + "\"/>\n";
         html += "    <text id=\"value" + fact.type + fact.address + "\" class=\"gauge-value\" text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"500\" y=\"450\" font-size=\"140\" font-weight=\"bold\"></text>\n";
         html += "    <text id=\"sMin" + fact.type + fact.address + "\" class=\"scale-text\" text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"50\" y=\"550\"></text>\n";
         html += "    <text id=\"sMax" + fact.type + fact.address + "\" class=\"scale-text\" text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"950\" y=\"550\"></text>\n";
         html += "  </svg>\n";

         elem.className = "widgetGauge rounded-border participation";
         elem.setAttribute("id", "widget" + fact.type + fact.address);
         elem.setAttribute("onclick", "toggleChartDialog('" + fact.type + "'," + fact.address + ")");
         elem.innerHTML = html;
         break;

      case 5:          // Meter
      case 6:          // MeterLevel
         var radial = fact.widgettype == 5;
         elem.className = radial ? "widgetMeter rounded-border" : "widgetMeterLinear rounded-border";
         var eTitle = document.createElement("div");
         eTitle.className = "widget-title";
         eTitle.innerHTML = title;
         elem.appendChild(eTitle);

         var main = document.createElement("div");
         main.className = radial ? "widget-main-meter" : "widget-main-meter-lin";
         elem.appendChild(main);
         var canvas = document.createElement('canvas');
         main.appendChild(canvas);
         canvas.setAttribute('id', 'widget' + fact.type + fact.address);
         canvas.setAttribute("onclick", "toggleChartDialog('" + fact.type + "'," + fact.address + ")");

         if (!radial) {
            var value = document.createElement("div");
            value.setAttribute('id', 'widgetValue' + fact.type + fact.address);
            value.className = "widget-main-value-lin";
            elem.appendChild(value);
         }

         if (fact.scalemin == undefined)
            fact.scalemin = 0;

         var ticks = [];
         var scaleRange = fact.scalemax - fact.scalemin;
         var stepWidth = fact.scalestep != undefined ? fact.scalestep : 0;

         if (!stepWidth) {
            var steps = 10;
            if (scaleRange <= 100)
               steps = scaleRange % 10 == 0 ? 10 : scaleRange % 5 == 0 ? 5 : scaleRange;
            if (steps < 10)
               steps = 10;
            if (!radial && fact.unit == '%')
               steps = 4;
            stepWidth = scaleRange / steps;
         }

         if (stepWidth <= 0) stepWidth = 1;

      for (var step = fact.scalemin; step.toFixed(2) <= fact.scalemax; step += stepWidth) {
            ticks.push(step % 1 ? parseFloat(step).toFixed(1) : parseInt(step));
            console.log("added step " + ((step % 1) ? parseFloat(step).toFixed(1) : parseInt(step)) +
                        " max scale is " + fact.scalemax + " stepWidth is " + stepWidth);
            console.log(((step+stepWidth <= fact.scalemax) ? "cont" : "break")  + " due to next step is " + (step+stepWidth));
         }

         var highlights = {};

         if (fact.critmin == -1) {
            highlights = [
               { from: fact.scalemin, to: 0,             color: 'rgba(0,0,255,.6)' },
               { from: 0,             to: fact.scalemax, color: 'rgba(0,255,0,.6)' }
            ];
         }
         else {
            highlights = [
               { from: fact.scalemin, to: fact.critmin,  color: 'rgba(255,0,0,.6)' },
               { from: fact.critmin,  to: fact.scalemax, color: 'rgba(0,255,0,.6)' }
            ];
         }

         // console.log("widget: " + JSON.stringify(widget, undefined, 4));
         // console.log("ticks: " + ticks + " range; " + scaleRange);
         // console.log("highlights: " + JSON.stringify(highlights, undefined, 4));

         options = {
            renderTo: 'widget' + fact.type + fact.address,
            units: radial ? fact.unit : '',
            // title: radial ? false : fact.unit
            colorTitle: 'white',
            minValue: fact.scalemin,
            maxValue: fact.scalemax,
            majorTicks: ticks,
            minorTicks: 5,
            strokeTicks: false,
            highlights: highlights,
            highlightsWidth: radial ? 8 : 6,
            colorPlate: radial ? '#2177AD' : 'rgba(0,0,0,0)',
            colorBar: 'gray',
            colorBarProgress: fact.unit == '%' ? 'blue' : 'red',
            colorBarStroke: 'red',
            colorMajorTicks: '#f5f5f5',
            colorMinorTicks: '#ddd',
            colorTitle: '#fff',
            colorUnits: '#ccc',
            colorNumbers: '#eee',
            colorNeedle: 'rgba(240, 128, 128, 1)',
            colorNeedleEnd: 'rgba(255, 160, 122, .9)',
            fontNumbersSize: 30,
            fontUnitsSize: 45,
            fontUnitsWeight: 'bold',
            borderOuterWidth: 0,
            borderMiddleWidth: 0,
            borderInnerWidth: 0,
            borderShadowWidth: 0,
            needle: radial,
            fontValueWeight: 'bold',
            valueBox: radial,
            valueInt: 0,
            valueDec: 1,
            colorValueText: 'white',
            colorValueBoxBackground: 'transparent',
            // colorValueBoxBackground: '#2177AD',
            valueBoxStroke: 0,
            fontValueSize: 45,
            valueTextShadow: false,
            animationRule: 'bounce',
            animationDuration: 500,
            barWidth: radial ? 0 : 7,
            numbersMargin: 0,

            // linear gauge specials

            barBeginCircle: fact.unit == '°C' ? 15 : 0,
            tickSide: 'left',
            needleSide: 'right',
            numberSide: 'left',
            colorPlate: 'transparent'
         };

         if (radial)
            gauge = new RadialGauge(options);
         else
            gauge = new LinearGauge(options);

         gauge.draw();
         $('#widget' + fact.type + fact.address).data('gauge', gauge);

         break;

      case 7:     // 7 (PlainText)
         var html = '<div id="widget' + fact.type + fact.address + '" class="widget-value" style="height:inherit;"></div>';
         elem.className = "widgetPlain rounded-border";
         elem.innerHTML = html;
         break;

      default:   // type 2(Text), 3(Value)
         var html = "";
         html += '<div class="widget-title">' + title + '</div>';
         html += '<div id="widget' + fact.type + fact.address + '" class="widget-value"></div>\n';
         elem.className = "widget rounded-border";
         elem.innerHTML = html;
         break;
   }
}

function updateDashboard(widgets, refresh)
{
   // console.log("updateDashboard");

   if (widgets)
   {
      for (var i = 0; i < widgets.length; i++)
      {
         updateWidget(widgets[i], refresh)
      }
   }
}

function updateWidget(sensor, refresh, fact)
{
   if (fact == null)
      fact = valueFacts[sensor.type + ":" + sensor.address];

   console.log("updateWidget " + sensor.name + " of type " + fact.widgettype);

   if (fact == null || fact == undefined) {
      console.log("Fact for widget '" + sensor.type + ":" + sensor.address + "' not found, ignoring");
      return ;
   }

   if (fact.widgettype == 0)         // Symbol
   {
      var modeStyle = sensor.options == 3 && sensor.mode == 'manual' ? "background-color: #a27373;" : "";
      var image = sensor.value != 0 ? fact.imgon : fact.imgoff;
      if (image == null) image = 'img/icon/unknown.png';
      $("#widget" + fact.type + fact.address).attr("src", image);

      var ddd = "div_" + fact.type + ":0x" + fact.address.toString(16).padStart(2, '0');
      var e = document.getElementById(ddd);
      e.setAttribute("style", modeStyle);
      // $('#' + ddd).attr("style", modeStyle);  // #TODO geht nicht??

      var e = document.getElementById("progress" + fact.type + fact.address);
      if (e != null)
         e.style.visibility = (sensor.next == null || sensor.next == 0) ? "hidden" : "visible";
      // else console.log("Element '" + "progress" + sensor.type + sensor.address + "' not found");

      if (sensor.mode == "auto" && sensor.next > 0) {
         var pWidth = 100;
         var s = sensor;
         var id = fact.type + fact.address;
         var iid = setInterval(progress, 200);

         function progress() {
            if (pWidth <= 0) {
               clearInterval(iid);
            } else {
               var d = new Date();
               pWidth = 100 - ((d/1000 - s.last) / ((s.next - s.last) / 100));
               document.getElementById("progressBar" + id).style.width = pWidth + "%";
            }
         }
      }
   }
   else if (fact.widgettype == 1)    // Chart
   {
      // var elem = $("#widget" + fact.type + fact.address);

      $("#peak" + fact.type + fact.address).text(sensor.peak != null ? sensor.peak.toFixed(2) + " " + fact.unit : "");

      if (!sensor.disabled) {
         $("#value" + fact.type + fact.address).text(sensor.value.toFixed(2) + " " + fact.unit);
         $("#value" + fact.type + fact.address).css('color', "var(--buttonFont)")
      }
      else {
         $("#value" + fact.type + fact.address).css('color', "var(--caption2)")
         $("#value" + fact.type + fact.address).text("(" + sensor.value.toFixed(2) + (fact.unit!="" ? " " : "") + fact.unit + ")");
      }

      if (refresh) {
         var jsonRequest = {};
         prepareChartRequest(jsonRequest, fact.type + ":0x" + fact.address.toString(16) , 0, 1, "chartwidget");
         socket.send({ "event" : "chartdata", "object" : jsonRequest });
      }
   }
   else if (fact.widgettype == 2 || fact.widgettype == 7)      // Text, PlainText
   {
      if (sensor.text != undefined) {
         var text = sensor.text.replace(/(?:\r\n|\r|\n)/g, '<br>');
         $("#widget" + fact.type + fact.address).html(text);
      }
   }
   else if (fact.widgettype == 3)      // plain value
   {
      $("#widget" + fact.type + fact.address).html(sensor.value + " " + fact.unit);
   }
   else if (fact.widgettype == 4)    // Gauge
   {
      var value = sensor.value.toFixed(2);
      var scaleMax = !fact.scalemax || fact.unit == '%' ? 100 : fact.scalemax.toFixed(0);
      var scaleMin = value >= 0 ? "0" : Math.ceil(value / 5) * 5 - 5;

      if (scaleMax < Math.ceil(value))       scaleMax = value;
      if (scaleMax < Math.ceil(sensor.peak)) scaleMax = sensor.peak.toFixed(0);

      $("#sMin" + fact.type + fact.address).text(scaleMin);
      $("#sMax" + fact.type + fact.address).text(scaleMax);
      $("#value" + fact.type + fact.address).text(value + " " + fact.unit);

      var ratio = (value - scaleMin) / (scaleMax - scaleMin);
      var peak = (sensor.peak.toFixed(2) - scaleMin) / (scaleMax - scaleMin);

      $("#pb" + fact.type + fact.address).attr("d", "M 950 500 A 450 450 0 0 0 50 500");
      $("#pv" + fact.type + fact.address).attr("d", svg_circle_arc_path(500, 500, 450 /*radius*/, -90, ratio * 180.0 - 90));
      $("#pp" + fact.type + fact.address).attr("d", svg_circle_arc_path(500, 500, 450 /*radius*/, peak * 180.0 - 91, peak * 180.0 - 90));
   }
   else if (fact.widgettype == 5 || fact.widgettype == 6)    // Meter
   {
      // console.log("DEBUG: Update " + '#widget' + fact.type + fact.address + " to: " + sensor.value);
      if (sensor.value != undefined) {
         $('#widgetValue' + fact.type + fact.address).html(sensor.value.toFixed(fact.unit == '%' ? 0 : 1) + ' ' + fact.unit);
         var gauge = $('#widget' + fact.type + fact.address).data('gauge');
         if (gauge != null)
            gauge.value = sensor.value;
         else
            console.log("Missing gauge instance for " + '#widget' + fact.type + fact.address);
      }
      else
         console.log("Missing value for " + '#widget' + fact.type + fact.address);
      //  = fact.address != 4 ? value = sensor.value : 45;  // for test
   }
}

function toggleChartDialog(type, address)
{
   var dialog = document.querySelector('dialog');
   dialog.style.position = 'fixed';

   if (type != "" && !dialog.hasAttribute('open')) {
      var canvas = document.querySelector("#chartDialog");
      canvas.getContext('2d').clearRect(0, 0, canvas.width, canvas.height);
      chartDialogSensor = type + address;

      // show the dialog

      dialog.setAttribute('open', 'open');

      var jsonRequest = {};
      prepareChartRequest(jsonRequest, type + ":0x" + address.toString(16) , 0, 1, "chartdialog");
      socket.send({ "event" : "chartdata", "object" : jsonRequest });

      // EventListener für ESC-Taste

      document.addEventListener('keydown', closeChartDialog);

      // only hide the background *after* you've moved focus out of
      //   the content that will be "hidden"

      var div = document.createElement('div');
      div.id = 'backdrop';
      document.body.appendChild(div);
   }
   else {
      chartDialogSensor = "";
      document.removeEventListener('keydown', closeChartDialog);
      dialog.removeAttribute('open');
      var div = document.querySelector('#backdrop');
      div.parentNode.removeChild(div);
   }
}

function closeChartDialog(event)
{
   if (event.keyCode == 27)
      toggleChartDialog("", 0);
}

// ---------------------------------
// gauge stuff ...

function polar_to_cartesian(cx, cy, radius, angle)
{
   var radians = (angle - 90) * Math.PI / 180.0;
   return [Math.round((cx + (radius * Math.cos(radians))) * 100) / 100, Math.round((cy + (radius * Math.sin(radians))) * 100) / 100];
};

function svg_circle_arc_path(x, y, radius, start_angle, end_angle)
{
   var start_xy = polar_to_cartesian(x, y, radius, end_angle);
   var end_xy = polar_to_cartesian(x, y, radius, start_angle);
   return "M " + start_xy[0] + " " + start_xy[1] + " A " + radius + " " + radius + " 0 0 0 " + end_xy[0] + " " + end_xy[1];
};

// ---------------------------------
// drag&drop stuff ...

function dragWidget(ev)
{
   console.log("drag: " + ev.target.getAttribute('id'));
   ev.dataTransfer.setData("source", ev.target.getAttribute('id'));
}

function dropWidget(ev)
{
   ev.preventDefault();
   var target = ev.target;
   while (target) {
      if (target.dataset.droppoint)
         break;
      target = target.parentElement;
   }

   var source = document.getElementById(ev.dataTransfer.getData("source"));
   console.log("drop element: " + source.getAttribute('id') + ' on ' + target.getAttribute('id'));
   target.after(source);

   var list = '';
   $('#widgetContainer > div').each(function () {
      // console.log(" - " + $(this).attr('id'));
      var sensor = $(this).attr('id').substring($(this).attr('id').indexOf("_") + 1);
      list += sensor + ',';
   });

   socket.send({ "event" : "storeconfig", "object" : { "addrsDashboard" : list } });
   console.log(" - " + list);
}
