/*
 *  ph.js
 *
 *  (c) 2020 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

function requestActualPh()
{
   socket.send({ "event" : "ph", "object" : {} });
}

function updatePhCal(data)
{
   if (document.getElementById("inputCalMeasureValue1").value == "") {
      document.getElementById("inputCalMeasureValue1").value = data.calValue;
      document.getElementById("currentCalMeasureText1").innerHTML = "&nbsp;&nbsp;&nbsp;(Durchschnitt über&nbsp;"
         + data.duration + "&nbsp;Sekunden)";
   }
   else {
      document.getElementById("inputCalMeasureValue2").value = data.calValue;
      document.getElementById("currentCalMeasureText2").innerHTML = "&nbsp;&nbsp;&nbsp;(Durchschnitt über&nbsp;"
         + data.duration + "&nbsp;Sekunden)";

      document.getElementById("buttonStoreCal").style.visibility = 'visible';
   }
}

function updatePhActual(data, rootPhActual)
{
   var phShowCal = document.getElementById("phShowCal");
   var currentPh = rootPhActual.querySelector("#currentPh");

   phShowCal.querySelector("#buttonStoreCal").style.visibility = 'hidden';

   if (data.currentPh == "--")
      currentPh.innerHTML = data.currentPh;
   else {
      currentPh.innerHTML = data.currentPh.toFixed(2);
      currentPh.style.color = "yellow";
      hh = setInterval(resetFont, 400);
      rootPhActual.querySelector("#checkboxRefresh").checked = true;

      function resetFont() {
         currentPh.style.color = "white";
         clearInterval(hh);
      }
   }

   rootPhActual.querySelector("#currentPhValue").innerHTML = data.currentPhValue;

   if (data.currentPhA != null) {
      rootPhActual.querySelector("#currentPhA").innerHTML = data.currentPhA.toFixed(2);
      rootPhActual.querySelector("#currentPhB").innerHTML = data.currentPhB.toFixed(2);
      rootPhActual.querySelector("#currentCalA").innerHTML = data.currentCalA;
      rootPhActual.querySelector("#currentCalB").innerHTML = data.currentCalB;

      phShowCal.querySelector("#inputCalPh1").value = data.currentPhA.toFixed(2);
      phShowCal.querySelector("#inputCalPh2").value = data.currentPhB.toFixed(2);
   }

   if (phCalTimerhandle == null)
      phCalTimerhandle = setInterval(requestActualPh, 3000);
}

window.checkboxRefreshKlick = function()
{
   if (!phCalTimerhandle && document.getElementById("checkboxRefresh").checked) {
      phCalTimerhandle = setInterval(requestActualPh, 3000);
   }
   else if (phCalTimerhandle && !document.getElementById("checkboxRefresh").checked) {
      clearInterval(phCalTimerhandle);
      phCalTimerhandle = null;
   }
}

window.doPhCal = function()
{
   clearInterval(phCalTimerhandle);
   phCalTimerhandle = null;
   document.getElementById("checkboxRefresh").checked = false;
   var duration = document.getElementById("inputCalDuration").value;
   socket.send({ "event" : "phcal", "object" : { "duration" : parseInt(duration)} });

   if (document.getElementById("inputCalMeasureValue2").value != "") {
      document.getElementById("inputCalMeasureValue1").value = "";
      document.getElementById("currentCalMeasureText1").innerHTML = "";
      document.getElementById("inputCalMeasureValue2").value = "";
      document.getElementById("currentCalMeasureText2").innerHTML = "";
   }
}

window.storePhCal = function()
{
   var cal1 = parseInt(document.getElementById("inputCalMeasureValue1").value);
   var ph1 = parseFloat(document.getElementById("inputCalPh1").value);
   var cal2 = parseInt(document.getElementById("inputCalMeasureValue2").value);
   var ph2 = parseFloat(document.getElementById("inputCalPh2").value);

   var message = "Kalibrierwert '" + cal1 + "' für PH " + ph1 + "</br>" +
       "Kalibrierwert '" + cal2 + "' für PH " + ph2;

   dialog.confirm({ title: "Kalibrierwerte speichern?",
                    message: message,
	                 cancel: "Abbrechen",
	                 button: "Speichern",
                    callback: function(value) {
                       if (value) {
                          console.log("storing calibration values");
                          socket.send({ "event" : "phsetcal", "object" :
                                        { "currentPhA"  : ph1,
                                          "currentCalA" : cal1,
                                          "currentPhB"  : ph2,
                                          "currentCalB" : cal2 }
                                      });
                       }
                    }
	               });
}
