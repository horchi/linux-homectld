/*
 *  activities.js
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  Garmin activities (tab 'Aktivitäten')
 *
 *  event 'activities'       : { activities: [...], newest, configured }
 *  event 'activitytypes'    : { types: [{key, id, parent}] }
 *  event 'activitydetails'  : { id, name, type, location, description, summary: {...}, cached }
 *  event 'activitytrack'    : { id, count, points: [[lat, lon, time, alt, speed], ...], cached }
 */

var activities = null;
var activityTypes = null;
var actPendingTypeChange = null;
var actFilterType = localStorage.getItem(storagePrefix + 'actFilterType') || 'all';
var actFilterPeriod = localStorage.getItem(storagePrefix + 'actFilterPeriod') || 'all';
var actFilterName = localStorage.getItem(storagePrefix + 'actFilterName') || '';
var actCollapsed = JSON.parse(localStorage.getItem(storagePrefix + 'actCollapsed') || '{}');

// German labels of the Garmin type keys, unknown keys are shown 'as is'

var actTypeLabels = {
   'running': 'Laufen', 'trail_running': 'Trailrunning', 'treadmill_running': 'Laufband', 'track_running': 'Bahnlauf',
   'cycling': 'Radfahren', 'road_biking': 'Rennrad', 'mountain_biking': 'Mountainbike', 'gravel_cycling': 'Gravel',
   'indoor_cycling': 'Indoor Cycling', 'e_bike_fitness': 'E-Bike', 'e_bike_mountain': 'E-Mountainbike', 'virtual_ride': 'Virtuelles Rad',
   'walking': 'Gehen', 'hiking': 'Wandern', 'casual_walking': 'Spaziergang', 'speed_walking': 'Walking',
   'swimming': 'Schwimmen', 'lap_swimming': 'Bahnenschwimmen', 'open_water_swimming': 'Freiwasserschwimmen',
   'windsurfing': 'Windsurfen', 'kiteboarding': 'Kitesurfen', 'wingfoiling': 'Wingfoilen', 'sailing': 'Segeln', 'surfing': 'Surfen',
   'stand_up_paddleboarding': 'SUP', 'rowing': 'Rudern', 'indoor_rowing': 'Indoor Rudern', 'kayaking': 'Kajak', 'canoeing': 'Kanu',
   'boating': 'Boot', 'whitewater_rafting_kayaking': 'Wildwasser', 'snorkeling': 'Schnorcheln', 'diving': 'Tauchen',
   'strength_training': 'Krafttraining', 'cardio': 'Cardio', 'hiit': 'HIIT', 'yoga': 'Yoga', 'pilates': 'Pilates', 'breathwork': 'Atemübung',
   'fitness_equipment': 'Fitnessgerät', 'elliptical': 'Crosstrainer', 'stair_climbing': 'Stepper', 'indoor_cardio': 'Indoor Cardio',
   'multi_sport': 'Multisport', 'triathlon': 'Triathlon', 'other': 'Sonstiges', 'motorcycling': 'Motorrad', 'driving_general': 'Auto',
   'resort_skiing_snowboarding': 'Ski / Snowboard', 'backcountry_skiing': 'Skitour', 'cross_country_skiing': 'Langlauf',
   'skate_skiing': 'Skating', 'snowshoeing': 'Schneeschuh', 'inline_skating': 'Inline Skating', 'golf': 'Golf', 'tennis': 'Tennis',
   'padel': 'Padel', 'pickleball': 'Pickleball', 'table_tennis': 'Tischtennis', 'badminton': 'Badminton', 'soccer': 'Fußball',
   'basketball': 'Basketball', 'volleyball': 'Volleyball', 'rock_climbing': 'Klettern', 'bouldering': 'Bouldern', 'mountaineering': 'Bergsteigen',
   'horseback_riding': 'Reiten', 'hunting': 'Jagd', 'fishing': 'Angeln', 'disc_golf': 'Disc Golf', 'archery': 'Bogenschießen'
};

// water sports -> speed in knots, running/walking -> pace, everything else km/h

var actWaterTypes = ['windsurfing', 'kiteboarding', 'wingfoiling', 'sailing', 'surfing', 'stand_up_paddleboarding', 'rowing',
                     'kayaking', 'canoeing', 'boating', 'whitewater_rafting_kayaking', 'open_water_swimming'];
var actPaceTypes = ['running', 'trail_running', 'treadmill_running', 'track_running', 'walking', 'hiking', 'casual_walking', 'speed_walking'];

// Garmin uses keys like 'windsurfing_v2' for newer profiles -> match on the base key

function actBaseType(key)
{
   return (key || 'other').replace(/_v\d+$/, '');
}

function actIsWater(key)
{
   return actWaterTypes.includes(actBaseType(key));
}

function actIsPace(key)
{
   return actPaceTypes.includes(actBaseType(key));
}

