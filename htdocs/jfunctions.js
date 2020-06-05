
function toggleMode(address)
{
    var divId = "#div" + address;

    $.ajax( {
        url : "interface.php",
        type : "POST",
        data : { "action" : 'togglemode', "addr" : address },
        // dataType : "json",
        success: function(response) {
            var modeStyle = response == 'manual' ? "background-color: #a27373;" : "";

            console.log("togglemode : " + response +  " : " + modeStyle);

            $(divId).attr("style", modeStyle);
        },
        error: function(jqXHR, textStatus, errorThrown) {
            console.log(textStatus, errorThrown);
        }
    });
2}

function toggleIo(address)
{
    var imgId = "#img" + address;
    var titleId = "#title" + address;

    $.ajax( {
        url : "interface.php",
        type : "POST",
        data : { "action" : 'toggleio', "addr" : address },
        // dataType : "json",
        success: function(response) {
            var title = $(titleId).html();
            var value = response;

            console.log(value + " : " + title);

            $.ajax( {
                url : "interface.php",
                type : "POST",
                data : { "action" : 'getImageOf', "title" : title, "value" : value },
                // dataType : "json",
                success: function(response) {
                    console.log(value + " : " + title + " : " + response);
                    $(imgId).attr("src", response);
                },
                error: function(jqXHR, textStatus, errorThrown) {
                    console.log(textStatus, errorThrown);
                }
            });
        },
        error: function(jqXHR, textStatus, errorThrown) {
            console.log(textStatus, errorThrown);
        }
    });

    // data = toJson(response);
}

function toggleIoNext(address)
{
    var imgId = "#img" + address;
    var titleId = "#title" + address;

    $.ajax( {
        url : "interface.php",
        type : "POST",
        data : { "action" : 'toggleioNext', "addr" : address },
        // dataType : "json",
        success: function(response) {
            var title = $(titleId).html();
            var value = response;

            console.log(value + " : " + title);

            $.ajax( {
                url : "interface.php",
                type : "POST",
                data : { "action" : 'getImageOf', "title" : title, "value" : value },
                // dataType : "json",
                success: function(response) {
                    console.log(value + " : " + title + " : " + response);
                    $(imgId).attr("src", response);
                },
                error: function(jqXHR, textStatus, errorThrown) {
                    console.log(textStatus, errorThrown);
                }
            });
        },
        error: function(jqXHR, textStatus, errorThrown) {
            console.log(textStatus, errorThrown);
        }
    });
}

function confirmSubmit(msg)
{
    if (confirm(msg))
        return true;

    return false;
}

function showContent(elm)
{
	 if (document.getElementById(elm).style.display == "block")
    {
        document.getElementById(elm).style.display = "none"
    }
    else
    {
        document.getElementById(elm).style.display = "block"
    }
}

function readonlyContent(elm, chk)
{
    var elm = document.querySelectorAll('[id*=' + elm + ']');  var i;

	 if (chk.checked == 1)
    {
		  for (i = 0; i < elm.length; i++)
        {
			   elm[i].readOnly = false;
			   elm[i].style.backgroundColor = "#fff";
		  }
	 }
    else
    {
		  for (i = 0; i < elm.length; i++)
        {
			   elm[i].readOnly = true;
			   elm[i].style.backgroundColor = "#ddd";
		  }
	 }
}

function disableContent(elm, chk)
{
    var elm = document.querySelectorAll('[id*=' + elm + ']');  var i;

	 if (chk.checked == 1)
    {
		  for (i = 0; i < elm.length; i++)
        {
			   elm[i].disabled = false;
		  }
	 }
    else
    {
		  for (i = 0; i < elm.length; i++)
        {
			   elm[i].disabled = true;
		  }
	 }
}

function showHide(id)
{
    if (document.getElementById)
    {
        var mydiv = document.getElementById(id);
        mydiv.style.display = mydiv.style.display == 'block' ? 'none' : 'block';
    }
}

//----------------------------------------------
// gauge

$(function()
{
    var polar_to_cartesian, svg_circle_arc_path, animate_arc;

    polar_to_cartesian = function(cx, cy, radius, angle) {
        var radians;
        radians = (angle - 90) * Math.PI / 180.0;
        return [Math.round((cx + (radius * Math.cos(radians))) * 100) / 100, Math.round((cy + (radius * Math.sin(radians))) * 100) / 100];
    };

    svg_circle_arc_path = function(x, y, radius, start_angle, end_angle) {
        var end_xy, start_xy;
        start_xy = polar_to_cartesian(x, y, radius, end_angle);
        end_xy = polar_to_cartesian(x, y, radius, start_angle);
        return "M " + start_xy[0] + " " + start_xy[1] + " A " + radius + " " + radius + " 0 0 0 " + end_xy[0] + " " + end_xy[1];
    };

    animate_arc = function(ratio, svg, value, perc, unit, y)
    {
        var arc, center, radius, startx, starty;

        arc = svg.path('');
        center = 500;
        radius = 450;
        startx = 0;
        starty = 450;

        return Snap.animate(0, ratio, (function(val)
        {
            var path;
            arc.remove();
            path = svg_circle_arc_path(500, y, 450, -90, val * 180.0 - 90);
            arc = svg.path(path);
            arc.attr({class: 'data-arc'});
            perc.text(value + " " + unit);
        }), Math.round(2000 * ratio), mina.easeinout);
    };

    draw_peak = function(peak, svg, y)
    {
        var arc, center, radius, startx, starty;

        arc = svg.path('');
        center = 500;
        radius = 450;
        startx = 0;
        starty = 450;

        return Snap.animate(0, peak, (function(val)
        {
            var path;
            arc.remove();
            path = svg_circle_arc_path(500, y, 450, val * 180.0 - 91, val * 180.0 - 90);
            arc = svg.path(path);
            arc.attr({class: 'data-peak'});
        }), Math.round(2000 * peak), mina.easeinout);
    };

    $('.widgetGauge').each(function()
    {
        var ratio, svg, perc, unit, value, peak, y;

        ratio = $(this).data('ratio');
        svg = Snap($(this).find('svg')[0]);
        perc = $(this).find('text._content');
        unit = $(this).data('unit');
        value = $(this).data('value');
        peak = $(this).data('peak');
        y = $(this).data('y');
        animate_arc(ratio, svg, value, perc, unit, y);
        draw_peak(peak, svg, y);
    });
});
