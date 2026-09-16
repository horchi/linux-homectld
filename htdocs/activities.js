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
var actFilterLocation = localStorage.getItem(storagePrefix + 'actFilterLocation') || '';
var actSort = JSON.parse(localStorage.getItem(storagePrefix + 'actSort') || '{"key":"start","dir":"desc"}');   // column sort of the lists
var actCollapsed = JSON.parse(localStorage.getItem(storagePrefix + 'actCollapsed') || '{}');
var actView = localStorage.getItem(storagePrefix + 'actView') || 'list';   // 'list' | 'map'
var actSelected = {};                                                       // ids of the selected activities (bulk edit)
var actLastClicked = null;                                                  // last clicked check box (range selection with shift / ctrl)

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

   let kmh = (mps * 3.6).toLocaleString('de-DE', { minimumFractionDigits: 1, maximumFractionDigits: 1 }) + ' km/h';

   if (actIsWater(type))          // knots, km/h in brackets - everywhere the speed is shown
      return (mps * 1.943844).toLocaleString('de-DE', { minimumFractionDigits: 1, maximumFractionDigits: 1 }) + ' kn (' + kmh + ')';

   if (actIsPace(type)) {
      let secPerKm = 1000 / mps;
      return Math.floor(secPerKm / 60) + ':' + ('0' + Math.round(secPerKm % 60)).slice(-2) + ' /km';
   }

   return kmh;
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

   // list <-> world map

   $("#controlContainer")
      .append($('<div></div>')
              .append($('<button></button>')
                      .addClass('rounded-border tool-button mdi ' + (actView == 'map' ? 'mdi-format-list-bulleted' : 'mdi-map'))
                      .html(actView == 'map' ? ' Liste' : ' Karte')
                      .attr('title', actView == 'map' ? 'zurück zur Liste' : 'alle Aktivitäten als Punkte auf einer Weltkarte, gefärbt nach Typ')
                      .click(function() { actSetView(actView == 'map' ? 'list' : 'map'); })))
      .append($('<div></div>')
              .addClass('button-group-spacing'));

   let locations = {};              // suggestions for the location filter (within the period)
   let from = actPeriodStart();

   if (activities)
      for (let a of activities.activities)
         if (a.location && (!from || a.start >= from))
            locations[a.location] = (locations[a.location] || 0) + 1;

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
       });

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
          actFillTypeOptions();
          actMarkFilters();
          actRender();
       });

   let inpLocation = $('<input></input>')
       .attr('id', 'actFilterLocation')
       .attr('type', 'search')
       .attr('list', 'actLocationList')
       .attr('placeholder', 'Ort (Regex)')
       .attr('title', 'Filter auf den Ort, regulärer Ausdruck, Groß-/Kleinschreibung egal, z.B. ^Malcesine|Sitia')
       .addClass('input rounded-border clearableOD')
       .css('width', '-webkit-fill-available')
       .css('width', '-moz-available')
       .val(actFilterLocation)
       .on('input', function() {
          actFilterLocation = $(this).val();
          localStorage.setItem(storagePrefix + 'actFilterLocation', actFilterLocation);
          actFillTypeOptions();
          actMarkFilters();
          actRender();
       });

   let locationList = $('<datalist></datalist>').attr('id', 'actLocationList');

   for (let loc of Object.keys(locations).sort(function(a, b) { return locations[b] - locations[a] || a.localeCompare(b, 'de'); }))
      locationList.append($('<option></option>').val(loc));

   $("#controlContainer")
      .append($('<div></div>').addClass('labelB1').html('Typ'))
      .append(selType)
      .append($('<div></div>').addClass('button-group-spacing'))
      .append($('<div></div>').addClass('labelB1').html('Zeitraum'))
      .append(selPeriod)
      .append($('<div></div>').addClass('button-group-spacing'))
      .append($('<div></div>').addClass('labelB1').html('Name'))
      .append(inpName)
      .append($('<div></div>').addClass('button-group-spacing'))
      .append($('<div></div>').addClass('labelB1').html('Ort'))
      .append(inpLocation)
      .append(locationList)
      .append($('<div></div>').addClass('button-group-spacing'));

   actFillTypeOptions();
   selPeriod.val(actFilterPeriod);
   actMarkFilters();

   if (activities) {
      $("#controlContainer")
         .append($('<div></div>')
                 .addClass('labelB1')
                 .html(activities.activities.length + ' Aktivitäten<br/>Stand: ' + (activities.newest ? actFmtDate(activities.newest) : '-')))
         .append($('<div></div>').addClass('button-group-spacing'));
   }

   // bulk edit of the selected activities (check boxes in the list)

   if (actHasControlRights() && actView == 'list') {
      $("#controlContainer")
         .append($('<div></div>').attr('id', 'actSelInfo').addClass('labelB1'))
         .append($('<div></div>')
                 .append($('<button></button>')
                         .attr('id', 'actBtnEditSel')
                         .addClass('rounded-border tool-button')
                         .html('Auswahl bearbeiten')
                         .attr('title', 'Typ, Name und/oder Ort der gewählten Aktivitäten ändern')
                         .click(function() { actEditDialog(Object.keys(actSelected).map(Number)); }))
                 .append($('<button></button>')
                         .attr('id', 'actBtnClearSel')
                         .addClass('rounded-border tool-button')
                         .html('Auswahl aufheben')
                         .click(function() { actClearSelection(); })))
         .append($('<div></div>').addClass('button-group-spacing'));

      actUpdateSelection();
   }

   $("#controlContainer")
      .append($('<button></button>')
              .addClass('rounded-border tool-button')
              .html('Hilfe')
              .attr('title', 'Beschreibung im README')
              .click(function() { showHelp('garmin-activities'); }));
}