function actTypeLabel(key)
{
   let base = actBaseType(key);

   if (actTypeLabels[base])
      return actTypeLabels[base];

   return (key || 'other').replace(/_/g, ' ').replace(/\b\w/g, function(c) { return c.toUpperCase(); });
}

function actHasControlRights()
{
   return (localStorage.getItem(storagePrefix + 'Rights') & 0x02) != 0;
}

function actFmtSpeed(mps, type)
{
   if (!mps)
      return '-';

   if (actIsWater(type))
      return (mps * 1.943844).toLocaleString('de-DE', { minimumFractionDigits: 1, maximumFractionDigits: 1 }) + ' kn';

   if (actIsPace(type)) {
      let secPerKm = 1000 / mps;
      return Math.floor(secPerKm / 60) + ':' + ('0' + Math.round(secPerKm % 60)).slice(-2) + ' /km';
   }

   return (mps * 3.6).toLocaleString('de-DE', { minimumFractionDigits: 1, maximumFractionDigits: 1 }) + ' km/h';
}

function actFmtDate(ts)
{
   return ts ? new Date(ts * 1000).toLocaleString('de-DE', { weekday: 'short', day: '2-digit', month: '2-digit', year: 'numeric', hour: '2-digit', minute: '2-digit' }) : '-';
}

// table cell: one line on wide screens, two lines (short year) on narrow ones - see .actDateLong/.actDateShort

function actFmtDateCell(ts)
{
   if (!ts)
      return '-';

   let d = new Date(ts * 1000);
   let short = d.toLocaleString('de-DE', { weekday: 'short', day: '2-digit', month: '2-digit', year: '2-digit' }).replace(',', '')
             + '<br/>' + d.toLocaleString('de-DE', { hour: '2-digit', minute: '2-digit' });

   return '<span class="actDateLong">' + actFmtDate(ts) + '</span><span class="actDateShort">' + short + '</span>';
}

function actFmtDuration(seconds)
{
   seconds = Math.max(0, Math.round(seconds || 0));
   let h = Math.floor(seconds / 3600);
   let m = Math.floor((seconds % 3600) / 60);
   let s = seconds % 60;

   return (h ? h + ':' : '') + (h ? ('0' + m).slice(-2) : m) + ':' + ('0' + s).slice(-2);
}

//***************************************************************************
// Page
//***************************************************************************

function initActivitiesPage()
{
   $('#dashboardMenu').addClass('hidden');
   showControlContainer();
   $('#container').removeClass('hidden');
   actResizeContainer();
   actBuildControlPanel();
   actRender();

   socket.send({ "event" : "activities", "object" : { "action" : "list" } });
}

// the container is viewport high (like the setup pages), it scrolls vertically and horizontally

