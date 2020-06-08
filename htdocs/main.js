
import WebSocketClient from "./websocket.js"
var isActive = null;
var socket = null;
var config = null;

function connectWebSocket()
{
    socket = new WebSocketClient({
        // url: "ws://" + location.hostname + ":61109",
        url: "ws://" + location.hostname + "/pool/ws",
        protocol: "pool",
        autoReconnectInterval: 2000,
        onopen: () => {
            console.log("socket opened :)");
            if (isActive === null)     // wurde beim Schliessen auf null gesetzt
            {
                $.ajax({
                    url : "ajax_interface.php",
                    type : "POST",
                    data : { "action" : 'gettoken' },
                    success: function(response) {
                        var token = response;
                        socket.send({ "event" : "login", "object" :
                                      { "type" : "active",
                                        "token" : token }
                                    });
                    },
                    error: function(jqXHR, textStatus, errorThrown) {
                        console.log(textStatus, errorThrown);
                    }
                });
            }
        }, onclose: () => {
            isActive = null;           // auf null setzen, dass ein neues login aufgerufen wird
        }, onmessage: (msg) => {
            dispatchMessage(msg.data)
        }
    });

    if (!socket)
        return !($el.innerHTML = "Your Browser will not support Websockets!");
}

function dispatchMessage(message)
{
    var jMessage = JSON.parse(message);
    var event = jMessage.event;
    var rootDashboard = document.getElementById("widgetContainer");
    var rootDashboardOld = document.getElementById("widgetContainerOld");
    var rootConfig = document.getElementById("configContainer");

    console.log("got event: " + event);

    if ((event == "update" || event == "all") && rootDashboard)
    {
        updateDashboard(jMessage.object);
    }
    else if (event == "init" && rootDashboard)
    {
        initDashboard(jMessage.object, rootDashboard);
        updateDashboard(jMessage.object);
    }
    else if (event == "init" && rootDashboardOld)
    {
        updateDashboard(jMessage.object);
    }
    else if (event == "config")
    {
        updateConfig(jMessage.object)
    }
    else if (event == "configdetails" && rootConfig)
    {
        initConfig(jMessage.object, rootConfig)
    }

    console.log("event: " + event + " dispatched");
}

function updateConfig(aConfig)
{
    config = aConfig;
}

function initConfig(configuration, root)
{
    var lastCat = "";

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
            if (i) html += "<br/>";
            html += "<div class=\"rounded-border seperatorTitle1\">" + item.category + "</div>";
            var elem = document.createElement("div");
            elem.innerHTML = html;
            root.appendChild(elem);
            html = "";
            lastCat = item.category;
        }

        html += "    <span>" + item.title + ":</span>\n";

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
            html += "    <span><input id=\"checkbox_" + item.name + "\" class=\"rounded-border input\" style=\"width:auto;\" type=\"checkbox\" " + (item.value == 1 ? "checked" : "") + "/></span>\n";
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
                    html += "     <input type=\"text\" class=\"rounded-border inputTime\" name=\"" + nameFrom + "\" value=\"" + range[0] + "\"/> -";
                    html += "     <input type=\"text\" class=\"rounded-border inputTime\" name=\"" + nameTo + "\" value=\"" + range[1] + "\"/>\n";
                    html += "   </span>\n";
                    html += "   <span></span>\n";
                }
            }

            var nameFrom = item.name + n + "From";
            var nameTo = item.name + n + "To";

            if (n > 0) html += "  <span/>  </span>\n";
            html += "  <span>\n";
            html += "    <input type=\"text\" class=\"rounded-border inputTime\" name=\"" + nameFrom + "\" value=\"\"/> -";
            html += "    <input type=\"text\" class=\"rounded-border inputTime\" name=\"" + nameTo + "\" value=\"\"/>\n";
            html += "  </span>\n";
            html += "  <span class=\"inputComment\">" + item.descrtiption + "</span>\n";

            break;
        }

        var elem = document.createElement("div");
        elem.innerHTML = html;
        root.appendChild(elem);
    }
}