// options of the type select: the types (with counts) within period, name and location filter;
//   called by the filter inputs, too (the options followed a filter only with the next list push
//   and stayed restricted after the filter was cleared)

function actFillTypeOptions()
{
   let sel = $('#actFilterType');

   if (!sel.length)
      return;

   let types = {};
   let from = actPeriodStart();
   let matcher = actTextMatcher();

   if (activities)
      for (let a of activities.activities)
         if (actPassesFilter(a, from, matcher))
            types[a.type] = (types[a.type] || 0) + 1;

   if (actFilterType != 'all' && !types[actFilterType])
      types[actFilterType] = 0;                 // the selected type stays visible (the list is empty then)

   sel.empty().append($('<option></option>').val('all').html('alle Typen'));

   for (let key of Object.keys(types).sort(function(a, b) { return types[b] - types[a]; }))
      sel.append($('<option></option>').val(key).html(actTypeLabel(key) + ' (' + types[key] + ')'));

   sel.val(actFilterType);
}

function actSetView(view)
{
   actView = view;
   localStorage.setItem(storagePrefix + 'actView', actView);
   actBuildControlPanel();
   actRender();
}

//***************************************************************************
// Selection (bulk edit)
//***************************************************************************

function actToggleSelect(id, on = null)
{
   if (on == null)
      on = !actSelected[id];

   if (on)
      actSelected[id] = true;
   else
      delete actSelected[id];

   // the row without re-rendering the list (the native check box is styled as a switch, too big here)

   $('#actSel_' + id).toggleClass('mdi-checkbox-marked', on).toggleClass('mdi-checkbox-blank-outline', !on).closest('tr').toggleClass('actRowSelected', on);

   // the check box of the group follows (all rows of the group selected?)

   let a = actFind(id);

   if (a) {
      let rows = $('#actGroup_' + a.type + ' .actRow').toArray();
      let all = rows.length > 0 && rows.every(function(tr) { return actSelected[tr.dataset.id]; });
      $('#actGrpSel_' + a.type).toggleClass('mdi-checkbox-marked', all).toggleClass('mdi-checkbox-blank-outline', !all);
   }

   actUpdateSelection();
}

// click on the check box of a row: with shift or ctrl all rows from the last clicked one up to this
//   one (in the order of the list, across the groups) get the state of the last clicked one

function actSelectClick(event, id)
{
   event.stopPropagation();

   if ((event.shiftKey || event.ctrlKey || event.metaKey) && actLastClicked != null && actLastClicked != id) {
      let ids = $('#container .actRow').toArray().map(function(tr) { return Number(tr.dataset.id); });
      let from = ids.indexOf(actLastClicked);
      let to = ids.indexOf(id);

      if (from >= 0 && to >= 0) {
         let on = !!actSelected[actLastClicked];

         for (let i = Math.min(from, to); i <= Math.max(from, to); i++)
            actToggleSelect(ids[i], on);

         actLastClicked = id;
         return;
      }
   }

   actToggleSelect(id);
   actLastClicked = id;
}