function actResizeContainer()
{
   let height = $(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('footer') - sab - 10;

   $('#container').height(height);
   window.onresize = function() { actResizeContainer(); };
}

function actBuildControlPanel()
{
   $("#controlContainer").empty();

   if (actHasControlRights()) {
      $("#controlContainer")
         .append($('<div></div>')
                 .append($('<button></button>')
                         .addClass('rounded-border tool-button')
                         .html('Abgleich mit Garmin')
                         .attr('title', 'Aktivitäten von Garmin Connect holen (fragt nach dem Zeitraum)')
                         .click(function() { actSyncDialog(); })))
         .append($('<div></div>')
                 .addClass('button-group-spacing'));
   }

   // counts per type within the period / name filter (what the list would show)

   let types = {};
   let from = actPeriodStart();
   let matcher = actNameMatcher();

   if (activities)
      for (let a of activities.activities)
         if (actPassesFilter(a, from, matcher))
            types[a.type] = (types[a.type] || 0) + 1;

   let selType = $('<select></select>')
       .attr('id', 'actFilterType')
       .addClass('input rounded-border')
       .css('width', '-webkit-fill-available')
       .css('width', '-moz-available')
       .on('change', function() {
          actFilterType = $(this).val();
          localStorage.setItem(storagePrefix + 'actFilterType', actFilterType);
          actMarkFilters();
          actRender();
       })
       .append($('<option></option>').val('all').html('alle Typen'));

   for (let key of Object.keys(types).sort(function(a, b) { return types[b] - types[a]; }))
      selType.append($('<option></option>').val(key).html(actTypeLabel(key) + ' (' + types[key] + ')'));

   let periods = { 'all': 'gesamter Zeitraum', 'year': 'dieses Jahr', '12m': 'letzte 12 Monate', '6m': 'letzte 6 Monate', '3m': 'letzte 3 Monate', '1m': 'letzter Monat' };
   let selPeriod = $('<select></select>')
       .attr('id', 'actFilterPeriod')
       .addClass('input rounded-border')
       .css('width', '-webkit-fill-available')
       .css('width', '-moz-available')
       .on('change', function() {
          actFilterPeriod = $(this).val();
          localStorage.setItem(storagePrefix + 'actFilterPeriod', actFilterPeriod);
          actBuildControlPanel();      // type counts follow the period
          actRender();
       });

   for (let key in periods)
      selPeriod.append($('<option></option>').val(key).html(periods[key]));

   let inpName = $('<input></input>')
       .attr('id', 'actFilterName')
       .attr('type', 'search')
       .attr('placeholder', 'Name (Regex)')
       .attr('title', 'Filter auf den Namen, regulärer Ausdruck, Groß-/Kleinschreibung egal, z.B. Sitia|Malcesine')
       .addClass('input rounded-border clearableOD')
       .css('width', '-webkit-fill-available')
       .css('width', '-moz-available')
       .val(actFilterName)
       .on('input', function() {
          actFilterName = $(this).val();
          localStorage.setItem(storagePrefix + 'actFilterName', actFilterName);
          actMarkFilters();
          actRender();
       });

   $("#controlContainer")
      .append($('<div></div>').addClass('labelB1').html('Typ'))
      .append(selType)
      .append($('<div></div>').addClass('button-group-spacing'))
      .append($('<div></div>').addClass('labelB1').html('Zeitraum'))
      .append(selPeriod)
      .append($('<div></div>').addClass('button-group-spacing'))
      .append($('<div></div>').addClass('labelB1').html('Name'))
      .append(inpName)
      .append($('<div></div>').addClass('button-group-spacing'));

   selType.val(types[actFilterType] ? actFilterType : 'all');
   selPeriod.val(actFilterPeriod);
   actMarkFilters();

   if (activities) {
      $("#controlContainer")
         .append($('<div></div>')
                 .addClass('labelB1')
                 .html(activities.activities.length + ' Aktivitäten<br/>Stand: ' + (activities.newest ? actFmtDate(activities.newest) : '-')))
         .append($('<div></div>').addClass('button-group-spacing'));
   }

   $("#controlContainer")
      .append($('<button></button>')
              .addClass('rounded-border tool-button')
              .html('Hilfe')
              .attr('title', 'Beschreibung im README')
              .click(function() { showHelp('garmin-activities'); }));
}

// period and name filter (the type filter is applied separately)

function actNameMatcher()
{
   if (!actFilterName)
      return null;

   let re = null;

   try {
      re = new RegExp(actFilterName, 'i');
   }
   catch (e) {
      re = null;      // invalid expression (e.g. while typing) -> plain substring
   }

   let needle = actFilterName.toLowerCase();

   return function(a) {
      return re ? re.test(a.name) || re.test(a.location || '') : (a.name + ' ' + (a.location || '')).toLowerCase().includes(needle);
   };
}

function actPassesFilter(a, from, matcher)
{
   if (from && a.start < from)
      return false;

   return !matcher || matcher(a);
}

// highlight filters which restrict the list

function actMarkFilters()
{
   $('#actFilterType').toggleClass('actFilterActive', actFilterType != 'all');
   $('#actFilterPeriod').toggleClass('actFilterActive', actFilterPeriod != 'all');
   $('#actFilterName').toggleClass('actFilterActive', actFilterName != '');
}

function actPeriodStart()
{
   let now = new Date();

   switch (actFilterPeriod) {
      case 'year': return new Date(now.getFullYear(), 0, 1).getTime() / 1000;
      case '12m':  return now.setMonth(now.getMonth() - 12) / 1000;
      case '6m':   return now.setMonth(now.getMonth() - 6) / 1000;
      case '3m':   return now.setMonth(now.getMonth() - 3) / 1000;
      case '1m':   return now.setMonth(now.getMonth() - 1) / 1000;
   }

   return 0;
}

function processActivities(obj)
{
   activities = obj;

   if (currentPage != 'activities')
      return;

   actBuildControlPanel();
   actRender();
}

//***************************************************************************
// List, grouped by type
//***************************************************************************

function actRender()
{
   let root = document.getElementById("container");

   if (!root)
      return;

   if (!activities) {
      root.innerHTML = '<div class="rounded-border seperatorFold">Aktivitäten werden geladen ...</div>';
      return;
   }

   if (!activities.configured) {
      root.innerHTML = '<div class="rounded-border setupContainer actHint">Kein Garmin Login vorhanden. Einmalig auf der Konsole <code>garmin.py login</code> ausführen (siehe garmin/README.md).</div>';
      return;
   }

   let from = actPeriodStart();
   let matcher = actNameMatcher();
   let groups = {};

   for (let a of activities.activities) {
      if (actFilterType != 'all' && a.type != actFilterType)
         continue;
      if (!actPassesFilter(a, from, matcher))
         continue;

      if (!groups[a.type])
         groups[a.type] = { 'type': a.type, 'items': [], 'duration': 0, 'distance': 0, 'maxspeed': 0 };

      let g = groups[a.type];
      g.items.push(a);
      g.duration += a.moving || a.duration;
      g.distance += a.distance;
      g.maxspeed = Math.max(g.maxspeed, a.maxspeed);
   }

   let keys = Object.keys(groups).sort(function(a, b) { return groups[b].items.length - groups[a].items.length; });

   if (!keys.length) {
      root.innerHTML = '<div class="rounded-border setupContainer actHint">Keine Aktivitäten' + (activities.activities.length ? ' im gewählten Filter.' : '. Mit \'Abgleich mit Garmin\' werden sie geholt.') + '</div>';
      return;
   }

   let control = actHasControlRights();
   let html = '';

   for (let key of keys) {
      let g = groups[key];
      let collapsed = actFilterType == 'all' && actCollapsed[key] == true;   // a selected type is always expanded

      html += '<div class="actGroup">';
      html += ' <div class="rounded-border seperatorFold actGroupHead" onclick="actToggleGroup(\'' + key + '\')">';
      html += '  <span class="actGroupArrow">' + (collapsed ? '▸' : '▾') + '</span> ' + gpsEscape(actTypeLabel(key));
      html += '  <span class="actGroupSum">' + g.items.length + ' · ' + actFmtDuration(g.duration)
            + (g.distance ? ' · ' + gpsFmtDistance(g.distance) : '')
            + (g.maxspeed ? ' · max ' + actFmtSpeed(g.maxspeed, key) : '') + '</span>';
      html += ' </div>';

      if (!collapsed) {
         html += ' <div class="rounded-border actGroupBody">';
         html += ' <table class="tableMultiCol actTable">';
         html += '  <thead><tr>';
         html += '   <td style="width:19%;">Datum</td>';
         html += '   <td style="width:30%;">Name</td>';
         html += '   <td style="width:21%;" class="actColOpt">Ort</td>';
         html += '   <td style="width:10%;" class="actNum">Dauer</td>';
         html += '   <td style="width:10%;" class="actNum">Distanz</td>';
         html += '   <td style="width:10%;" class="actActions"></td>';
         html += '  </tr></thead><tbody>';

         for (let a of g.items) {
            html += '  <tr class="actRow" onclick="actShowDetails(' + a.id + ')">';
            html += '   <td class="actDate">' + actFmtDateCell(a.start) + '</td>';
            html += '   <td class="actWrap">' + gpsEscape(a.name) + '</td>';
            html += '   <td class="actWrap actColOpt">' + gpsEscape(a.location) + '</td>';
            html += '   <td class="actNum">' + actFmtDuration(a.moving || a.duration) + '</td>';
            html += '   <td class="actNum">' + (a.distance ? gpsFmtDistance(a.distance) : '-') + '</td>';
            html += '   <td class="actActions">';

            if (control) {
               html += '    <button class="rounded-border tool-button mdi mdi-lead-pencil" type="button" title="Typ / Name ändern" onclick="event.stopPropagation(); actEdit(' + a.id + ')"></button>';
               html += '    <button class="rounded-border tool-button mdi mdi-delete actDelete" type="button" title="Aktivität bei Garmin löschen" onclick="event.stopPropagation(); actDelete(' + a.id + ')"></button>';
            }

            html += '   </td>';
            html += '  </tr>';
         }

         html += '  </tbody></table>';
         html += ' </div>';
      }

      html += '</div>';
   }

   root.innerHTML = html;
}

function actToggleGroup(key)
{
   actCollapsed[key] = !actCollapsed[key];
   localStorage.setItem(storagePrefix + 'actCollapsed', JSON.stringify(actCollapsed));
   actRender();
}

function actFind(id)
{
   return activities ? activities.activities.find(function(a) { return a.id == id; }) : null;
}

//***************************************************************************
// Sync (asks for the period)
//***************************************************************************

function actSyncDialog()
{
   let newest = activities && activities.newest ? new Date(activities.newest * 1000) : null;
   let defDate = newest ? new Date(newest.getTime() - 24 * 3600 * 1000) : new Date(new Date().getFullYear(), 0, 1);
   let iso = defDate.toISOString().substring(0, 10);

   let form = '<div class="dialog-content actSyncDialog">' +
       ' <div><label><input type="radio" name="actSyncMode" value="since" checked> seit ' +
       (newest ? 'der letzten Aktivität (' + iso + ')' : 'Datum') + '</label></div>' +
       ' <div><label><input type="radio" name="actSyncMode" value="date"> seit Datum</label> ' +
       '  <input type="date" id="actSyncDate" class="rounded-border input" value="' + iso + '"></div>' +
       ' <div><label><input type="radio" name="actSyncMode" value="all"> alle Aktivitäten (kompletter Neuabgleich, bei Garmin gelöschte werden entfernt)</label></div>' +
       ' <div class="actSyncHint">Datenvolumen: etwa 3-4 kB je Aktivität, Tracks werden nicht geladen.<br/>' +
       'Bei Garmin geänderte Aktivitäten werden übernommen, ihre Details beim nächsten Öffnen neu geholt.</div>' +
       '</div>';

   $(form).dialog({
      modal: true,
      width: 'auto',
      title: 'Abgleich mit Garmin',
      buttons: {
         'Abbrechen': function() { $(this).dialog('close'); },
         'Abgleich starten': function() {
            let mode = $('input[name=actSyncMode]:checked').val();
            let since = mode == 'all' ? '' : mode == 'date' ? $('#actSyncDate').val() : iso;
            $(this).dialog('close');
            pingTimeoutMs = 600000;
            showProgressDialog();
            socket.send({ "event" : "activities", "object" : { "action" : "sync", "since" : since } });
         }
      },
      close: function() { $(this).dialog('destroy').remove(); }
   });
}

//***************************************************************************
// Edit (type and name in one dialog)
//***************************************************************************

function actEdit(id)
{
   if (!activityTypes) {
      actPendingTypeChange = id;
      showProgressDialog();
      socket.send({ "event" : "activities", "object" : { "action" : "types" } });
      return;
   }

   let a = actFind(id);

   if (!a)
      return;

   let sel = $('<select></select>').attr('id', 'actEditType').addClass('rounded-border input').css('width', '100%');
   let types = activityTypes.types.slice().sort(function(x, y) { return actTypeLabel(x.key).localeCompare(actTypeLabel(y.key), 'de'); });

   for (let t of types)
      sel.append($('<option></option>').val(t.key).html(actTypeLabel(t.key) + (actTypeLabels[t.key] ? '' : ' (' + t.key + ')')).prop('selected', t.key == a.type));

   let form = $('<div></div>').addClass('dialog-content actEditDialog')
       .append($('<div></div>').addClass('actDetailsHead').html(actFmtDate(a.start) + (a.location ? ' · ' + gpsEscape(a.location) : '')))
       .append($('<div></div>').addClass('labelB1').html('Name'))
       .append($('<input></input>').attr('id', 'actEditName').attr('type', 'text').attr('maxlength', 200).addClass('rounded-border input').css('width', '100%').val(a.name))
       .append($('<div></div>').addClass('labelB1').html('Typ'))
       .append(sel);

   form.dialog({
      modal: true,
      width: 'auto',
      title: 'Aktivität ändern',
      buttons: {
         'Abbrechen': function() { $(this).dialog('close'); },
         'Speichern': function() {
            let type = $('#actEditType').val();
            let name = $('#actEditName').val().trim();
            $(this).dialog('close');
            if (type == a.type && (name == a.name || name == ''))
               return;
            showProgressDialog();
            socket.send({ "event" : "activities", "object" : { "action" : "edit", "id" : id, "type" : type, "name" : name } });
         }
      },
      open: function() { $('#actEditName').focus(); },
      close: function() { $(this).dialog('destroy').remove(); }
   });
}

function actDelete(id)
{
   let a = actFind(id);

   if (!a)
      return;

   confirmDialog(function() {
      showProgressDialog();
      socket.send({ "event" : "activities", "object" : { "action" : "delete", "id" : id } });
   }, 'Aktivität <b>' + gpsEscape(a.name) + '</b> vom ' + actFmtDate(a.start) + ' bei Garmin löschen?<br/>Das kann nicht rückgängig gemacht werden.', 'Löschen');
}

function processActivityTypes(obj)
{
   activityTypes = obj;
   hideProgressDialog();

   if (actPendingTypeChange) {
      let id = actPendingTypeChange;
      actPendingTypeChange = null;
      actEdit(id);
   }
}

//***************************************************************************
// Details (all values Garmin provides for the activity)
//***************************************************************************

var actSummaryLabels = {
   'distance':               ['Distanz', 'dist'],
   'duration':               ['Dauer', 'time'],
   'movingDuration':         ['Dauer in Bewegung', 'time'],
   'elapsedDuration':        ['Gesamtzeit', 'time'],
   'averageSpeed':           ['Ø Geschwindigkeit', 'speed'],
   'averageMovingSpeed':     ['Ø Geschwindigkeit in Bewegung', 'speed'],
   'maxSpeed':               ['Max. Geschwindigkeit', 'speed'],
   'averageHR':              ['Ø Puls', 'bpm'],
   'maxHR':                  ['Max. Puls', 'bpm'],
   'minHR':                  ['Min. Puls', 'bpm'],
   'calories':               ['Kalorien', 'kcal'],
   'bmrCalories':            ['Grundumsatz', 'kcal'],
   'elevationGain':          ['Anstieg', 'm'],
   'elevationLoss':          ['Abstieg', 'm'],
   'minElevation':           ['Min. Höhe', 'm'],
   'maxElevation':           ['Max. Höhe', 'm'],
   'averageTemperature':     ['Ø Temperatur', '°C'],
   'minTemperature':         ['Min. Temperatur', '°C'],
   'maxTemperature':         ['Max. Temperatur', '°C'],
   'steps':                  ['Schritte', ''],
   'averageRunCadence':      ['Ø Schrittfrequenz', 'spm'],
   'maxRunCadence':          ['Max. Schrittfrequenz', 'spm'],
   'averageBikeCadence':     ['Ø Trittfrequenz', 'rpm'],
   'maxBikeCadence':         ['Max. Trittfrequenz', 'rpm'],
   'averagePower':           ['Ø Leistung', 'W'],
   'maxPower':               ['Max. Leistung', 'W'],
   'normalizedPower':        ['Normalisierte Leistung', 'W'],
   'trainingEffect':         ['Aerober Trainingseffekt', ''],
   'anaerobicTrainingEffect':['Anaerober Trainingseffekt', ''],
   'trainingEffectLabel':    ['Primärer Nutzen', 'enum'],
   'aerobicTrainingEffectMessage':   ['Aerober Effekt', 'enum'],
   'anaerobicTrainingEffectMessage': ['Anaerober Effekt', 'enum'],
   'activityTrainingLoad':   ['Trainingsbelastung', ''],
   'vO2MaxValue':            ['VO2max', ''],
   'averageStrokeDistance':  ['Ø Strecke je Schlag', 'm'],
   'averageStrokes':         ['Ø Schläge', ''],
   'strokes':                ['Schläge', ''],
   'averageSwolf':           ['Ø SWOLF', ''],
   'moderateIntensityMinutes': ['Minuten moderate Intensität', 'min'],
   'vigorousIntensityMinutes': ['Minuten hohe Intensität', 'min'],
   'waterEstimated':         ['Flüssigkeitsverlust (geschätzt)', 'ml'],
   'startLatitude':          ['Start Breite', 'coord'],
   'startLongitude':         ['Start Länge', 'coord'],
   'endLatitude':            ['Ende Breite', 'coord'],
   'endLongitude':           ['Ende Länge', 'coord'],
   'startTimeLocal':         ['Start', 'text'],
   'endTimeLocal':           ['Ende', 'text']
};

// Garmin enum codes (training effect), the trailing digit is the level 0..5 shown on the watch

var actEnumLabels = {
   'UNKNOWN':                             'nicht bewertet',
   'RECOVERY':                            'Erholung',
   'AEROBIC_BASE':                        'aerobe Basis',
   'TEMPO':                               'Tempo',
   'THRESHOLD':                           'Schwelle',
   'LACTATE_THRESHOLD':                   'Laktatschwelle',
   'VO2MAX':                              'VO2max',
   'ANAEROBIC_CAPACITY':                  'anaerobe Kapazität',
   'SPRINT':                              'Sprint',
   'SPEED':                               'Geschwindigkeit',
   'NO_AEROBIC_BENEFIT':                  'kein aerober Nutzen',
   'MINOR_AEROBIC_BENEFIT':               'geringer aerober Nutzen',
   'MAINTAINING_AEROBIC_FITNESS':         'aerobe Fitness erhalten',
   'IMPROVING_AEROBIC_FITNESS':           'aerobe Fitness verbessert',
   'IMPROVING_AEROBIC_BASE':              'aerobe Basis verbessert',
   'HIGHLY_IMPROVING_AEROBIC_BASE':       'aerobe Basis stark verbessert',
   'IMPROVING_TEMPO':                     'Tempo verbessert',
   'HIGHLY_IMPROVING_TEMPO':              'Tempo stark verbessert',
   'IMPROVING_LACTATE_THRESHOLD':         'Laktatschwelle verbessert',
   'HIGHLY_IMPROVING_LACTATE_THRESHOLD':  'Laktatschwelle stark verbessert',
   'IMPROVING_VO2_MAX':                   'VO2max verbessert',
   'HIGHLY_IMPROVING_VO2_MAX':            'VO2max stark verbessert',
   'OVERREACHING':                        'Überbelastung',
   'NO_ANAEROBIC_BENEFIT':                'kein anaerober Nutzen',
   'MINOR_ANAEROBIC_BENEFIT':             'geringer anaerober Nutzen',
   'MAINTAINING_ANAEROBIC_FITNESS':       'anaerobe Fitness erhalten',
   'IMPROVING_ANAEROBIC_CAPACITY':        'anaerobe Kapazität verbessert',
   'HIGHLY_IMPROVING_ANAEROBIC_CAPACITY': 'anaerobe Kapazität stark verbessert',
   'IMPROVING_ECONOMY_AND_SPEED':         'Ökonomie und Geschwindigkeit verbessert',
   'HIGHLY_IMPROVING_ECONOMY_AND_SPEED':  'Ökonomie und Geschwindigkeit stark verbessert',
   'IMPROVING_ANAEROBIC_BASE':            'anaerobe Basis verbessert',
   'HIGHLY_IMPROVING_ANAEROBIC_BASE':     'anaerobe Basis stark verbessert'
};

function actFmtEnum(value)
{
   let m = String(value).match(/^([A-Z0-9_]+?)(?:_(\d))?$/);

   if (!m)
      return gpsEscape(String(value));

   let label = actEnumLabels[m[1]] || m[1].toLowerCase().replace(/_/g, ' ');

   return gpsEscape(label) + (m[2] != null ? ' <span class="actSyncHint">(Stufe ' + m[2] + ')</span>' : '');
}

function actFmtSummary(key, value, type)
{
   let def = actSummaryLabels[key];
   let fmt = def ? def[1] : '';

   if (value == null)
      return '-';

   switch (fmt) {
      case 'dist':  return gpsFmtDistance(value);
      case 'time':  return actFmtDuration(value);
      case 'speed': return actFmtSpeed(value, type) + (actIsWater(type) ? ' (' + (value * 3.6).toFixed(1) + ' km/h)' : '');
      case 'coord': return value.toFixed(5);
      case 'text':  return gpsEscape(String(value));
      case 'enum':  return actFmtEnum(value);
   }

   // unknown keys with enum-like values (UPPER_CASE) -> translate too

   if (typeof value == 'string' && /^[A-Z][A-Z0-9_]+$/.test(value))
      return actFmtEnum(value);

   if (typeof value == 'number')
      return (Number.isInteger(value) ? value : value.toFixed(1)) + (fmt ? ' ' + fmt : '');

   return gpsEscape(String(value));
}

function actShowDetails(id, force = false)
{
   showProgressDialog();
   socket.send({ "event" : "activities", "object" : { "action" : "details", "id" : id, "force" : force } });
}

function showActivityDetails(obj)
{
   hideProgressDialog();

   let a = actFind(obj.id) || {};
   let type = obj.type || a.type;
   let summary = obj.summary || {};
   let html = '<div class="dialog-content actDetails">';

   html += '<div class="actDetailsHead">' + gpsEscape(obj.name) + '<br/>' + actTypeLabel(type) + ' · ' + actFmtDate(a.start) + (obj.location ? ' · ' + gpsEscape(obj.location) : '')
        + '<br/><span class="actSyncHint">Garmin ID ' + obj.id + '</span></div>';

   if (obj.description)
      html += '<div class="actDetailsDesc">' + gpsEscape(obj.description) + '</div>';

   // known values first (in the order above), then the rest - two key/value pairs per row

   let rows = [];
   let shown = {};

   // no training effect assessed (e.g. water sports) -> hide the three effect rows

   let noEffect = summary.trainingEffectLabel == 'UNKNOWN';
   let effectKeys = ['trainingEffectLabel', 'aerobicTrainingEffectMessage', 'anaerobicTrainingEffectMessage', 'trainingEffect', 'anaerobicTrainingEffect'];

   for (let key in actSummaryLabels) {
      if (summary[key] == null || key.startsWith('start') || key.startsWith('end'))
         continue;
      if (noEffect && effectKeys.includes(key)) {
         shown[key] = true;
         continue;
      }
      rows.push([actSummaryLabels[key][0], actFmtSummary(key, summary[key], type)]);
      shown[key] = true;
   }

   for (let key of Object.keys(summary).sort()) {
      if (shown[key] || summary[key] == null || typeof summary[key] == 'object')
         continue;
      if (key.startsWith('start') || key.startsWith('end') || key.endsWith('GMT'))
         continue;
      rows.push([gpsEscape(key), actFmtSummary(key, summary[key], type)]);
   }

   let half = Math.ceil(rows.length / 2);

   html += '<table class="actDetailsTable">';

   for (let i = 0; i < half; i++) {
      let l = rows[i];
      let r = rows[i + half];
      html += '<tr><td>' + l[0] + '</td><td class="actNum">' + l[1] + '</td>';
      html += r ? '<td class="actDetailsCol2">' + r[0] + '</td><td class="actNum">' + r[1] + '</td>' : '<td></td><td></td>';
      html += '</tr>';
   }

   html += '</table>';
   html += '<div class="actSyncHint">' + (obj.cached ? 'aus dem Zwischenspeicher' : 'von Garmin geladen') + '</div>';
   html += '</div>';

   let buttons = {};

   if (a.lat || a.lon)
      buttons['Track'] = function() { $(this).dialog('close'); actShowTrack(obj.id); };

   buttons['Neu laden'] = function() { $(this).dialog('close'); actShowDetails(obj.id, true); };
   buttons['Ok'] = function() { $(this).dialog('close'); };

   $(html).dialog({
      modal: true,
      width: Math.min(980, Math.round($(window).width() * 0.92)),
      maxHeight: $(window).height() * 0.9,
      title: 'Aktivität',
      buttons: buttons,
      close: function() { $(this).dialog('destroy').remove(); }
   });
}

//***************************************************************************
// Track on a map (Leaflet), colored by speed
//***************************************************************************

var actTrackMap = null;

function actShowTrack(id, force = false)
{
   showProgressDialog();
   socket.send({ "event" : "activities", "object" : { "action" : "track", "id" : id, "force" : force } });
}

// blue (slow) -> green -> yellow -> red (fast)

function actSpeedColor(ratio)
{
   ratio = Math.max(0, Math.min(1, ratio));
   let stops = [[0, [30, 100, 220]], [0.4, [40, 180, 60]], [0.7, [240, 200, 30]], [1, [220, 40, 40]]];

   for (let i = 1; i < stops.length; i++) {
      if (ratio <= stops[i][0]) {
         let f = (ratio - stops[i-1][0]) / (stops[i][0] - stops[i-1][0]);
         let c = stops[i-1][1].map(function(v, k) { return Math.round(v + (stops[i][1][k] - v) * f); });
         return 'rgb(' + c.join(',') + ')';
      }
   }

   return 'rgb(220,40,40)';
}

function actHaversine(a, b)
{
   let R = 6371000;
   let dLat = (b[0] - a[0]) * Math.PI / 180;
   let dLon = (b[1] - a[1]) * Math.PI / 180;
   let h = Math.sin(dLat / 2) ** 2 + Math.cos(a[0] * Math.PI / 180) * Math.cos(b[0] * Math.PI / 180) * Math.sin(dLon / 2) ** 2;

   return 2 * R * Math.asin(Math.sqrt(h));
}

function showActivityTrack(obj)
{
   hideProgressDialog();

   let a = actFind(obj.id) || {};
   let points = obj.points || [];

   if (points.length < 2) {
      showInfoDialog({ 'status': -1, 'message': 'Kein Track für diese Aktivität' });
      return;
   }

   // speed per segment: from Garmin if present, otherwise distance / time

   let speeds = [];
   let maxSpeed = 0;

   for (let i = 1; i < points.length; i++) {
      let s = points[i][4];

      if (!s) {
         let dt = points[i][2] - points[i-1][2];
         s = dt > 0 ? actHaversine(points[i-1], points[i]) / dt : 0;
      }

      speeds.push(s);
   }

   let sorted = speeds.slice().sort(function(x, y) { return x - y; });
   maxSpeed = sorted.length ? sorted[Math.floor((sorted.length - 1) * 0.98)] : 0;   // color scale, robust against GPS jumps
   let shownMax = a.maxspeed || maxSpeed;                                            // header: Garmin's value if known

   let w = Math.round($(window).width() * 0.85);
   let h = Math.round($(window).height() * 0.75);
   let html = '<div class="actTrackDialog">' +
       '<div class="actTrackHead">' + gpsEscape(a.name || '') + ' · ' + actTypeLabel(a.type) + ' · ' + actFmtDate(a.start) +
       ' · ' + points.length + ' Punkte · max ' + actFmtSpeed(shownMax, a.type) +
       ' <span class="actTrackLegend"><span style="background:' + actSpeedColor(0) + '"></span><span style="background:' + actSpeedColor(0.4) + '"></span>' +
       '<span style="background:' + actSpeedColor(0.7) + '"></span><span style="background:' + actSpeedColor(1) + '"></span> langsam → schnell</span>' +
       '<span class="actSyncHint"> (' + (obj.cached ? 'aus dem Zwischenspeicher' : 'von Garmin geladen') + ')</span></div>' +
       '<div id="actTrackMap" style="width:' + w + 'px;height:' + h + 'px;"></div>' +
       '</div>';

   let buttons = {};
   buttons['Neu laden'] = function() { $(this).dialog('close'); actShowTrack(obj.id, true); };
   buttons['Ok'] = function() { $(this).dialog('close'); };

   $(html).dialog({
      modal: true,
      width: 'auto',
      title: 'Track',
      buttons: buttons,
      open: function() {
         // create the map after the dialog is laid out, Leaflet needs the real container size

         setTimeout(function() {
            let container = document.getElementById('actTrackMap');

            if (!container)
               return;

            actTrackMap = L.map(container, { attributionControl: false, fadeAnimation: false, preferCanvas: true });

            if (navigator.onLine)
               L.tileLayer('https://tile.openstreetmap.de/{z}/{x}/{y}.png', { attribution: '© OpenStreetMap', maxZoom: 19 }).addTo(actTrackMap);

            let latlngs = points.map(function(p) { return L.latLng(p[0], p[1]); });
            let bounds = L.latLngBounds(latlngs);

            actTrackMap.fitBounds(bounds, { padding: [20, 20], maxZoom: 17 });

            // one polyline per segment, colored by its speed

            for (let i = 1; i < latlngs.length; i++)
               L.polyline([latlngs[i-1], latlngs[i]], { color: actSpeedColor(maxSpeed ? speeds[i-1] / maxSpeed : 0), weight: 4, opacity: 0.9 }).addTo(actTrackMap);

            L.circleMarker(latlngs[0], { radius: 6, color: 'white', fillColor: 'green', fillOpacity: 1 }).addTo(actTrackMap).bindTooltip('Start');
            L.circleMarker(latlngs[latlngs.length-1], { radius: 6, color: 'white', fillColor: 'black', fillOpacity: 1 }).addTo(actTrackMap).bindTooltip('Ende');

            console.log('track', points.length, 'points, bounds', bounds.toBBoxString(), 'size', actTrackMap.getSize().toString(), 'zoom', actTrackMap.getZoom());
         }, 50);
      },
      close: function() {
         if (actTrackMap) {
            actTrackMap.remove();
            actTrackMap = null;
         }
         $(this).dialog('destroy').remove();
      }
   });
}
