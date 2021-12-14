/*
 *  main.js
 *
 *  (c) 2020-2021 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

var WebSocketClient = window.WebSocketClient
var colorStyle = null;

var onSmalDevice = false;
var isActive = null;
var socket = null;
var config = {};
var daemonState = {};

var widgetTypes = {};
var valueFacts = {};
var dashboards = {};
var allSensors = [];

var images = [];
var currentPage = "dashboard";
var widgetCharts = {};
var theChart = null;
var theChartRange = null;
var theChartStart = null;
var chartDialogSensor = "";
var chartBookmarks = {};
var infoDialogTimer = null;
var grouplist = {};

var setupMode = false;

$('document').ready(function() {
   daemonState.state = -1;

   console.log("currentPage: " + currentPage);
   var url = "";

   if (window.location.href.startsWith("https"))
      url = "wss://" + location.hostname + ":" + location.port;
   else
      url = "ws://" + location.hostname + ":" + location.port;

   var protocol = myProtocol;

   connectWebSocket(url, protocol);
   colorStyle = getComputedStyle(document.body);
});

function onSocketConnect(protocol)
{
   var token = localStorage.getItem(storagePrefix + 'Token');
   var user = localStorage.getItem(storagePrefix + 'User');

   if (token == null) token = "";
   if (user == null)  user = "";

   socket.send({ "event" : "login", "object" :
                 { "type" : "active",
                   "user" : user,
                   "page" : currentPage,
                   "token" : token }
               });

   prepareMenu();
   document.title = pageTitle;
   onSmalDevice = window.matchMedia("(max-width: 740px)").matches;
   console.log("onSmalDevice : " + onSmalDevice);
}

function connectWebSocket(useUrl, protocol)
{
   console.log("try socket opened " + protocol);

   socket = new WebSocketClient({
      url: useUrl,
      protocol: protocol,
      autoReconnectInterval: 1000,
      onopen: function (){
         console.log("socket opened " + socket.protocol);
         if (isActive === null)     // wurde beim Schliessen auf null gesetzt
            onSocketConnect(protocol);
      }, onclose: function () {
         isActive = null;           // auf null setzen, dass ein neues login aufgerufen wird
      }, onmessage: function (msg) {
         dispatchMessage(msg.data)
      }.bind(this)
   });

   if (!socket)
      return !($el.innerHTML = "Your Browser will not support Websockets!");
}

function sleep(ms) {
   return new Promise(resolve => setTimeout(resolve, ms));
}

var infoDialog = null;

async function showInfoDialog(object)
{
   var message = object.message;
   var titleMsg = "";

   hideInfoDialog();

   if (object.status == -1) {
      titleMsg = "Error";
      message = message.replace(/\n/g, "<br/>");
      $('#infoBox').addClass('error-border');
   }
   else if (object.status < -1) {
      message = message.replace(/\n/g, "<br/>");
      titleMsg = "Information (" + object.status + ")";
      $('#infoBox').addClass('error-border');
   }
   else if (object.status == 1) {
      var array = message.split("#:#");
      titleMsg = array[0];
      message = array[1].replace(/\n/g, "<br/>");;
   }

   var msDuration = 2000;
   var bgColor = "";
   var cls = "no-titlebar";
   var align = "center";

   if (object.status != 0) {
      msDuration = 30000;
      cls = "";
      align = "left";
   }

   var progress = ($('<div></div>')
                   .addClass('progress-smaller')
                   .css('margin-right', '15px')
                   .click(function() { hideInfoDialog(); }));

   $('#infoBox').append($('<div></div>')
                        .css('overflow', 'hidden')
                        .css('display', 'inline-flex')
                        .css('align-items', 'center')
                        .click(function() {
                           clearTimeout(infoDialogTimer);
                           infoDialogTimer = null;
                           progress.addClass('hidden');
                           $('#progressButton').removeClass('hidden');
                        })
                        .append($('<span></span>')
                                .append($('<button></button>')
                                        .attr('id', 'progressButton')
                                        .css('margin-right', '15px')
                                        .addClass('rounded-border')
                                        .addClass('hidden')
                                        .click(function() { hideInfoDialog(); })
                                        .html('x')))
                        .append($('<span></span>')
                                .append(progress))
                        .append($('<span></span>')
                                .append($('<span></span>')
                                        .css('font-weight', 'bold')
                                        .html(titleMsg))
                                .append($('<br></br>')
                                        .addClass(titleMsg == '' ? 'hidden' : ''))
                                .append($('<span></span>')
                                        .html(message)))
                       );

   $('#infoBox').addClass('infoBox-active');

   infoDialogTimer = setTimeout(function() {
      hideInfoDialog();
   }, msDuration);
}

function hideInfoDialog()
{
   infoDialogTimer = null;

   if ($('#progressDiv') != null)
      $('#progressDiv').css("visibility", "hidden");

   $('#infoBox').html('');
   $('#infoBox').removeClass('infoBox-active');
   $('#infoBox').removeClass('error-border');
}

var progressDialog = null;

async function hideProgressDialog()
{
   if (progressDialog != null) {
      console.log("hide progress");
      progressDialog.dialog('destroy').remove();
      progressDialog = null;
   }
}

async function showProgressDialog()
{
   while (progressDialog)
      await sleep(100);

   var msDuration = 300000;   // timeout 5 minutes

   var form = document.createElement("div");
   form.style.overflow = "hidden";
   var div = document.createElement("div");
   form.appendChild(div);
   div.className = "progress";

   console.log("show progress");

   $(form).dialog({
      dialogClass: "no-titlebar rounded-border",
      width: "125px",
      title: "",
		modal: true,
      resizable: false,
		closeOnEscape: true,
      minHeight: "0px",
      hide: "fade",
      open: function() {
         progressDialog = $(this);
         setTimeout(function() {
            if (progressDialog)
               progressDialog.dialog('close');
            progressDialog = null }, msDuration);
      },
      close: function() {
         $(this).dialog('destroy').remove();
         progressDialog = null;
      }
   });
}

function dispatchMessage(message)
{
   var jMessage = JSON.parse(message);
   var event = jMessage.event;

   if (event != "chartdata")
      console.log("got event: " + event);

   if (event == "result") {
      hideProgressDialog();
      showInfoDialog(jMessage.object);
   }
   else if (event == "init") {
      allSensors = jMessage.object;
      if (currentPage == 'dashboard')
         initDashboard();
      else if (currentPage == 'schema')
         updateSchema();
      else if (currentPage == 'list')
         initList();
   }
   else if (event == "update" || event == "all") {
      if (event == "all") {
         allSensors = jMessage.object;
      }
      else {
         for (var key in jMessage.object)
            allSensors[key] = jMessage.object[key];
               }
      if (currentPage == 'dashboard')
         updateDashboard(jMessage.object, event == "all");
      else if (currentPage == 'schema')
         updateSchema();
      else if (currentPage == 'list')
         updateList();
   }
   else if (event == "schema") {
      initSchema(jMessage.object);
      // console.log("schema " + JSON.stringify(jMessage.object, undefined, 4));
   }
   else if (event == "chartbookmarks") {
      chartBookmarks = jMessage.object;
      updateChartBookmarks();
   }
   else if (event == "config") {
      config = jMessage.object;
   }
   else if (event == "configdetails" && currentPage == 'setup') {
      initConfig(jMessage.object)
   }
   else if (event == "userdetails" && currentPage == 'userdetails') {
      initUserConfig(jMessage.object);
   }
   else if (event == "daemonstate") {
      daemonState = jMessage.object;
   }
   else if (event == "widgettypes") {
      widgetTypes = jMessage.object;
   }
   else if (event == "syslog") {
      showSyslog(jMessage.object);
   }
   else if (event == "token") {
      localStorage.setItem(storagePrefix + 'Token', jMessage.object.value);
      localStorage.setItem(storagePrefix + 'User', jMessage.object.user);
      localStorage.setItem(storagePrefix + 'Rights', jMessage.object.rights);

      if (jMessage.object.state == "confirm") {
         window.location.replace("index.html");
      } else {
         if (document.getElementById("confirm"))
            document.getElementById("confirm").innerHTML = "<div class=\"infoError\"><b><center>Login fehlgeschlagen</center></b></div>";
      }
   }
   else if (event == "dashboards") {
      dashboards = jMessage.object;
      // console.log("dashboards " + JSON.stringify(dashboards, undefined, 4));
   }

   else if (event == "valuefacts") {
      valueFacts = jMessage.object;
      // console.log("valueFacts " + JSON.stringify(valueFacts, undefined, 4));
   }
   else if (event == "images") {
      images = jMessage.object;
      if (currentPage == 'images')
         initImages();
      // console.log("images " + JSON.stringify(images, undefined, 4));
   }
   else if (event == "chartdata") {
      hideProgressDialog();
      var id = jMessage.object.id;

      if (currentPage == 'chart') {                                 // the charts page
         drawCharts(jMessage.object);
      }
      else if (currentPage == 'dashboard' && id == "chartwidget") { // the dashboard widget
         drawChartWidget(jMessage.object);
      }
      else if (currentPage == 'dashboard' && id == "chartdialog") { // the dashboard chart dialog
         drawChartDialog(jMessage.object);
      }
   }
   else if (event == "groups") {
      initGroupSetup(jMessage.object);
   }
   else if (event == "grouplist") {
      grouplist = jMessage.object;
   }
   else if (event == "alerts") {
      initAlerts(jMessage.object);
   }
   else if (event == "errors") {
      initErrors(jMessage.object);
   }

   // console.log("event: " + event + " dispatched");
}

function prepareMenu()
{
   var html = "";
   // var haveToken = localStorage.getItem(storagePrefix + 'Token') && localStorage.getItem(storagePrefix + 'Token') != "";

   console.log("prepareMenu: " + currentPage);

   html += '<button class="rounded-border button1" onclick="mainMenuSel(\'dashboard\')">Dashboards</button>';
   html += '<button class="rounded-border button1" onclick="mainMenuSel(\'list\')">Liste</button>';
   html += '<button class="rounded-border button1" onclick="mainMenuSel(\'chart\')">Charts</button>';
   html += '<button class="rounded-border button1" onclick="mainMenuSel(\'schema\')">Schema</button>';
   html += '<button id="vdrMenu" class="rounded-border button1" onclick="mainMenuSel(\'vdr\')">VDR</button>';

   if (localStorage.getItem(storagePrefix + 'Rights') & 0x08 || localStorage.getItem(storagePrefix + 'Rights') & 0x10)
      html += '<button class="rounded-border button1" onclick="mainMenuSel(\'setup\')">Setup</button>';

   html +=
      '<button id="burgerMenu" class="rounded-border button1 menuLogin" onclick="menuBurger()">' +
      ' <div></div>' +
      ' <div></div>' +
      ' <div></div>' +
      '</button>';

   // buttons below menu

   if (currentPage == "setup" || currentPage == "iosetup" || currentPage == "userdetails" || currentPage == "groups" || currentPage == "alerts" || currentPage == "syslog" || currentPage == "images") {
      if (localStorage.getItem(storagePrefix + 'Rights') & 0x08 || localStorage.getItem(storagePrefix + 'Rights') & 0x10) {
         html += "<div>";
         html += '  <button class="rounded-border button2" onclick="mainMenuSel(\'setup\')">Allg. Konfiguration</button>';
         html += '  <button class="rounded-border button2" onclick="mainMenuSel(\'iosetup\')">IO Setup</button>';
         html += '  <button class="rounded-border button2" onclick="mainMenuSel(\'userdetails\')">User</button>';
         html += '  <button class="rounded-border button2" onclick="mainMenuSel(\'alerts\')">Sensor Alerts</button>';
         html += '  <button class="rounded-border button2" onclick="mainMenuSel(\'groups\')">Baugruppen</button>';
         html += '  <button class="rounded-border button2" onclick="mainMenuSel(\'images\')">Images</button>';
         html += '  <button class="rounded-border button2" onclick="mainMenuSel(\'syslog\')">Syslog</button>';
         html += "</div>";
      }
   }

   if (currentPage == "setup") {
      html += "<div class=\"confirmDiv\">";
      html += "  <button class=\"rounded-border buttonOptions\" onclick=\"storeConfig()\">Speichern</button>";
      html += "  <button class=\"rounded-border buttonOptions\" title=\"Letzter Reset: " + config.peakResetAt + "\" id=\"buttonResPeaks\" onclick=\"resetPeaks()\">Reset Peaks</button>";
      html += "  <button class=\"rounded-border buttonOptions\" onclick=\"sendMail('Test Mail', 'test')\">Test Mail</button>";
      html += "</div>";
   }
   else if (currentPage == "iosetup") {
      html += "<div class=\"confirmDiv\">";
      html += "  <button class=\"rounded-border buttonOptions\" onclick=\"storeIoSetup()\">Speichern</button>";
      html += "  <button class=\"rounded-border buttonOptions\" id=\"filterIoSetup\" onclick=\"filterIoSetup()\">[alle]</button>";
      html += '  <input id="incSearchName" class="input rounded-border clearableOD" placeholder="filter..." type="search" oninput="doIncrementalFilterIoSetup()"</input>';
      html += "</div>";
   }
   else if (currentPage == "groups") {
      html += "<div class=\"confirmDiv\">";
      html += "  <button class=\"rounded-border buttonOptions\" onclick=\"storeGroups()\">Speichern</button>";
      html += "</div>";
   }
   else if (currentPage == "alerts") {
      html += "<div class=\"confirmDiv\">";
      html += "  <button class=\"rounded-border buttonOptions\" onclick=\"storeAlerts()\">Speichern</button>";
      html += "</div>";
   }

   else if (currentPage == "login") {
      html += '<div id="confirm" class="confirmDiv"/>';
   }

   if (currentPage == "schema") {
      if (localStorage.getItem(storagePrefix + 'Rights') & 0x08 || localStorage.getItem(storagePrefix + 'Rights') & 0x10) {
         html += "<div class=\"confirmDiv\">";
         html += "  <button class=\"rounded-border buttonOptions\" onclick=\"schemaEditModeToggle()\">Anpassen</button>";
         html += "  <button class=\"rounded-border buttonOptions\" id=\"buttonSchemaAddItem\" title=\"Konstante (Text) hinzufügen\" style=\"visibility:hidden;\" onclick=\"schemaAddItem()\">&#10010;</button>";
         html += "  <button class=\"rounded-border buttonOptions\" id=\"buttonSchemaStore\" style=\"visibility:hidden;\" onclick=\"schemaStore()\">Speichern</button>";
         html += "</div>";
      }
   }

   $("#navMenu").html(html);

   // var msg = "DEBUG: Browser: '" + $.browser.name + "' : '" + $.browser.versionNumber + "' : '" + $.browser.version + "'";
   // socket.send({ "event" : "logmessage", "object" : { "message" : msg } });
}

function menuBurger()
{
   var haveToken = localStorage.getItem(storagePrefix + 'Token') && localStorage.getItem(storagePrefix + 'Token') != "";

   var form = '<div id="burgerPopup" style="padding:0px;">';
   if (haveToken) {
      form += '<button style="width:120px;" class="rounded-border button1" onclick="mainMenuSel(\'user\')">[' + localStorage.getItem(storagePrefix + 'User') + ']</button>';
      if (currentPage == 'dashboard')
         form += '  <br/><div><button style="width:120px;color" class="rounded-border button1 mdi mdi-lead-pencil" onclick=" setupDashboard()">' + (setupMode ? ' Stop Setup' : ' Setup Dashboard') + '</button></div>';
   }
   else
      form += '<button style="width:120px;" class="rounded-border button1" onclick="mainMenuSel(\'login\')">Login</button>';

   form += '</div>';

   $(form).dialog({
      position: { my: "right top", at: "right bottom", of: document.getElementById("burgerMenu") },
      minHeight: "auto",
      width: "auto",
      dialogClass: "no-titlebar rounded-border",
		modal: true,
      resizable: false,
		closeOnEscape: true,
      hide: "fade",
      open: function(event, ui) {
         $(event.target).parent().css('background-color', 'var(--light1)');
         $('.ui-widget-overlay').bind('click', function()
                                      { $("#burgerPopup").dialog('close'); });
      },
      close: function() {
         $("body").off("click");
         $(this).dialog('destroy').remove();
      }
   });
}

function mainMenuSel(what)
{
   $("#burgerPopup").dialog("close");

   var lastPage = currentPage;
   currentPage = what;
   console.log("switch to " + currentPage);
   hideAllContainer();

   if (currentPage != lastPage && (currentPage == "vdr" || lastPage == "vdr")) {
      console.log("closing socket " + socket.protocol);
      socket.close();
      // delete socket;
      socket = null;

      var protocol = myProtocol;
      var url = "ws://" + location.hostname + ":" + location.port;

      if (currentPage == "vdr") {
         protocol = "osd2vdr";
         url = "ws://" + location.hostname + ":4444";
      }

      connectWebSocket(url, protocol);
   }

   prepareMenu();

   if (currentPage != "vdr")
      socket.send({ "event" : "pagechange", "object" : { "page"  : currentPage }});

   var event = null;
   var jsonRequest = {};

   if (currentPage == "setup")
      event = "setup";
   else if (currentPage == "iosetup")
      initIoSetup(valueFacts);
   else if (currentPage == "user")
      initUser();
   else if (currentPage == "userdetails")
      event = "userdetails";
   else if (currentPage == "images")
      initImages();
   else if (currentPage == "syslog") {
      showProgressDialog();
      event = "syslog";
   }
   else if (currentPage == "list")
      initList();
   else if (currentPage == "dashboard")
      initDashboard();
   else if (currentPage == "vdr")
      initVdr();
   else if (currentPage == "chart") {
      event = "chartdata";
      // console.log("config.chartSensors: " + config.chartSensors);
      // console.log("1 config.chartRange " + config.chartRange.replace(',', '.') + " :" + parseFloat(config.chartRange.replace(',', '.')));
      if (!theChartRange) {
         theChartRange = parseFloat(config.chartRange.replace(',', '.'));
         theChartStart = new Date().subHours(theChartRange * 24);
      }
      else if (!theChartStart)
         theChartStart = new Date().subHours(theChartRange * 24);

      prepareChartRequest(jsonRequest, config.chartSensors, theChartStart, theChartRange, "chart");
      currentRequest = jsonRequest;
   }
   else if (currentPage == "groups")
      event = "groups";
   else if (currentPage == "alerts")
      event = "alerts";
   else if (currentPage == "schema")
      event = "schema";

   if (currentPage != "vdr" && currentPage != "iosetup") {
      if (jsonRequest != {} && event != null)
         socket.send({ "event" : event, "object" : jsonRequest });
      else if (event != null)
         socket.send({ "event" : event, "object" : { } });

      if (currentPage == 'login')
         initLogin();
   }
}

function setupDashboard()
{
   $("#burgerPopup").dialog("close");

   setupMode = !setupMode;
   initDashboard();
}

function resetBatt(what)
{
   socket.send({ "event" : "reset", what : {}});
}

function initLogin()
{
   $('#container').removeClass('hidden');

   document.getElementById("container").innerHTML =
      '<div id="loginContainer" class="rounded-border inputTableConfig">' +
      '  <table>' +
      '    <tr>' +
      '      <td>User:</td>' +
      '      <td><input id="user" class="rounded-border input" type="text" value=""></input></td>' +
      '    </tr>' +
      '    <tr>' +
      '      <td>Passwort:</td>' +
      '      <td><input id="password" class="rounded-border input" type="password" value=""></input></td>' +
      '    </tr>' +
      '  </table>' +
      '  <button class="rounded-border buttonHighlighted" onclick="doLogin()">Anmelden</button>' +
      '</div>';
}

function showSyslog(log)
{
   $('#container').removeClass('hidden');
   document.getElementById("container").innerHTML = '<div id="syslogContainer" class="log"></div>';

   var root = document.getElementById("syslogContainer");
   root.innerHTML = log.lines.replace(/(?:\r\n|\r|\n)/g, '<br/>');
   hideProgressDialog();
}

window.sendMail = function(subject, body)
{
   socket.send({ "event" : "sendmail", "object" : { "subject" : subject, "body" : body } });
}

window.toggleMode = function(address, type)
{
   socket.send({ "event": "togglemode", "object":
                 { "address": address,
                   "type": type }
               });
}

window.toggleIo = function(address, type)
{
   socket.send({ "event": "toggleio", "object":
                 { "address": address,
                   "type": type }
               });
}

window.toggleIoNext = function(address, type)
{
   socket.send({ "event": "toggleionext", "object":
                 { "address": address,
                   "type": type }
               });
}

function doLogin()
{
   // console.log("login: " + $("#user").val() + " : " + $.md5($("#password").val()));
   socket.send({ "event": "gettoken", "object":
                 { "user": $("#user").val(),
                   "password": $.md5($("#password").val()) }
               });
}

function doLogout()
{
   localStorage.removeItem(storagePrefix + 'Token');
   localStorage.removeItem(storagePrefix + 'User');
   localStorage.removeItem(storagePrefix + 'Rights');
   console.log("logout");
   mainMenuSel('login');
}

function hideAllContainer()
{
   $('#dashboardMenu').addClass('hidden');
   $('#stateContainer').addClass('hidden');
   $('#container').addClass('hidden');
}

// ---------------------------------
// charts

function prepareChartRequest(jRequest, sensors, start, range, id)
{
   var gaugeSelected = false;

   if (!$.browser.safari || $.browser.versionNumber > 9)
   {
      const urlParams = new URLSearchParams(window.location.search);
      if (urlParams.has("sensors"))
      {
         gaugeSelected = true;
         sensors = urlParams.get("sensors");
      }
   }

   jRequest["id"] = id;
   jRequest["name"] = "chartdata";

   // console.log("requesting chart for '" + start + "' range " + range);

   if (gaugeSelected) {
      jRequest["sensors"] = sensors;
      jRequest["start"] = 0;   // default (use today-range)
      jRequest["range"] = 1;
   }
   else {
      jRequest["start"] = start == 0 ? 0 : Math.floor(start.getTime()/1000);  // calc unix timestamp
      jRequest["range"] = range;
      jRequest["sensors"] = sensors;
   }
}

function drawChartWidget(dataObject)
{
   var root = document.getElementById("container");
   var id = "widget" + dataObject.rows[0].sensor;

   if (widgetCharts[id] != null) {
      widgetCharts[id].destroy();
      widgetCharts[id] = null;
   }

   var data = {
      type: "line",
      data: {
         datasets: []
      },
      options: {
         legend: {
            display: false
         },
         responsive: true,
         maintainAspectRatio: false,
         aspectRatio: false,
         scales: {
            xAxes: [{
               type: "time",
               time: {
                  unit: 'hour',
                  unitStepSize: 1,
                  displayFormats: {
                     hour: 'HH:mm',
                     day: 'HH:mm'
                  }},
               distribution: "linear",
               display: true,
               gridLines: {
                  color: "gray"
               },
               ticks: {
                  padding: 10,
                  maxTicksLimit: 6,
                  maxRotation: 0,
                  fontColor: "darkgray"
               }

            }],
            yAxes: [{
               display: true,
               gridLines: {
                  color: "darkgray",
                  zeroLineColor: 'darkgray'
               },
               ticks: {
                  padding: 10,
                  maxTicksLimit: 5,
                  fontColor: "darkgray"
               }
            }]
         }
      }
   };

   for (var i = 0; i < dataObject.rows.length; i++) {
      var dataset = {};

      dataset["data"] = dataObject.rows[i].data;
      dataset["backgroundColor"] = "#415969";  // fill color
      dataset["borderColor"] = "#3498db";      // line color
      dataset["borderWidth"] = 1;
      dataset["fill"] = false;
      dataset["pointRadius"] = 1.5;
      data.data.datasets.push(dataset);
   }

   var canvas = document.getElementById(id);
   widgetCharts[id] = new Chart(canvas, data);
}

function drawChartDialog(dataObject)
{
   var root = document.querySelector('dialog')
   if (dataObject.rows[0].sensor != chartDialogSensor) {
      return ;
   }

   if (theChart != null) {
      theChart.destroy();
      theChart = null;
   }

   var data = {
      type: "line",
      data: {
         datasets: []
      },
      options: {
         tooltips: {
            mode: "index",
            intersect: false,
         },
         legend: {
            display: true
         },
         hover: {
            mode: "nearest",
            intersect: true
         },
         responsive: true,
         maintainAspectRatio: false,
         aspectRatio: false,
         scales: {
            xAxes: [{
               type: "time",
               time: {
                  unit: 'hour',
                  unitStepSize: 1,
                  displayFormats: {
                     hour: 'HH:mm',
                     day: 'HH:mm',
                  }},
               distribution: "linear",
               display: true,
               ticks: {
                  maxTicksLimit: 12,
                  padding: 10,
                  maxRotation: 0,
                  fontColor: "white"
               },
               gridLines: {
                  color: "gray",
                  borderDash: [5,5]
               },
               scaleLabel: {
                  display: true,
                  fontColor: "white",
                  labelString: "Zeit"
               }
            }],
            yAxes: [{
               display: true,
               ticks: {
                  padding: 10,
                  maxTicksLimit: 20,
                  fontColor: "white"
               },
               gridLines: {
                  color: "gray",
                  zeroLineColor: 'gray',
                  borderDash: [5,5]
               },
               scaleLabel: {
                  display: true,
                  fontColor: "white",
                  labelString: "Temperatur [°C]"
               }
            }]
         }
      }
   };

   for (var i = 0; i < dataObject.rows.length; i++)
   {
      var dataset = {};

      dataset["data"] = dataObject.rows[i].data;
      dataset["backgroundColor"] = "#415969";  // fill color
      dataset["borderColor"] = "#3498db";      // line color
      dataset["borderWidth"] = 1;
      dataset["fill"] = false;
      dataset["label"] = dataObject.rows[i].title;
      dataset["pointRadius"] = 0;
      data.data.datasets.push(dataset);
   }

   var canvas = root.querySelector("#chartDialog");
   theChart = new Chart(canvas, data);
}

function toKey(type, address)
{
   return type + ":0x" + address.toString(16).padStart(2, '0');
}

function parseBool(val)
{
   if ((typeof val === 'string' && (val.toLowerCase() === 'true' || val.toLowerCase() === 'yes' || val.toLowerCase() === 'ja')) || val === 1)
      return true;
   else if ((typeof val === 'string' && (val.toLowerCase() === 'false' || val.toLowerCase() === 'no' || val.toLowerCase() === 'nein')) || val === 0)
      return false;

   return null;
}

Number.prototype.pad = function(size)
{
  var s = String(this);
  while (s.length < (size || 2)) {s = "0" + s;}
  return s;
}

Date.prototype.toDatetimeLocal = function toDatetimeLocal()
{
   var date = this,
       ten = function (i) {
          return (i < 10 ? '0' : '') + i;
       },
       YYYY = date.getFullYear(),
       MM = ten(date.getMonth() + 1),
       DD = ten(date.getDate()),
       HH = ten(date.getHours()),
       II = ten(date.getMinutes()),
       SS = ten(date.getSeconds()),
       MS = date.getMilliseconds()
   ;

   return YYYY + '-' + MM + '-' + DD + 'T' +
      HH + ':' + II + ':' + SS; //  + ',' + MS;
};

Date.prototype.toDateLocal = function toDateLocal()
{
   var date = this,
       ten = function (i) {
          return (i < 10 ? '0' : '') + i;
       },
       YYYY = date.getFullYear(),
       MM = ten(date.getMonth() + 1),
       DD = ten(date.getDate())
   ;

   return YYYY + '-' + MM + '-' + DD;
};