function initDashboard(widgets, root)
{
    if (!widgets)
    {
        console.log("Faltal: Missing payload!");
        return;
    }

    for (var i = 0; i < widgets.length; i++)
    {
        var widget = widgets[i];

        switch(widget.widgettype) {
        case 0:           // Symbol
            var html = "";
            var ctrlButtons = widget.name == "Pool Light" && config.poolLightColorToggle == 1;

            html += "  <button class=\"widget-title\" type=\"button\" onclick=\"toggleMode(" + widget.address + ", '" + widget.type + "')\">" + widget.title + "</button>\n";
            html += "  <button class=\"widget-main\" type=\"button\" onclick=\"toggleIo(" + widget.address + ",'" + widget.type + "')\" >\n";
            html += "    <img id=\"widget" + widget.type + widget.address + "\" />\n";
            html += "   </button>\n";

            if (ctrlButtons)
            {
                html += "  <div class=\"widget-ctrl2\">\n";
                html += "    <button type=\"button\" onclick=\"toggleIoNext(" + widget.address + ",'" + widget.type + "')\">\n";
                html += "      <img src=\"img/icon/right.png\" />\n";
                html += "    </button>\n";
                html += "  </div>\n";
            }

            var elem = document.createElement("div");

            if (ctrlButtons)
                elem.className = "widgetCtrl rounded-border";
            else
                elem.className = "widget rounded-border";

            elem.setAttribute("id", "div" + widget.type + widget.address);
            elem.innerHTML = html;
            root.appendChild(elem);

            break;
        case 1:           // Gauge
            var html = "";

            html += "  <div class=\"widget-title\">" + widget.title + "</div>";
            html += "  <svg class=\"widget-svg\" viewBox=\"0 0 1000 600\" preserveAspectRatio=\"xMidYMin slice\">\n";
            html += "    <path id=\"pb" + widget.type + widget.address + "\"/>\n";
            html += "    <path class=\"data-arc\" id=\"pv" + widget.type + widget.address + "\"/>\n";
            html += "    <path class=\"data-peak\" id=\"pp" + widget.type + widget.address + "\"/>\n";
            html += "    <text id=\"value" + widget.type + widget.address + "\" text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"500\" y=\"450\" font-size=\"140\" font-weight=\"bold\"></text>\n";
            html += "    <text id=\"sMin" + widget.type + widget.address + "\" class='scale-text' text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"50\" y=\"550\"></text>\n";
            html += "    <text id=\"sMax" + widget.type + widget.address + "\" class='scale-text' text-anchor=\"middle\" alignment-baseline=\"middle\" x=\"950\" y=\"550\"></text>\n";
            html += "  </svg>\n";

            var elem = document.createElement("div");
            elem.className = "widgetGauge rounded-border participation";
            elem.setAttribute("id", "widget" + widget.type + widget.address);
            elem.setAttribute("href", "#");

            var start = new Date();  start.setHours(0,0,0,0);
            var url = "window.open('detail.php?width=1200&height=600&address=" + widget.address + "&type=" + widget.type + "&from="
                + Math.round(start/1000) + "&range=1&chartXLines=1&chartDiv=25 ','_blank',"
                + "'scrollbars=yes,width=1200,height=600,resizable=yes,left=120,top=120')";

            elem.setAttribute("onclick", url);

            elem.innerHTML = html;
            root.appendChild(elem);

            break;

        default:   // type 2(Text), 3(value), ...
            var html = "";
            html += "<div class=\"widget-title\">" + widget.title + "</div>\n";
            html += "<div id=\"widget" + widget.type + widget.address + "\" class=\"widget-value\"></div>\n";

            var elem = document.createElement("div");
            elem.className = "widget rounded-border";
            elem.innerHTML = html;
            root.appendChild(elem);

            break;
        }
    }
}