function actSelectGroup(type)
{
   // all rows of the group (within the filters); all selected -> deselect

   let rows = $('#actGroup_' + type + ' .actRow').toArray();
   let on = !rows.every(function(tr) { return actSelected[tr.dataset.id]; });

   for (let tr of rows)
      actToggleSelect(Number(tr.dataset.id), on);
}

function actClearSelection()
{
   actSelected = {};
   actRender();
   actUpdateSelection();
}

function actUpdateSelection()
{
   let n = Object.keys(actSelected).length;

   $('#actSelInfo').html(n ? n + ' gewählt' : 'keine Auswahl (Häkchen in der Liste)');
   $('#actBtnEditSel').prop('disabled', !n);
   $('#actBtnClearSel').prop('disabled', !n);
}

// period, name and location filter (the type filter is applied separately)

function actRegexMatcher(text)
{
   if (!text)
      return null;

   let re = null;

   try {
      re = new RegExp(text, 'i');
   }
   catch (e) {
      re = null;      // invalid expression (e.g. while typing) -> plain substring
   }

   let needle = text.toLowerCase();

   return function(value) {
      return re ? re.test(value || '') : (value || '').toLowerCase().includes(needle);
   };
}

function actTextMatcher()
{
   let byName = actRegexMatcher(actFilterName);
   let byLocation = actRegexMatcher(actFilterLocation);

   if (!byName && !byLocation)
      return null;

   return function(a) {
      return (!byName || byName(a.name)) && (!byLocation || byLocation(a.location));
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
   $('#actFilterLocation').toggleClass('actFilterActive', actFilterLocation != '');
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
   actTypeColorMap = null;

   // drop selected ids which are gone

   for (let id in actSelected)
      if (!actFind(id))
         delete actSelected[id];

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
   let matcher = actTextMatcher();
   let groups = {};
   let inPeriod = activities.activities.filter(function(a) { return actPassesFilter(a, from, matcher); });   // period / name filter only
   let items = inPeriod.filter(function(a) { return actFilterType == 'all' || a.type == actFilterType; });

   if (actView == 'map') {
      actRenderMap(root, items, inPeriod);
      return;
   }

   if (actMap) {           // back from the map
      actMap.remove();
      actMap = null;
   }

   for (let a of items) {

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
      let allSelected = g.items.every(function(a) { return actSelected[a.id]; });

      html += '<div class="actGroup" id="actGroup_' + key + '">';
      html += ' <div class="rounded-border seperatorFold actGroupHead" onclick="actToggleGroup(\'' + key + '\')">';
      html += '  <span class="actGroupArrow">' + (collapsed ? '▸' : '▾') + '</span> ';

      if (control)
         html += '<span id="actGrpSel_' + key + '" class="actSelBox mdi ' + (allSelected ? 'mdi-checkbox-marked' : 'mdi-checkbox-blank-outline') + '" title="alle Aktivitäten dieser Gruppe wählen / abwählen"'
               + ' onclick="event.stopPropagation(); actSelectGroup(\'' + key + '\')"></span> ';

      html += gpsEscape(actTypeLabel(key));
      html += '  <span class="actGroupSum">' + g.items.length + ' · ' + actFmtDuration(g.duration)
            + (g.distance ? ' · ' + gpsFmtDistance(g.distance) : '')
            + (g.maxspeed ? ' · max ' + actFmtSpeed(g.maxspeed, key) : '') + '</span>';
      html += ' </div>';

      if (!collapsed) {
         html += ' <div class="rounded-border actGroupBody">';
         html += ' <table class="tableMultiCol actTable">';
         html += '  <thead><tr>';

         if (control)
            html += '   <td class="actSel"></td>';

         html += '   <td style="width:2%;" title="oben: Details geladen, unten: Track geladen"></td>';
         html += actHeadCell('start', 'Datum', 'width:18%;');
         html += actHeadCell('name', 'Name', 'width:29%;');
         html += actHeadCell('location', 'Ort', 'width:21%;', 'actColOpt');
         html += actHeadCell('duration', 'Dauer', 'width:10%;', 'actNum');
         html += actHeadCell('distance', 'Distanz', 'width:10%;', 'actNum');
         html += '   <td style="width:10%;" class="actActions"></td>';
         html += '  </tr></thead><tbody>';

         for (let a of actSortItems(g.items)) {
            html += '  <tr class="actRow' + (actSelected[a.id] ? ' actRowSelected' : '') + '" data-id="' + a.id + '" onclick="actShowDetails(' + a.id + ')">';

            if (control)
               html += '   <td class="actSel" onclick="actSelectClick(event, ' + a.id + ')" onmousedown="event.preventDefault()" title="wählen, mit Shift / Strg bis zur zuletzt geklickten">'
                     + '<span id="actSel_' + a.id + '" class="actSelBox mdi ' + (actSelected[a.id] ? 'mdi-checkbox-marked' : 'mdi-checkbox-blank-outline') + '"></span></td>';

            html += '   <td class="actFlags"><span class="actDot' + (a.hasdetails ? ' on' : '') + '" title="Details ' + (a.hasdetails ? '' : 'nicht ') + 'geladen"></span>'
                  + '<span class="actDot' + (a.hastrack ? ' on' : '') + '" title="Track ' + (a.hastrack ? '' : 'nicht ') + 'geladen"></span></td>';
            html += '   <td class="actDate">' + actFmtDateCell(a.start) + '</td>';
            html += '   <td class="actWrap">' + gpsEscape(a.name) + '</td>';
            html += '   <td class="actWrap actColOpt">' + gpsEscape(a.location) + '</td>';
            html += '   <td class="actNum">' + actFmtDuration(a.moving || a.duration) + '</td>';
            html += '   <td class="actNum' + (a.distancefromtrack ? ' actFromTrack" title="aus dem GPS-Track berechnet, Garmin meldet ' + gpsFmtDistance(a.garmindistance) + '"' : '"') + '>'
                  + (a.distance ? gpsFmtDistance(a.distance) : '-') + '</td>';
            html += '   <td class="actActions">';

            if (control) {
               html += '    <button class="rounded-border tool-button mdi mdi-lead-pencil" type="button" title="Typ / Name / Ort ändern" onclick="event.stopPropagation(); actEditDialog([' + a.id + '])"></button>';
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

// sort by a column (click on the header), the same column again toggles the direction

function actSortBy(key)
{
   actSort = { 'key': key, 'dir': actSort.key == key && actSort.dir == 'asc' ? 'desc' : 'asc' };
   localStorage.setItem(storagePrefix + 'actSort', JSON.stringify(actSort));
   actRender();
}

function actSortValue(a)
{
   switch (actSort.key) {
      case 'name':     return a.name || '';
      case 'location': return a.location || '';
      case 'duration': return a.moving || a.duration || 0;
      case 'distance': return a.distance || 0;
   }

   return a.start || 0;
}

function actSortItems(items)
{
   let dir = actSort.dir == 'asc' ? 1 : -1;

   return items.slice().sort(function(x, y) {
      let vx = actSortValue(x);
      let vy = actSortValue(y);
      let c = typeof vx == 'string' ? vx.localeCompare(vy, 'de', { sensitivity: 'base' }) : vx - vy;

      return c ? c * dir : y.start - x.start;       // equal -> newest first
   });
}

function actHeadCell(key, label, style, cls)
{
   let active = actSort.key == key;

   return '<td style="' + style + '" class="actSortable' + (cls ? ' ' + cls : '') + '" onclick="actSortBy(\'' + key + '\')" title="nach ' + label + ' sortieren">'
        + label + (active ? '<span class="actSortArrow">' + (actSort.dir == 'asc' ? '▲' : '▼') + '</span>' : '') + '</td>';
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
// Edit (type, name and location in one dialog) - one activity or the selection (bulk)
//   bulk: empty fields / type 'unverändert' stay as they are
//***************************************************************************

function actEditDialog(ids)
{
   if (!ids || !ids.length)
      return;

   if (!activityTypes) {
      actPendingTypeChange = ids;
      showProgressDialog();
      socket.send({ "event" : "activities", "object" : { "action" : "types" } });
      return;
   }

   let a = ids.length == 1 ? actFind(ids[0]) : null;
   let bulk = ids.length > 1;

   if (!a && !bulk)
      return;

   let sel = $('<select></select>').attr('id', 'actEditType').addClass('rounded-border input').css('width', '100%');
   let types = activityTypes.types.slice().sort(function(x, y) { return actTypeLabel(x.key).localeCompare(actTypeLabel(y.key), 'de'); });

   if (bulk)
      sel.append($('<option></option>').val('').html('unverändert'));

   for (let t of types)
      sel.append($('<option></option>').val(t.key).html(actTypeLabel(t.key) + (actTypeLabels[t.key] ? '' : ' (' + t.key + ')')).prop('selected', a && t.key == a.type));

   let head = a ? actFmtDate(a.start) + ' · ' + actTypeLabel(a.type)
                : ids.length + ' Aktivitäten gewählt<br/><span class="actSyncHint">leere Felder bleiben unverändert, die Änderung wird bei Garmin gespeichert (etwa 1 s je Aktivität)</span>';

   let form = $('<div></div>').addClass('dialog-content actEditDialog')
       .append($('<div></div>').addClass('actDetailsHead').html(head))
       .append($('<div></div>').addClass('labelB1').html('Name'))
       .append($('<input></input>').attr('id', 'actEditName').attr('type', 'text').attr('maxlength', 200).addClass('rounded-border input').css('width', '100%').val(a ? a.name : ''))
       .append($('<div></div>').addClass('labelB1').html('Ort'))
       .append($('<input></input>').attr('id', 'actEditLocation').attr('type', 'text').attr('maxlength', 100).addClass('rounded-border input').css('width', '100%').val(a ? a.location : ''))
       .append($('<div></div>').addClass('labelB1').html('Typ'))
       .append(sel);

   form.dialog({
      modal: true,
      width: Math.min(420, window.innerWidth - 20),
      title: bulk ? 'Aktivitäten ändern' : 'Aktivität ändern',
      buttons: {
         'Abbrechen': function() { $(this).dialog('close'); },
         'Speichern': function() {
            let type = $('#actEditType').val();
            let name = $('#actEditName').val().trim();
            let location = $('#actEditLocation').val().trim();
            $(this).dialog('close');

            if (a) {         // only the changed fields
               if (type == a.type) type = '';
               if (name == a.name) name = '';
               if (location == a.location) location = '';
            }

            if (type == '' && name == '' && location == '')
               return;

            if (bulk) {
               pingTimeoutMs = 600000;
               actClearSelection();
            }

            showProgressDialog(bulk ? 60000 + ids.length * 2000 : 300000);   // like the timeout of the daemon
            socket.send({ "event" : "activities", "object" : { "action" : "edit", "ids" : ids, "type" : type, "name" : name, "location" : location } });
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
      let ids = actPendingTypeChange;
      actPendingTypeChange = null;
      actEditDialog(ids);
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
      case 'speed': return actFmtSpeed(value, type);
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

function actMarkLoaded(id, what)
{
   let a = actFind(id);

   if (a && !a[what]) {
      a[what] = true;
      actRender();
   }
}

function showActivityDetails(obj)
{
   hideProgressDialog();
   if (obj.full !== false)
      actMarkLoaded(obj.id, 'hasdetails');

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

      if (key == 'distance' && obj.trackdistance) {
         // the watch summed up (almost) no distance in some sessions: Garmin's value and the one of the track
         rows.push(['Distanz (Garmin)', actFmtSummary(key, summary[key], type) + (obj.distancefromtrack ? ' <span class="actSyncHint">unplausibel</span>' : '')]);
         rows.push(['Distanz (GPS-Track)', '<span' + (obj.distancefromtrack ? ' class="actFromTrack"' : '') + '>' + gpsFmtDistance(obj.trackdistance) + '</span>']);
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

   // two key/value pairs per row, one on phones (the four columns don't fit, the dialog
   //   would scroll horizontally and the buttons end below the screen)

   let narrow = window.innerWidth < 640;
   let half = narrow ? rows.length : Math.ceil(rows.length / 2);

   html += '<table class="actDetailsTable">';

   for (let i = 0; i < half; i++) {
      let l = rows[i];
      let r = narrow ? null : rows[i + half];
      html += '<tr><td>' + l[0] + '</td><td class="actNum">' + l[1] + '</td>';

      if (!narrow)
         html += r ? '<td class="actDetailsCol2">' + r[0] + '</td><td class="actNum">' + r[1] + '</td>' : '<td></td><td></td>';

      html += '</tr>';
   }

   html += '</table>';

   let partial = obj.full === false;        // the values of the list entry, 'details' not loaded yet

   if (partial)
      html += '<div class="actSyncHint">Werte aus der Aktivitätenliste. \'Details nachladen\' holt den Rest von Garmin (min. Puls, Ø Temperatur)</div>';
   else
      html += '<div class="actSyncHint">' + (obj.cached ? 'aus dem Zwischenspeicher' : 'von Garmin geladen') + '</div>';

   html += '</div>';

   let buttons = {};

   if (a.lat || a.lon)
      buttons['Track'] = function() { $(this).dialog('close'); actShowTrack(obj.id); };

   buttons[partial ? 'Details nachladen' : 'Neu laden'] = function() { $(this).dialog('close'); actShowDetails(obj.id, true); };
   buttons['Ok'] = function() { $(this).dialog('close'); };

   $(html).dialog({
      modal: true,
      width: Math.min(980, Math.round(window.innerWidth * (narrow ? 0.97 : 0.92))),
      maxHeight: Math.round(window.innerHeight * 0.92),
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

function actFmtClock(ts)
{
   return new Date(ts * 1000).toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
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
   actMarkLoaded(obj.id, 'hastrack');

   let a = actFind(obj.id) || {};
   let points = obj.points || [];

   // the track's distance came with the reply: update the row (italic when it replaces Garmin's)

   if (obj.trackdistance != null && a.id && (a.trackdistance != obj.trackdistance || a.distancefromtrack != obj.distancefromtrack)) {
      a.trackdistance = obj.trackdistance;
      a.distancefromtrack = obj.distancefromtrack;
      a.distance = obj.distancefromtrack ? obj.trackdistance : obj.garmindistance;
      actRender();
   }

   if (points.length < 2) {
      showInfoDialog({ 'status': -1, 'message': 'Kein Track für diese Aktivität' });
      return;
   }

   // speed per segment: from Garmin if present, otherwise distance / time

   let speeds = [];
   let maxSpeed = 0;
   let trackDistance = 0;

   for (let i = 1; i < points.length; i++) {
      let s = points[i][4];
      let d = actHaversine(points[i-1], points[i]);
      trackDistance += d;

      if (!s) {
         let dt = points[i][2] - points[i-1][2];
         s = dt > 0 ? d / dt : 0;
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
       ' · ' + points.length + ' Punkte · ' + gpsFmtDistance(trackDistance) + ' · max ' + actFmtSpeed(shownMax, a.type) +
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

            // canvas renderer with a tolerance, the segments are hovered for the speed

            actTrackMap = L.map(container, { attributionControl: false, fadeAnimation: false, renderer: L.canvas({ tolerance: 6 }) });

            if (navigator.onLine)
               L.tileLayer('https://tile.openstreetmap.de/{z}/{x}/{y}.png', { attribution: '© OpenStreetMap', maxZoom: 19 }).addTo(actTrackMap);

            let latlngs = points.map(function(p) { return L.latLng(p[0], p[1]); });
            let bounds = L.latLngBounds(latlngs);

            actTrackMap.fitBounds(bounds, { padding: [20, 20], maxZoom: 17 });
            gpsCenterControl(actTrackMap, function() { actTrackMap.fitBounds(bounds, { padding: [20, 20], maxZoom: 17 }); });

            // one polyline per segment, colored by its speed; hovering shows speed, time and altitude

            for (let i = 1; i < latlngs.length; i++) {
               let tip = actFmtSpeed(speeds[i-1], a.type)
                   + (points[i][2] ? ' · ' + actFmtClock(points[i][2]) : '')
                   + (points[i][3] != null ? ' · ' + Math.round(points[i][3]) + ' m' : '');

               L.polyline([latlngs[i-1], latlngs[i]], { color: actSpeedColor(maxSpeed ? speeds[i-1] / maxSpeed : 0), weight: 4, opacity: 0.9 })
                  .bindTooltip(tip, { sticky: true, direction: 'top', opacity: 0.9 })
                  .addTo(actTrackMap);
            }

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

//***************************************************************************
// World map - the start positions of the activities as points, colored by type
//   a click on a point opens the track dialog (the map stays behind it)
//***************************************************************************

var actMap = null;
var actMapMarkers = null;
var actMapLegend = null;
var actMapView = null;             // last center / zoom, kept while filtering
var actTypeColorMap = null;

// categorical palette (dark surface), assigned in a fixed order (types by frequency), the rest gray

var actPalette = ['#3987e5', '#d95926', '#199e70', '#c98500', '#d55181', '#008300', '#9085e9', '#e66767'];

function actTypeColor(type)
{
   if (!actTypeColorMap) {
      let counts = {};

      for (let a of activities.activities)
         counts[a.type] = (counts[a.type] || 0) + 1;

      let keys = Object.keys(counts).sort(function(x, y) { return counts[y] - counts[x] || x.localeCompare(y); });

      actTypeColorMap = {};
      keys.forEach(function(key, i) { actTypeColorMap[key] = i < actPalette.length ? actPalette[i] : '#9e9e9e'; });
   }

   return actTypeColorMap[type] || '#9e9e9e';
}

function actRenderMap(root, items, legendItems)
{
   if (!document.getElementById('actMap')) {
      if (actMap) {
         actMap.remove();
         actMap = null;
      }

      root.innerHTML = '<div id="actMap"></div>';
   }

   if (!actMap) {
      actMap = L.map('actMap', { attributionControl: false, fadeAnimation: false, preferCanvas: true, worldCopyJump: true });

      if (navigator.onLine)
         L.tileLayer('https://tile.openstreetmap.de/{z}/{x}/{y}.png', { attribution: '© OpenStreetMap', maxZoom: 19 }).addTo(actMap);

      actMapMarkers = L.layerGroup().addTo(actMap);
      actMap.on('moveend', function() { actMapView = { center: actMap.getCenter(), zoom: actMap.getZoom() }; });
      gpsCenterControl(actMap, function() { actMapFit(); });

      let Legend = L.Control.extend({
         options: { position: 'bottomleft' },
         onAdd: function() {
            let div = L.DomUtil.create('div', 'actMapLegend');
            L.DomEvent.disableClickPropagation(div);
            return div;
         }
      });

      actMapLegend = new Legend();
      actMap.addControl(actMapLegend);

      if (actMapView)
         actMap.setView(actMapView.center, actMapView.zoom, { animate: false });
      else
         actMap.setView([30, 10], 2);

      setTimeout(function() { actMap.invalidateSize(); }, 100);
   }

   actMapMarkers.clearLayers();

   let noPosition = 0;

   for (let a of items) {
      if (!a.lat && !a.lon) {
         noPosition++;
         continue;
      }

      L.circleMarker([a.lat, a.lon], { radius: 6, fillColor: actTypeColor(a.type), fillOpacity: 0.9, color: '#ffffff', weight: 1 })
         .bindTooltip(gpsEscape(a.name) + '<br/>' + actTypeLabel(a.type) + ' · ' + actFmtDate(a.start) + (a.location ? ' · ' + gpsEscape(a.location) : ''))
         .on('click', function() { actShowTrack(a.id); })
         .addTo(actMapMarkers);
   }

   // legend: the types within period / name filter, click restricts to one type (again: all)

   let counts = {};

   for (let a of legendItems)
      counts[a.type] = (counts[a.type] || 0) + 1;

   let html = '';

   for (let key of Object.keys(counts).sort(function(x, y) { return counts[y] - counts[x]; }))
      html += '<div class="actLegendItem' + (actFilterType == key ? ' active' : '') + '" onclick="actMapFilter(\'' + key + '\')" title="nur diesen Typ zeigen (nochmal: alle)">'
            + '<span class="actLegendDot" style="background:' + actTypeColor(key) + '"></span>' + gpsEscape(actTypeLabel(key)) + ' (' + counts[key] + ')</div>';

   html += '<div class="actSyncHint">' + (items.length - noPosition) + ' Punkte' + (noPosition ? ', ' + noPosition + ' ohne Position' : '') + ' · Klick öffnet den Track</div>';
   actMapLegend.getContainer().innerHTML = html;

   if (!actMapView)
      actMapFit();
}

function actMapFit()
{
   let layers = actMapMarkers ? actMapMarkers.getLayers() : [];

   if (!actMap || !layers.length)
      return;

   actMap.fitBounds(L.featureGroup(layers).getBounds(), { padding: [30, 30], maxZoom: 12 });
}

function actMapFilter(key)
{
   actFilterType = actFilterType == key ? 'all' : key;
   localStorage.setItem(storagePrefix + 'actFilterType', actFilterType);
   actBuildControlPanel();
   actRender();
}
