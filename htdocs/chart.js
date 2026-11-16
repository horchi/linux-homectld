/*
 *  chart.js
 *
 *  (c) 2020 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

var currentRequest = null;

function drawCharts(dataObject)
{
   var root = document.getElementById("chartContainer");

   if (!root) {
      $('#container').removeClass('hidden');
      $('#dashboardMenu').removeClass('hidden');
      $('#dashboardMenu').html('');

      document.getElementById("dashboardMenu").innerHTML =
         '<div class="chartHeader">' +
         '  <div id="chartTitle" class="chartTitle"></div>' +
         '  <div id="chartBookmarks" class="chartBookmarks"></div>' +
         '</div>';

      document.getElementById("container").innerHTML =
         '<div class="chartCard">' +
         '  <div id="chartContainer" class="chartContainer">' +
         '    <canvas id="chartCanvas" class="chartCanvas"></canvas>' +
         '  </div>' +
         '  <div class="chartButtons">' +
         '    <div class="chartNav">' +
         '    <div class="chartSegment">' +
         '      <button class="chartButton" title="30 Tage zurück" onclick="chartSelect(\'prevmonth\')">&#10094;&#10094; <span class="chartLong">Monat</span><span class="chartShort">M</span></button>' +
         '      <button class="chartButton" title="7 Tage zurück" onclick="chartSelect(\'prevweek\')">&#10094; <span class="chartLong">Woche</span><span class="chartShort">W</span></button>' +
         '      <button class="chartButton" title="1 Tag zurück" onclick="chartSelect(\'prev\')">&#10094; <span class="chartLong">Tag</span><span class="chartShort">T</span></button>' +
         '    </div>' +
         '    <button class="chartButton chartButtonNow" onclick="chartSelect(\'now\')">Jetzt</button>' +
         '    <div class="chartSegment">' +
         '      <button class="chartButton" title="1 Tag vor" onclick="chartSelect(\'next\')"><span class="chartLong">Tag</span><span class="chartShort">T</span> &#10095;</button>' +
         '      <button class="chartButton" title="7 Tage vor" onclick="chartSelect(\'nextweek\')"><span class="chartLong">Woche</span><span class="chartShort">W</span> &#10095;</button>' +
         '      <button class="chartButton" title="30 Tage vor" onclick="chartSelect(\'nextmonth\')"><span class="chartLong">Monat</span><span class="chartShort">M</span> &#10095;&#10095;</button>' +
         '    </div>' +
         '    </div>' +
         '    <div class="chartInputs">' +
         '      <label class="chartField"><span>Start</span><input class="chartInput" type="date" onchange="chartSelect(\'setstart\')" id="chartStart" min="2020-01-01"></input></label>' +
         '      <label class="chartField"><span>Tage</span><input class="chartInput chartInputRange" onchange="chartSelect(\'range\')" id="chartRange" type="number" step="0.25" min="0.25" value="' + theChartRange + '"></input></label>' +
         '    </div>' +
         '  </div>' +
         '</div>' +
         '<div id="chartSelector" class="chartSelectors"></div>';

      document.getElementById("chartStart").valueAsDate = theChartStart;
   }

   root = document.getElementById("chartCanvas");

   if (theChart != null) {
      theChart.destroy();
      theChart = null;
   }

   // Farben aus dem aktiven Theme

   var style = getComputedStyle(document.documentElement);
   var cssVar = function(name, def) { var v = style.getPropertyValue(name).trim(); return v != '' ? v : def; };
   var fontColor = cssVar('--fontColor', 'white');
   var gridColor = cssVar('--neutral2', '#545454');
   var mutedColor = cssVar('--neutral4', '#a3a3a3');
   var tipBackground = cssVar('--neutral1', '#454545');
   var multiDay = theChartRange > 1;

   var data = {
      type: "line",
      data: {
         labels: [],
         datasets: []
      },
      options: {
         responsive: true,
         maintainAspectRatio: false,
         animation: { duration: 300 },
         tooltips: {
            mode: "index",
            intersect: false,
            backgroundColor: tipBackground,
            titleFontColor: fontColor,
            bodyFontColor: fontColor,
            borderColor: gridColor,
            borderWidth: 1,
            xPadding: 10,
            yPadding: 8,
            caretPadding: 6,
            cornerRadius: 6,
            callbacks: {
               title: function(items) { return items.length ? moment(items[0].xLabel).format('dd DD.MM.YYYY HH:mm') : ''; },
               label: function(item, d) {
                  var ds = d.datasets[item.datasetIndex];
                  if (ds.logical == 'state')
                     return ' ' + ds.label + ': ' + (item.yLabel > 0 ? 'An' : 'Aus');
                  if (ds.logical == 'duty')
                     return ' ' + ds.label + ': ' + Math.round(item.yLabel * 100) + ' % an';
                  return ' ' + ds.label + ': ' + item.yLabel.toLocaleString(undefined, { maximumFractionDigits: 2 })
                     + (ds.unit ? ' ' + ds.unit : '');
               }
            }
         },
         hover: {
            mode: "index",
            intersect: false
         },
         legend: {
            display: true,
            position: 'top',
            labels: {
               filter: function(item, d) { return !d.datasets[item.datasetIndex].logical; },   // Bänder haben ihren Titel links
               fontColor: fontColor,
               usePointStyle: true,
               boxWidth: 8,
               padding: 14
            }
         },
         scales: {
            xAxes: [{
               type: "time",
               distribution: "linear",
               display: true,
               time: {
                  tooltipFormat: 'DD.MM.YYYY HH:mm',
                  displayFormats: {
                     millisecond: 'HH:mm:ss',
                     second: 'HH:mm:ss',
                     minute: 'HH:mm',
                     hour: multiDay ? 'DD.MM. HH:mm' : 'HH:mm',
                     day: 'DD.MM.',
                     week: 'DD.MM.',
                     month: 'MMM YYYY',
                     quarter: 'MMM YYYY',
                     year: 'YYYY' }
               },
               ticks: {
                  autoSkip: true,          // Anzahl aus Breite / Labelgröße
                  autoSkipPadding: 30,
                  maxRotation: 0,
                  padding: 8,
                  fontColor: mutedColor
               },
               gridLines: {
                  color: gridColor,
                  drawBorder: false,
                  drawTicks: false,
                  borderDash: [3,4]
               }
            }],
         }
      }
   };

   // console.log("dataObject: " + JSON.stringify(dataObject, undefined, 4));

   var colors = ['#3498db','#f39c12','#2ecc71','#e74c3c','#9b59b6','#1abc9c','#e67e22','#ec407a','#f1c40f','#95a5a6','#00bcd4'];
   var yAxes = [];
   var knownUnits = {};
   var sensorColors = {};

   var lanes = [];          // Index der Datasets mit logischen Werten (Statusbänder)

   for (var i = 0; i < dataObject.rows.length; i++) {
      var unit = valueFacts[dataObject.rows[i].key].unit;
      var color = colors[i % colors.length];
      var dataset = {};

      sensorColors[dataObject.rows[i].key] = color;

      // logische Werte (siehe wsactions.c): unsichtbares Dataset auf eigener Achse für Tooltip
      //  und Zeitachse, gezeichnet wird es vom statusLanesPlugin als Band unter dem Chart

      if (dataObject.rows[i].logical) {
         lanes.push(data.data.datasets.length);
         data.data.datasets.push({
            data: dataObject.rows[i].data,
            label: dataObject.rows[i].title,
            logical: dataObject.rows[i].logical,
            backgroundColor: color,
            borderColor: color,
            showLine: false,
            fill: false,
            pointRadius: 0,
            pointHoverRadius: 0,
            pointHitRadius: 0,
            yAxisID: 'logical'
         });
         continue;
      }

      if (knownUnits[unit] == null) {
         // eine Y-Achse pro Einheit, abwechselnd links / rechts, Gitter nur von der ersten

         knownUnits[unit] = true;
         yAxes.push({
            id: unit,
            display: true,
            position: yAxes.length % 2 ? 'right' : 'left',
            ticks: { padding: 8, maxTicksLimit: 10, fontColor: color },
            gridLines: { color: gridColor, zeroLineColor: mutedColor, drawBorder: false, drawTicks: false,
                         borderDash: [3,4], drawOnChartArea: yAxes.length == 0 },
            scaleLabel: { display: unit != '', fontColor: color, labelString: unit }
         });
      }

      dataset["data"] = dataObject.rows[i].data;
      dataset["backgroundColor"] = color;
      dataset["borderColor"] = color;
      dataset["label"] = dataObject.rows[i].title;
      dataset["unit"] = unit;
      dataset["borderWidth"] = 2;
      dataset["fill"] = false;
      dataset["lineTension"] = 0.2;
      dataset["pointRadius"] = 0;
      dataset["pointHoverRadius"] = 4;
      dataset["pointHoverBorderColor"] = fontColor;
      dataset["pointHoverBorderWidth"] = 2;
      dataset["yAxisID"] = unit;

      data.data.datasets.push(dataset);
   }

   if (lanes.length)
      setupStatusLanes(data, yAxes, lanes, { gridColor: gridColor, mutedColor: mutedColor, background: cssVar('--neutral1', '#454545') });

   data.options.scales.yAxes = yAxes;
   var end = new Date();
   end.setFullYear(theChartStart.getFullYear(), theChartStart.getMonth(), theChartStart.getDate()+theChartRange);

   $("#chartTitle").html(moment(theChartStart).format('dd DD.MM.YYYY HH:mm') + ' &ndash; ' + moment(end).format('dd DD.MM.YYYY HH:mm'));
   $("#chartSelector").html("");

   updateChartBookmarks();

   for (var i = 0; i < dataObject.sensors.length; i++) {
      // console.log("sensor " + dataObject.sensors[i].id);
      var fact = valueFacts[dataObject.sensors[i].id];

      var color = sensorColors[dataObject.sensors[i].id];
      var unit = fact && fact.unit ? ' <span class="chartSelUnit">' + fact.unit + '</span>' : '';
      var html = '<div class="chartSel"><input id="checkChartSel_' + dataObject.sensors[i].id +
          '" type="checkbox" onclick="chartSelect(\'choice\')" ' + (dataObject.sensors[i].active ? 'checked' : '') +
          '/><label for="checkChartSel_' + dataObject.sensors[i].id + '"' + (color ? ' style="--selColor:' + color + '"' : '') + '>'
          + dataObject.sensors[i].title + unit + '</label></div>';
      $("#chartSelector").append(html);
   }

   theChart = new Chart(root.getContext("2d"), data);

   // calc container size

   $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('dashboardMenu') - getTotalHeightOf('footer') - 15);
   window.onresize = function() {
      $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('dashboardMenu') - getTotalHeightOf('footer') - 15);
   };
}

//***************************************************************************
// Statusbänder für logische Werte
//   je Sensor ein Band zwischen Plot und Zeitachse, Titel links
//   'state': an (0/1), 'duty': Einschaltdauer 0..1 als Deckkraft
//***************************************************************************

function setupStatusLanes(data, yAxes, lanes, colors)
{
   var laneH = 14, laneGap = 5, top = 10;
   var lanesHeight = top + lanes.length * (laneH + laneGap);
   var font = '11px ' + Chart.defaults.global.defaultFontFamily;
   var analog = yAxes.length > 0;

   // Platz für die Titel links

   var ctx = document.createElement('canvas').getContext('2d');
   ctx.font = font;
   var labelWidth = 60;

   for (var i = 0; i < lanes.length; i++)
      labelWidth = Math.max(labelWidth, ctx.measureText(data.data.datasets[lanes[i]].label).width + 14);

   labelWidth = Math.min(labelWidth, 160);

   if (analog)
      yAxes[0].afterFit = function(scale) { scale.width = Math.max(scale.width, labelWidth); };

   yAxes.push({
      id: 'logical',
      display: true,
      position: 'left',
      ticks: { display: false, min: 0, max: 1 },
      gridLines: { display: false, drawBorder: false },
      afterFit: function(scale) { scale.width = analog ? 0 : labelWidth; }
   });

   // Zeitachse unter die Bänder schieben

   data.options.scales.xAxes[0].ticks.padding = 8 + lanesHeight;

   data.plugins = [{
      afterDraw: function(chart) {
         var c = chart.ctx;
         var area = chart.chartArea;
         var xScale = chart.scales['x-axis-0'];

         c.save();
         c.font = font;
         c.textBaseline = 'middle';
         c.textAlign = 'right';

         for (var l = 0; l < lanes.length; l++) {
            var ds = chart.data.datasets[lanes[l]];
            var y = area.bottom + top + l * (laneH + laneGap);
            var points = ds.data || [];

            // Titel (gekürzt)

            var title = ds.label;
            while (title.length > 1 && c.measureText(title).width > labelWidth - 10)
               title = title.slice(0, -2) + '…';

            c.fillStyle = colors.mutedColor;
            c.fillText(title, area.left - 6, y + laneH / 2);

            // Hintergrund

            c.fillStyle = colors.background;
            c.fillRect(area.left, y, area.right - area.left, laneH);

            if (chart.getDatasetMeta(lanes[l]).hidden || !points.length)
               continue;

            // typischer Abstand der Samples -> Lücken ohne Daten nicht füllen

            var times = points.map(function(p) { return moment(p.x).valueOf(); });
            var diffs = [];
            for (var i = 1; i < times.length; i++) diffs.push(times[i] - times[i-1]);
            diffs.sort(function(a, b) { return a - b; });
            var step = diffs.length ? diffs[Math.floor(diffs.length / 2)] : 5*60*1000;

            c.save();
            c.beginPath();
            c.rect(area.left, y, area.right - area.left, laneH);
            c.clip();
            c.fillStyle = ds.borderColor;

            for (var i = 0; i < points.length; i++) {
               var v = points[i].y;
               if (!(v > 0))
                  continue;

               var t1 = i + 1 < times.length ? Math.min(times[i+1], times[i] + 2*step) : times[i] + step;
               var x0 = Math.floor(xScale.getPixelForValue(times[i]));
               var x1 = Math.ceil(xScale.getPixelForValue(t1));

               c.globalAlpha = ds.logical == 'duty' ? 0.15 + 0.85 * Math.min(v, 1) : 1;
               c.fillRect(x0, y, Math.max(x1 - x0, 1), laneH);
            }

            c.restore();
         }

         c.restore();
      }
   }];
}

function getSensors()
{
   var sensors = "";
   var root = document.getElementById("chartSelector");
   var elements = root.querySelectorAll("[id^='checkChartSel_']");

   for (var i = 0; i < elements.length; i++) {
      if (elements[i].checked) {
         var id = elements[i].id.substring(elements[i].id.indexOf("_") + 1);
         sensors += id + ",";
       }
   }

   return sensors;
}

var refreshTimer = null;

function setRefreshTimer()
{
   if (refreshTimer)
      clearTimeout(refreshTimer);

   if ($('#refresh').is(":checked")) {
      refreshTimer = setTimeout(function() {
         refresh();
         console.log("refresh");
      }, 120000);
   }
}

function refresh()
{
   if (currentRequest) {
      console.log("do refresh", JSON.stringify(currentRequest, undefined, 4));
      socket.send({ "event" : "chartdata", "object" : currentRequest });
      showProgressDialog();
   }
}

function updateChartBookmarks()
{
   var html = '';

   if (localStorage.getItem(storagePrefix + 'Rights') & 0x08 || localStorage.getItem(storagePrefix + 'Rights') & 0x10)
      html += '<button title="Lesezeichen hinzufügen" class="chartBookmarkButton chartBookmarkIcon" onclick="addBookmark()">&#128209;</button>'
         + '<button title="zum Löschen hier ablegen" ondrop="dropBm(event)" ondragover="allowDropBm(event)" class="chartBookmarkButton chartBookmarkIcon">&#128465;</button>'
         + '<span class="chartBookmarkSep"></span>';

   if (chartBookmarks != null) {
      for (var i = 0; i < chartBookmarks.length; i++)
         html += '<button class="chartBookmarkButton" ondragstart="dragBm(event, \'' + chartBookmarks[i].name + '\')" draggable="true" onclick="chartSelectBookmark(\'' + chartBookmarks[i].sensors + '\')">' + chartBookmarks[i].name + '</button>';
   }

   html += '<div class="chartRefresh"><input id="refresh" type="checkbox"' + (refreshTimer != null ? ' checked' : '') + '/><label for="refresh">Refresh</label></div>';

   $("#chartBookmarks").html(html);
   $("#refresh").click(function() {setRefreshTimer()});

   setRefreshTimer();
}

function dragBm(ev, name)
{
   console.log("drag: " + name);
   ev.dataTransfer.setData("name", name);
}

function allowDropBm(ev)
{
   console.log("allowDropBm");
   ev.preventDefault();
}

function dropBm(ev)
{
   ev.preventDefault();
   var name = ev.dataTransfer.getData("name");

   console.log("dropBm: " + name);
   for (var i = 0; i < chartBookmarks.length; i++) {
      if (chartBookmarks[i].name == name) {
         chartBookmarks.splice(i, 1);
         break;
      }
   }

   socket.send({ "event" : "storechartbookmarks", "object" : chartBookmarks});

   // console.log("bookmarks now " + JSON.stringify(chartBookmarks, undefined, 4));
}

Date.prototype.subHours = function(h)
{
  this.setTime(this.getTime() - (h*60*60*1000));
  return this;
}

function chartSelect(action)
{
   theChartRange = parseFloat($("#chartRange").val());
   console.log("theChartRange: " + theChartRange);
   var sensors = getSensors();
   var now = new Date();

   if (action == "next")
      theChartStart.setFullYear(theChartStart.getFullYear(), theChartStart.getMonth(), theChartStart.getDate()+1);
   else if (action == "nextmonth")
      theChartStart.setFullYear(theChartStart.getFullYear(), theChartStart.getMonth(), theChartStart.getDate()+30);
   else if (action == "nextweek")
      theChartStart.setFullYear(theChartStart.getFullYear(), theChartStart.getMonth(), theChartStart.getDate()+7);
   else if (action == "prev")
      theChartStart.setFullYear(theChartStart.getFullYear(), theChartStart.getMonth(), theChartStart.getDate()-1);
   else if (action == "prevmonth")
      theChartStart.setFullYear(theChartStart.getFullYear(), theChartStart.getMonth(), theChartStart.getDate()-30);
   else if (action == "prevweek")
      theChartStart.setFullYear(theChartStart.getFullYear(), theChartStart.getMonth(), theChartStart.getDate()-7);
   else if (action == "now")
      theChartStart.setFullYear(now.getFullYear(), now.getMonth(), now.getDate()-theChartRange);
   else if (action == "range")
      ;
   else if (action == "choice")
      ;
   else if (action == "setstart")
      theChartStart = new Date(document.getElementById("chartStart").value);
   else
      console.log("chartSelect(" + action + ")");

   // console.log("sensors:  '" + sensors + "'" + ' Range:' + theChartRange);

   var jsonRequest = {};
   prepareChartRequest(jsonRequest, sensors, theChartStart, theChartRange, "chart");
   currentRequest = jsonRequest;
   socket.send({ "event" : "chartdata", "object" : jsonRequest });
   showProgressDialog();
}

function chartSelectBookmark(sensors)
{
   console.log("bookmark: " + sensors);

   var jsonRequest = {};
   prepareChartRequest(jsonRequest, sensors, theChartStart, theChartRange, "chart");
   currentRequest = jsonRequest;
   socket.send({ "event" : "chartdata", "object" : jsonRequest });
   showProgressDialog();
}

function addBookmark()
{
   var sensors = getSensors();

   var form = '<form><div>Sensoren: ' + sensors + '</div><br/><input type="text" name="input"><br></form>';

   $(form).dialog({
      modal: true,
      width: "60%",
      title: "Lesezeichen",
      buttons: {
         'Speichern': function () {
            var name = $('input[name="input"]').val();
            if (name != "")
               storeBookmark(name, sensors);
            $(this).dialog('close');
         },
         'Abbrechen': function () {
            $(this).dialog('close');
         }
      },
      close: function() { $(this).dialog('destroy').remove(); }
   });

   function storeBookmark(name, sensors) {
      if (chartBookmarks != null)
         chartBookmarks.push({ "name"  : name, "sensors" : sensors });
      else
         chartBookmarks = [{ "name"  : name, "sensors" : sensors }];

      console.log("storing " + name + " - " + sensors);
      socket.send({ "event" : "storechartbookmarks", "object" : chartBookmarks});
   }
}