function updateDashboard(sensors)
{
    // check if dashboard is the active page

    if (!document.getElementById("widgetContainer") && !document.getElementById("widgetContainerOld"))
        return;

    // console.log("updateDashboard");

    var d = new Date();
    document.getElementById("refreshTime").innerHTML = d.toLocaleTimeString();

    if (sensors)
    {
        for (var i = 0; i < sensors.length; i++)
        {
            var sensor = sensors[i];

            if (sensor.widgettype == 0)         // Symbol
            {
                var modeStyle = sensor.options == 3 && sensor.mode == 'manual' ? "background-color: #a27373;" : "";
                $("#widget" + sensor.type + sensor.address).attr("src", sensor.image);
                $("#div" + sensor.type + sensor.address).attr("style", modeStyle);
            }
            else if (sensor.widgettype == 1)    // Gauge
            {
                var elem = $("#widget" + sensor.type + sensor.address);
                var value = sensor.value.toFixed(2);
                var scaleMax = !sensor.scalemax || sensor.unit == '%' ? 100 : sensor.scalemax.toFixed(0);
                var scaleMin = value >= 0 ? "0" : Math.ceil(value / 5) * 5 - 5;

                if (scaleMax < Math.ceil(value))       scaleMax = value;
                if (scaleMax < Math.ceil(sensor.peak)) scaleMax = sensor.peak.toFixed(0);

                $("#sMin" + sensor.type + sensor.address).text(scaleMin);
                $("#sMax" + sensor.type + sensor.address).text(scaleMax);
                $("#value" + sensor.type + sensor.address).text(value + " " + sensor.unit);

                var ratio = (value - scaleMin) / (scaleMax - scaleMin);
                var peak = (sensor.peak.toFixed(2) - scaleMin) / (scaleMax - scaleMin);

                $("#pb" + sensor.type + sensor.address).attr("d", "M 950 500 A 450 450 0 0 0 50 500");
                $("#pv" + sensor.type + sensor.address).attr("d", svg_circle_arc_path(500, 500, 450 /*radius*/, -90, ratio * 180.0 - 90));
                $("#pp" + sensor.type + sensor.address).attr("d", svg_circle_arc_path(500, 500, 450 /*radius*/, peak * 180.0 - 91, peak * 180.0 - 90));

                // console.log(sensor.name + " : " + value + " : "  + ratio + " : " + peak + " : " + scaleMin + " : " + scaleMax);
            }
            else if (sensor.widgettype == 2)      // Text
            {
                $("#widget" + sensor.type + sensor.address).innerHTML = sensor.text;
            }
            else if (sensor.widgettype == 3)      // plain value
            {
                $("#widget" + sensor.type + sensor.address).innerHTML = value + " " + sensor.unit;
            }

            // console.log(i + ": " + sensor.name + " / " + sensor.title);
        }
    }
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

window.storeConfig = function()
{
    console.log("storeSettings");

    var rootConfig = document.getElementById("configContainer");
    var elements = rootConfig.querySelectorAll("[id^='input_']");
    var jsonObj = {};

    for (var e in elements) {
        if (elements[e].tagName == "INPUT") {
            var name = elements[e].id.substring(elements[e].id.indexOf("_") + 1);
            jsonObj[name] = elements[e].value;
        }
    }

    var elements = rootConfig.querySelectorAll("[id^='checkbox_']");

    for (var e in elements) {
        if (elements[e].tagName == "INPUT") {
            var name = elements[e].id.substring(elements[e].id.indexOf("_") + 1);
            jsonObj[name] = (elements[e].checked ? "1" : "0");
        }
    }

    // console.log(JSON.stringify(jsonObj, undefined, 4));

    socket.send({ "event" : "storeconfig", "object" : jsonObj });

    var elem = document.createElement("div");
    elem.innerHTML = "<br/><div class=\"info\"><b><center>Einstellungen gespeichert</center></b></div>";
    document.getElementById("confirm").appendChild(elem);
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

//----------------------------------------------
// on document ready

$(document).ready(function()
{
    // console.log("document ready");
    connectWebSocket();
});
