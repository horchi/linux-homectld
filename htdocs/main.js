
import WebSocketClient from "./websocket.js"
var isActive = null;
var socket = null;

function connectWebSocket()
{
    socket = new WebSocketClient({
        url: "ws://" + location.hostname + ":61109",
        protocol: "pool",
        autoReconnectInterval: 5000,
        onopen: () => {
            console.log("socket opened :)");
            if (isActive === null)     // wurde beim Schliessen auf null gesetzt
            {
                console.log("send login");
                socket.send({ "event": "login", "object": { "type": "foo" } });
            }
        }, onclose: () => {
            isActive = null;           // auf null setzten, dass ein neues login aufgerufen wird
        }, onmessage: (msg) => {
            updateDashboard(msg.data);
        }
    });

    if (!socket)
        return !($el.innerHTML = "Your Browser will not support Websockets!");
}

function updateDashboard(inData)
{
    // check if dashboard is the active page

    if (!document.getElementById("widgetContainer"))
        return;

    var d = new Date();
    document.getElementById("demo").innerHTML = d.toLocaleTimeString();

    if (inData != "")
    {
        var message = JSON.parse(inData);
        var event = message.event;
        var sensors = message.object;

        console.log("got event: " + event);

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
                var scaleMax = sensor.unit == '%' ? 100 : sensor.scalemax;
                var scaleMin = value >= 0 ? "0" : Math.ceil(value / 5) * 5 - 5;

                if (scaleMax < value) scaleMax = value;
                if (scaleMax < sensor.peak) scaleMax = sensor.peak;

                var ratio = (value - scaleMin) / (scaleMax - scaleMin);
                var peak = (sensor.peak - scaleMin) / (scaleMax - scaleMin);

                elem.attr("data-ratio", ratio);
                elem.attr("data-value", value);
                elem.attr("data-unit", sensor.unit);
                elem.attr("data-peak", peak);

                var svg = Snap(elem.find('svg')[0]);
                var perc = elem.find('text._content');
                var y = elem.data('y');

                animate_arc(ratio, svg, value, perc, sensor.unit, y);
                draw_peak(peak, svg, y);

                // console.log(sensor.name + " : " + value + " : "  + ratio + " : " + peak + " : " + scaleMin + " : " + scaleMax);
            }
            else if (sensor.widgettype == 2)      // Text
            {
                $("#widget" + sensor.type + sensor.address).innerHTML = sensor.text;
            }

            console.log(i + ": " + sensor.name + " / " + sensor.title);
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

function animate_arc(ratio, svg, value, perc, unit, y)
{
    return Snap.animate(0, ratio, (function(val) {
        svg.path('').remove();
        var path = svg_circle_arc_path(500, y, 450 /*radius*/, -90, val * 180.0 - 90);
        svg.path(path).attr({class: 'data-arc'});
        perc.text(value + " " + unit);
    }), Math.round(2000 * ratio), mina.easeinout);
};

function draw_peak(peak, svg, y)
{
    svg.path('').remove();
    var path = svg_circle_arc_path(500, y, 450 /*radius*/, peak * 180.0 - 91, peak * 180.0 - 90);
    svg.path(path).attr({class: 'data-peak'});
};

//----------------------------------------------
// on document ready

$(document).ready(function()
{
    connectWebSocket();

    //----------------------------------------------
    // gauge stuff

    $('.widgetGauge').each(function() {
        var ratio = $(this).data('ratio');
        var svg = Snap($(this).find('svg')[0]);
        var perc = $(this).find('text._content');
        var unit = $(this).data('unit');
        var value = $(this).data('value');
        var peak = $(this).data('peak');
        var y = $(this).data('y');

        animate_arc(ratio, svg, value, perc, unit, y);
        draw_peak(peak, svg, y);
    });
});
