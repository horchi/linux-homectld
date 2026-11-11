/*
 *  dashboard.js
 *
 *  (c) 2020-2024 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

// layout of the widgets of the actual dashboard (option 'layout' of the dashboard, set by initDashboard()):
//   'flex' - row by row (flex-wrap), a higher widget makes its whole row higher
//   'grid' - grid of half widget units, a higher widget spans rows, the widgets beside fill them
//            (the widgets keep the sizes of 'flex', the grid only places them)

var layout = 'flex';

var weatherData = null;
var weatherInterval = null;
var mouseDownOn = { 'object' : null };
var lightClickTimeout = null;
var lightClickPosX = null;
var lightClickPosY = null;
var dashboardGauges = {};
var jDashboards = [];

const symbolColorDefault = '#ffffff';
const symbolOnColorDefault = '#059eeb';

function applyMarquee(el) {
   if (!el) return;
   el.classList.remove('marquee-active');
   const span = el.querySelector('span.marquee-text');
   const text = span ? span.textContent : el.textContent;
   el.textContent = text;
   if (el.scrollWidth > el.clientWidth) {
      el.style.setProperty('--mq', (el.scrollWidth - el.clientWidth) + 'px');
      el.innerHTML = '<span class="marquee-text">' + text + '</span>';
      el.classList.add('marquee-active');
   }
}

// big dashboard symbols: the URL parameter 'kiosk' has priority, otherwise the config option

function bigDashSymbols()
{
   if (kioskMode != null)
      return parseInt(kioskMode) != 0;

   return config.bigDashSymbols == '1';
}

// scroll position of the dashboard pages: remembered per dashboard, kept over a rebuild, a change of the
// dashboard and a reload of the page (map did -> position, stored in the localStorage)

var scrollRestoreUntil = 0;             // no storing of the position while restoring
var scrollSaveTimer = null;
var scrollPos = null;                   // { did : top }

function scrollPositions()
{
   if (scrollPos == null) {
      try { scrollPos = JSON.parse(localStorage.getItem(storagePrefix + 'dashScrollPos') || '{}'); } catch (e) { scrollPos = {}; }
   }

   return scrollPos;
}

function scrollRestore(did, top)
{
   let $c = $('#container');

   scrollRestoreUntil = Date.now() + 1000;
   $c.scrollTop(top);

   // widgets may still grow (charts, images, values): once more after the layout settled

   if (top > 0)
      setTimeout(function() { if (did == actDashboard && Math.abs($c.scrollTop() - top) > 1) $c.scrollTop(top); }, 300);
}

function scrollInit()
{
   $('#container').off('scroll.dash').on('scroll.dash', function() {
      // not while rebuilding / restoring and not if the container shows something else (the
      // clamping of the position by removed content would be taken as scrolling of the user)

      if (Date.now() < scrollRestoreUntil || !$('#widgetContainer').length)
         return;

      scrollPositions()[actDashboard] = $('#container').scrollTop();

      clearTimeout(scrollSaveTimer);
      scrollSaveTimer = setTimeout(function() {
         try {
            localStorage.setItem(storagePrefix + 'dashScrollPos', JSON.stringify(scrollPositions()));
         } catch (e) {}
      }, 300);
   });
}

function initDashboard(update = false)
{
   // console.log("initDashboard " + JSON.stringify(allSensors, undefined, 4));

   if (allSensors == null) {
      console.log("Fatal: Missing widgets!");
      return;
   }

   scrollRestoreUntil = Date.now() + 1000;

   initLongTouch();

   for (const gkey in dashboardGauges) {
      if (dashboardGauges.hasOwnProperty(gkey)) {
         dashboardGauges[gkey].destroy();
      }
   }

   dashboardGauges = {};

   if (!update) {
      $('#container').removeClass('hidden');
      $('#dashboardMenu').removeClass('hidden gpsToolbar gpsToolbarBig');
      $('#dashboardMenu').empty();
   }

   // if (!Object.keys(dashboards).length)
   //    setupMode = true;

   if (setupMode) {
      $('#dashboardMenu').append($('<button></button>')
                                 .addClass('rounded-border buttonDashboard buttonDashboardTool')
                                 .addClass('mdi mdi-close-outline')
                                 .attr('title', 'Setup beenden')
                                 .click(function() { setupDashboard(); })
                                );
   }

   jDashboards = [];

   htmlWidgetDeps = {};

   for (let did in dashboards) {
      if (!dashboardGroup || dashboards[did].group == dashboardGroup)
         jDashboards.push([dashboards[did].order, did]);
   }
   jDashboards.sort((a, b) => a[0] - b[0]);   // sort() without comparator compares as strings ('10,..' < '2,..')

   // no (valid) dashboard remembered (fresh storage after a login, deleted dashboard,
   // storage of another instance) -> the first one gets active

   if (actDashboard == null || actDashboard < 0 ||
       (Object.keys(dashboards).length && dashboards[actDashboard] == null))
      actDashboard = -1;

   for (let i = 0; i < jDashboards.length; i++) {
      let did = jDashboards[i][1];
      if (actDashboard < 0)
         setActualDashboard(did, i);

      if (kioskBackTime > 0 && actDashboard != jDashboards[0][1]) {
         setTimeout(function() {
            setActualDashboard(jDashboards[0][1], 0);
            initDashboard(false);
         }, kioskBackTime * 1000);
      }

      let classes = dashboards[did].symbol != '' ? dashboards[did].symbol.replace(':', ' ') : '';

      if (dashboards[actDashboard] == dashboards[did])
         classes += ' buttonDashboardActive';

      $('#dashboardMenu').append($('<button></button>')
                                 .addClass('rounded-border buttonDashboard widgetDropZone')
                                 .addClass(classes)
                                 .attr('id', 'dash' + did)
                                 .attr('draggable', setupMode)
                                 .attr('data-droppoint', setupMode)
                                 .attr('data-dragtype', 'dashboard')
                                 .data('index', i)
                                 .data('did', did)
                                 .on('dragstart', function(event) {dragDashboard(event)})
                                 .on('dragover', function(event) {dragOverDashboard(event)})
                                 .on('dragleave', function(event) {dragLeaveDashboard(event)})
                                 .on('drop', function(event) {dropDashboard(event)})
                                 .html(dashboards[did].title)
                                 .click({'id': did, 'index': i}, function(event) {
                                    if (event.ctrlKey) {
                                       // dashboard setup without the setup mode (like the widgets, needs 'full control')
                                       if (localStorage.getItem(storagePrefix + 'Rights') & 0x04)
                                          dashboardSetup('dash' + $(this).data('did'));
                                       return;
                                    }
                                    setActualDashboard($(this).data('did'), $(this).data('index'));
                                    console.log("Activate dashboard " + actDashboard);
                                    initDashboard();
                                 }));

      if (bigDashSymbols()) {
         $('#dash'+did).css('font-size', '-webkit-xxx-large');
         $('#dash'+did).css('font-size', 'xxx-large');
      }

      if (setupMode && did == actDashboard)
         $('#dashboardMenu').append($('<button></button>')
                                    .addClass('rounded-border buttonDashboard buttonDashboardTool')
                                    .addClass('mdi mdi-lead-pencil')  //  mdi-draw-pen
                                    .click({"id" : 'dash'+did}, function(event) { dashboardSetup(event.data.id); })
                                   );
   }

   // additional elements in setup mode

   if (setupMode) {
      $('#dashboardMenu')
         .append($('<div></div>')
                 .css('width', '-webkit-fill-available')
                 .css('width', '-moz-available'))
         .append($('<div></div>')
                 .css('display', 'flex')
                 .css('float', 'right')
                 .append($('<input></input>')
                         .addClass('rounded-border input')
                         .css('margin', '15px,15px,15px,0px')
                         .css('align-self', 'center')
                         .attr('id', 'newDashboard')
                         .attr('title', 'Neues Dashboard')
                        )
                 .append($('<button></button>')
                         .addClass('rounded-border buttonDashboard buttonDashboardTool')
                         .addClass('mdi mdi-file-plus-outline')
                         .attr('title', 'dashboard hinzufügen')
                         .click(function() { newDashboard(); })
                        )
                 .append($('<button></button>')
                         .addClass('rounded-border buttonDashboard buttonDashboardTool')
                         .addClass('mdi mdi-shape-plus')
                         .attr('title', 'widgets zum dashboard hinzufügen')
                         .css('font-size', 'x-large')
                         .click(function() { addWidget(); })
                        )
                );
   }

   initScrollStrip('dashboardMenu');

   // widgets

   document.getElementById("container").innerHTML = '<div id="widgetContainer" class="widgetContainer"></div>';

   $('#container')
      .append($('<div></div>')
              .attr('id', 'widgetContainer')
              .addClass('widgetContainer'));

   let dashOptions = dashboards[actDashboard] ? dashboards[actDashboard].options : null;
   layout = dashOptions && dashOptions.layout == 'grid' ? 'grid' : 'flex';

   if (layout == 'flex') {
      $('#widgetContainer').css('display', 'flex');
      $('#widgetContainer').css('flex-wrap', 'wrap');
   }
   else {
      $('#widgetContainer').css('display', 'grid');
      $('#widgetContainer').css('gap', '0px');
      $('#widgetContainer').css('grid-auto-flow', 'row');        // keeps the order of the widgets
      $('#widgetContainer').css('align-content', 'start');
      $('#widgetContainer').css('align-items', 'start');
   }

   // swipe/slide dashboard page

   let isScrolling = false;

   $("#container").swipe({
      'swipeStatus': function(event, phase, direction, distance) {
         const $widgetContainer = $("#widgetContainer");

         const maxDistance = 300; // Entfernung, bei der die minimale Deckkraft erreicht ist
         const threshold = 50; // Ab hier wird es schwerfälliger
         const resistance = 0.3; // 30% Bewegung nach dem Schwellenwert

         if (phase === 'start') {
            isScrolling = false;
         }
         else if (phase === "move") {
            let moveX = (distance > threshold) ? threshold + (distance - threshold) * resistance : distance;
            let finalX = (direction === "left") ? -moveX : moveX;
            let opacity = 1 - (Math.min(distance, maxDistance) / maxDistance) * 0.6;

            if (direction === "left" || direction === "right") {
               $widgetContainer.css({
                  'transition': 'none',
                  'transform': 'translate3d(' + finalX + 'px, 0, 0)',
                  'opacity': opacity
               });
            }
            else   // horizontal -> zurück zur Mitte
               $widgetContainer.css("transform", "translate3d(0, 0, 0)");

            // Wir erlauben eine kleine Toleranz (10px), bevor wir "Sperren"
            if (distance > 10) {
               isScrolling = true;
               // Klicks auf Unterelementen während des Swipes blockieren
               $widgetContainer.css('pointer-events', 'none');
            }
         }
         else if (phase === "end") {
            $widgetContainer.css("pointer-events", "auto");
            // Finger losgelassen UND Schwellenwert (threshold) erreicht
            if (direction === "left")
               animateOut($widgetContainer, "-100%", "next");
            else if (direction === "right")
               animateOut($widgetContainer, "100%", "prev");
         }

         else if (phase === "cancel") {
            // Nicht weit genug gewischt: Zurück zur Mitte (Snap-back)
            $widgetContainer.css({
               'transition': 'transform 0.5s cubic-bezier(0.175, 0.885, 0.32, 1.275)',
               'transform': 'translate3d(0, 0, 0)',
               'pointer-events': 'auto',
               'opacity' : 1
            });
         }
      },
      'preventDefaultEvents': false,
      'excludedElements': 'input, select, textarea, .noSwipe, #windywidgettable', // KEINE Buttons oder Links hier!
      'threshold': 120,                // Mindestpixel für Seitenwechsel
      'fingerReleaseThreshold': 30,    // Verhindert zu sensible Reaktionen
      'allowPageScroll': 'vertical'
   });

   $('#container').swipe(setupMode ? 'disable' : 'enable');

   if (dashboards[actDashboard] != null) {
      for (let key in dashboards[actDashboard].widgets) {
         initWidget(key, dashboards[actDashboard].widgets[key]);
         updateWidget(allSensors[key], true, dashboards[actDashboard].widgets[key]);
      }
   }

   initLightColorDialog();
   initLightDimDialog();

   resizeDashboard();
   window.onresize = function() { resizeDashboard(); };

   scrollInit();
   scrollRestore(actDashboard, scrollPositions()[actDashboard] || 0);     // the remembered position of the dashboard
}

function animateOut($el, targetX, pageDir)
{
   $el.css({
      "transition": "transform 0.3s ease-in",
      "transform": "translate3d(" + targetX + ", 0, 0)"
   });

   // Nach der Animation: Inhalt tauschen und von der anderen Seite reinholen
   setTimeout(() => {
      if (pageDir == 'prev') {
         let index = actDashboardIndex;
         if (!index)
            index = jDashboards.length;
         setActualDashboard(jDashboards[index-1][1], index-1);
      }
      else if (pageDir == 'next') {
         let index = actDashboardIndex+1;
         if (index == jDashboards.length)
            index = 0;
         setActualDashboard(jDashboards[index][1], index);
      }

      // console.log("Activate dashboard", actDashboard);
      initDashboard();

      const startX = (targetX === "100%") ? "-100%" : "100%";
      $el.css({ "transition": "none", "transform": "translate3d(" + startX + ", 0, 0)" });
      $el[0].offsetHeight; // Force Reflow
      $el.css({ "transition": "transform 0.3s ease-out", "transform": "translate3d(0, 0, 0)" });
   }, 300);
}

function resizeDashboard()
{
   let space = 20;

   if (getTotalHeightOf('menu') == 0)
      space = 10;

   $("#container").height($(window).outerHeight() - getTotalHeightOf('menu') - getTotalHeightOf('dashboardMenu') - getTotalHeightOf('footer') - sab - space);

   if (layout == 'grid') {
      // a column is half a widget of width 1 incl. its margin (see initWidget()), the
      // rows size by the widgets (fixed heights like 'flex'), a linefeed spacer is a thin row

      let colWidth = gridColumnWidth();
      let colCount = Math.max(1, Math.floor($('#container').innerWidth() / colWidth));

      $('#widgetContainer').css('grid-template-columns', 'repeat(' + colCount + ', ' + colWidth + 'px)');

      // widgets wider than the grid would add columns

      $('#widgetContainer > div[data-colspan]').each(function() {
         $(this).css('grid-column', 'span ' + Math.min(parseInt(this.dataset.colspan), colCount));
      });
   }
}

// width of a grid column (layout 'grid'): half of a widget of width 1 incl. its margin

function gridColumnWidth()
{
   const marginWidth = 3 *2;
   const useKioskHeight = kioskMode == 1 || kioskMode == 2;
   let style = getComputedStyle(document.documentElement);
   let widgetWidthBase = parseFloat(style.getPropertyValue('--widgetWidthBase'));
   let kioskFactor = 1;

   if (useKioskHeight && dashboards[actDashboard] && dashboards[actDashboard].options && dashboards[actDashboard].options.widthfactorKiosk)
      kioskFactor = parseFloat(dashboards[actDashboard].options.widthfactorKiosk) || 1;

   return kioskFactor * (widgetWidthBase + marginWidth / 2);
}

function newDashboard()
{
   if (!$('#newDashboard').length)
      return;

   let name = $('#newDashboard').val();
   $('#newDashboard').val('');

   if (name != '') {
      console.log("new dashboard: " + name);
      socket.send({ "event" : "storedashboards", "object" : { [-1] : { 'title' : name, 'widgets' : {} } } });
      socket.send({ "event" : "forcerefresh", "object" : { 'action' : 'dashboards' } });
   }
}

function getWidgetColor(widget, colorCondition, defaultColor, value)
{
   if (colorCondition == null || colorCondition == '') {
      if (defaultColor != null)
         return defaultColor;
      return 'white';
   }
   else {
      let conditions = colorCondition.split(",");
      for (c = 0; c < conditions.length; ++c) {
         if (conditions[c].indexOf('=') != -1) {
            let [val,col] = conditions[c].split("=");
            if (value == val)
               return col;
         }
         else if (conditions[c].indexOf('~') != -1) {   // ~ für string enthalten in
            let [val,col] = conditions[c].split("~");
            if (value.indexOf(val) != -1)
               return col;
         }
         else if (conditions[c].indexOf('<') != -1) {1
            let [val,col] = conditions[c].split("<");
            if (val < value)
               return col;
         }
         else if (conditions[c].indexOf('>') != -1) {
            let [val,col] = conditions[c].split(">");
            if (val > value)
               return col;
         }
         else if (conditions[c].indexOf('<=') != -1) {1
            let [val,col] = conditions[c].split("<=");
            if (val <= value)
               return col;
         }
         else if (conditions[c].indexOf('>=') != -1) {
            let [val,col] = conditions[c].split(">=");
            if (val >= value)
               return col;
         }
         else if (conditions[c].indexOf('<>') != -1) {
            let [val,col] = conditions[c].split("<>");
            if (val != value)
               return col;
         }
      }
   }
}

// battery symbol by the charge [%]: green, yellow (< 60 %), orange (< 30 %), red (< 15 %)

function batterySymbol(percent)
{
   if (percent == null || percent < 0)
      return $('<span></span>');

   let level = Math.min(100, Math.max(0, Math.round(percent / 10) * 10));
   let icon = level >= 100 ? 'mdi-battery' : level == 0 ? 'mdi-battery-outline' : 'mdi-battery-' + level;
   let color = percent >= 60 ? 'var(--green)' : percent >= 30 ? 'var(--yellow)'
       : percent >= 15 ? 'var(--orange)' : 'var(--red)';

   return $('<span></span>').addClass('mdi ' + icon).css('color', color).css('font-size', 'larger')
      .attr('title', percent + ' %');
}

function getWidgetTitle(widget, fact)
{
   let title = ' ';

   if (widget.title && widget.title != '')
      title += widget.title;
   else if (fact && fact.usrtitle && fact.usrtitle != '')
      title += fact.usrtitle;
   else if (fact)
      title += fact.title;

   return title;
}

function getWidgetTitleClass(widget, fact)
{
   let titleClass = '';
   let title = '';

   if (fact != null)
      title = (fact.usrtitle != null ? fact.usrtitle : fact.title).toUpperCase();

   // #TODO move to configuration

   if (!setupMode && widget.unit == '°C' && title.toLowerCase().includes('BOILER'))
      titleClass = 'mdi mdi-shower-head';
   else if (!setupMode && title.toLowerCase().includes('zeit'))
      titleClass = 'mdi mdi-timer-sand-complete';
   else if (!setupMode && widget.unit == '°C')
      titleClass = 'mdi mdi-thermometer';
   else if (!setupMode && widget.unit == 'bar' && fact != null && fact.type == 'TPMS')
      titleClass = 'mdi mdi-car-tire-alert';
   else if (!setupMode && (widget.unit == 'hPa' || widget.unit == 'bar' ||
                           widget.unit == 'A' || widget.unit == 'mA' ||
                           widget.unit == 'W' || widget.unit == 'V' ||
                           widget.unit == 'Ah'))
      titleClass = 'mdi mdi-gauge';
   else if (!setupMode && widget.unit == '%' && title.indexOf('FEUCHT') != -1)
      titleClass = 'mdi mdi-water-percent';
   else if (!setupMode && title.indexOf('GAS') != -1)
      titleClass = 'mdi mdi-gas-cylinder';
   else if (!setupMode && widget.unit == '%')
      titleClass = 'mdi mdi-label-percent-outline';
   else if (!setupMode && widget.unit == 'l')
      titleClass = 'mdi mdi-water';
   else if (widget && widget.symbolOn)
      titleClass = widget.symbolOn.replace(':', ' ');
   else if (widget && widget.symbol)
      titleClass = widget.symbol.replace(':', ' ');

   return titleClass;
}

//***************************************************************************
// Init a Widget
//***************************************************************************

function initWidget(key, widget, fact)
{
   if (key == null || key == '')
      return ;

   if (fact == null)
      fact = valueFacts[key];

   if (widget == null) {
      console.log("Missing widget for '" + key + "'  ignoring");
      return;
   }

   // widgets without usable sensor are shown marked as placeholder (see below):
   //   orphaned - the sensor (valuefact) does not exist (anymore)
   //   inactive - the sensor exists but is not active

   let placeholder = null;

   if (widget.widgettype != 10 && widget.widgettype != 11 && widget.widgettype != 18) {
      if (fact == null) {
         console.log("Fact '" + key + "' not found, showing widget as orphaned");
         placeholder = { text: 'Sensor fehlt', hint: 'Sensor ' + key + ' existiert nicht (mehr)' };
      }
      else if (!fact.state)
         placeholder = { text: 'Sensor inaktiv', hint: 'Sensor ' + key + ' ist nicht aktiv' };
   }

   // console.log("Widget " + key + ': '+ JSON.stringify(widget));
   // console.log("initWidget " + key + " : " + (fact ? fact.name : ''));
   // console.log("fact: " + JSON.stringify(fact, undefined, 4));
   // console.log("widget: " + JSON.stringify(widget, undefined, 4));

   if (fact && widget.unit == '')
      widget.unit = fact.unit;

   const marginWidth = 3 *2;
   const useKioskHeight = kioskMode == 1 || kioskMode == 2;
   let root = document.getElementById("widgetContainer");
   let id = 'div_' + key;
   let elem = document.getElementById(id);

   if (elem == null) {

      elem = document.createElement("div");
      root.appendChild(elem);
      elem.setAttribute('id', id);
   }

   {
      // the height (also for 'grid', there the rows size by the widgets), on each
      // init - the 'Vorschau' of the widget setup re-inits the existing element

      let style = getComputedStyle(document.documentElement);
      let widgetHeightBase = style.getPropertyValue('--widgetHeightBase') * 2;

      eHeight = widgetHeightBase;

      // verschiedene heightfactor für verscheidene dashboards und / kiosk mode

      if (heightFactor && heightFactor != 1)
         eHeight = widgetHeightBase * heightFactor;
      else if (!useKioskHeight && dashboards[actDashboard].options && dashboards[actDashboard].options.heightfactor)
         eHeight = widgetHeightBase * dashboards[actDashboard].options.heightfactor;
      else if (useKioskHeight && dashboards[actDashboard].options && dashboards[actDashboard].options.heightfactorKiosk) {
         eHeight = widgetHeightBase * dashboards[actDashboard].options.heightfactorKiosk;
         // console.log("eHeight", eHeight, "dashboards[actDashboard].options.heightfactorKiosk", dashboards[actDashboard].options.heightfactorKiosk);
      }

      if (widget.heightfactor != null && widget.heightfactor != 1)
         eHeight = eHeight * widget.heightfactor + ((widget.heightfactor-1) * marginWidth);

      elem.style.height = eHeight + 'px';

      // reset what a linefeed spacer sets (the widget type may have changed)

      $(elem).css({ 'flex-basis': '', 'padding': '', 'margin': '', 'align-content': '' });
   }

   elem.innerHTML = '';

   if (layout == 'grid') {
      // spans in half widget units, the columns get limited by resizeDashboard()

      let widthFactor = widget.widthfactor != null ? parseFloat(widget.widthfactor) : 1;
      let heightFactor = widget.heightfactor != null ? parseFloat(widget.heightfactor) : 1;

      elem.dataset.colspan = Math.max(1, Math.round(widthFactor * 2));
      $(elem).css('grid-column', 'span ' + elem.dataset.colspan);
      $(elem).css('grid-row', 'span ' + Math.max(1, Math.round(heightFactor * 2)));
   }

   {
      // the width (also for 'grid', the widget fills its columns exactly)

      let style = getComputedStyle(document.documentElement);
      let widgetWidthBase = style.getPropertyValue('--widgetWidthBase') * 2;
      let widthFactor = widget.widthfactor != null ? widget.widthfactor : 1;

      if (useKioskHeight && dashboards[actDashboard].options && dashboards[actDashboard].options.widthfactorKiosk)
         widthFactor *= dashboards[actDashboard].options.widthfactorKiosk;

      if (widthFactor != 1)
         elem.style.width = widgetWidthBase * widthFactor + ((widthFactor-1) * marginWidth) + 'px'; // adjust margin not the formula
      else
         elem.style.width = widgetWidthBase + 'px';

      // console.log("widget", key, "width", elem.style.width, "widgetWidthBase", widgetWidthBase, "widthFactor", widthFactor);
   }

   if (setupMode && (widget.widgettype < 900 || widget.widgettype == null)) {
      elem.setAttribute('draggable', true);
      elem.dataset.droppoint = true;
      elem.dataset.dragtype = 'widget';
      elem.addEventListener('dragstart', function(event) {dragWidget(event)}, false);
      elem.addEventListener('dragover', function(event) {dragOver(event)}, false);
      elem.addEventListener('dragleave', function(event) {dragLeave(event)}, false);
      elem.addEventListener('drop', function(event) {dropWidget(event)}, false);
   }

   let titleClass = getWidgetTitleClass(widget, fact);
   let title = getWidgetTitle(widget, fact);

   if (!widget.color)
      widget.color = 'white';

   if (placeholder) {
      $(elem)
         .addClass("widget widgetDropZone widget-orphaned")
         .append($('<div></div>')
                 .addClass('widget-title ' + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : ''))
                 .css('user-select', 'none')
                 .attr('title', placeholder.hint)
                 .click(function(event) {titleClick(event.ctrlKey, key);})
                 .html(title.trim() != '' ? title : ' ' + key))
         .append($('<div></div>')
                 .addClass('widget-orphaned-main')
                 .css('user-select', 'none')
                 .click(function(event) {titleClick(event.ctrlKey, key);})
                 .append($('<span></span>').addClass('mdi mdi-link-variant-off'))
                 .append($('<div></div>').html(placeholder.text))
                 .append($('<div></div>').addClass('widget-orphaned-key').html(key)));
      return;
   }

   $(document).on({'mouseup touchend' : function(e) {
      if (!mouseDownOn.object)
         return;
      e.preventDefault();
      let scale = parseInt((e.pageX - $(mouseDownOn.div).position().left) / ($(mouseDownOn.div).innerWidth() / 100));
      if (scale > 100) scale = 100;
      if (scale < 0) scale = 0;
      console.log("dim: " + scale + '%');
      toggleIo(mouseDownOn.fact.address, mouseDownOn.fact.type, scale);
      mouseDownOn = { 'object' : null };
   }});

   $(document).on({'mousemove touchmove' : function(e) {
      if (!mouseDownOn.object)
         return;
      e.preventDefault();
      let scale = parseInt((e.pageX - $(mouseDownOn.div).position().left) / ($(mouseDownOn.div).innerWidth() / 100));
      if (scale > 100) scale = 100;
      if (scale < 0) scale = 0;
      $(mouseDownOn.object).css('width', scale + '%');
   }})

   switch (widget.widgettype)
   {
      case 0: {          // Symbol
         $(elem)
            .addClass("widget widgetDropZone")
            .append($('<div></div>')
                    .addClass('widget-title ' + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : ''))
                    .addClass(titleClass)
                    .click(function(event) { console.log("click"); titleClick(event.ctrlKey, key); })
                    .css('user-select', 'none')
                    .html(title))
            .append($('<button></button>')
                    .attr('id', 'button' + fact.type + fact.address)
                    .attr('type', 'button')
                    .css('color', widget.color)
                    .css('user-select', 'none')
                    .css('border-radius', '100%')
                    .css('width', '100%')
                    .css('height', '100%')
                    .addClass('rounded-border')
                    .addClass('widget-main')
                    .append($('<img></img>')
                            .attr('id', 'widget' + fact.type + fact.address)
                            .attr('draggable', false)
                            .css('user-select', 'none'))
                   );

         if (!setupMode) {
            $('#button' + fact.type + fact.address)
               .on('mousedown touchstart', {"fact" : fact}, function(e) {
                  if (lightEmulatedMouse(e))
                     return;
                  if (e.touches != null) {
                     lightClickPosX = e.touches[0].pageX;
                     lightClickPosY = e.touches[0].pageY;
                  }
                  else
                  {
                     e.preventDefault();
                     lightClickPosX = e.clientX;
                     lightClickPosY = e.clientY;
                  }
                  // e.preventDefault();
                  if ((e.which != 0 && e.which != 1) || lightDialogOpen())
                     return;
                  // long press: color (color lights) resp. brightness (dimmable lights) dialog
                  if (fact.options & 0x06) {
                     lightClickTimeout = setTimeout(function(event) {
                        lightClickTimeout = lightClickPosX = lightClickPosY = null;
                        if (fact.options & 0x04)
                           showLightColorDialog(key);
                        else
                           showLightDimDialog(key);
                     }, 400);
                  }
               })
               .on('mousemove touchmove', {"fact" : fact}, function(e) {
                  // moved (e.g. scrolling the dashboard) -> no long press and no toggle
                  if (lightEmulatedMouse(e) || lightClickPosX == null || lightClickPosY == null)
                     return;
                  let x = e.touches != null ? e.touches[0].pageX : e.clientX;
                  let y = e.touches != null ? e.touches[0].pageY : e.clientY;
                  if (Math.abs(lightClickPosX - x) >= 15 || Math.abs(lightClickPosY - y) >= 15) {
                     if (lightClickTimeout)
                        clearTimeout(lightClickTimeout);
                     lightClickTimeout = lightClickPosX = lightClickPosY = null;
                  }
               })
               .on('mouseup mouseleave touchend touchcancel', {"fact" : fact}, function(e) {
                  if (lightEmulatedMouse(e) || lightDialogOpen())
                     return;
                  if (e.type == 'touchcancel') {        // the browser took the touch (e.g. scrolling)
                     if (lightClickTimeout)
                        clearTimeout(lightClickTimeout);
                     lightClickTimeout = lightClickPosX = lightClickPosY = null;
                     return;
                  }
                  if (e.changedTouches != null) {
                     posX = e.changedTouches[0].pageX;
                     posY = e.changedTouches[0].pageY;
                  }
                  else
                  {
                     posX = e.clientX;
                     posY = e.clientY;
                  }
                  if (lightClickPosX != null || lightClickPosY != null) {
                     if (Math.abs(lightClickPosX - posX) < 15 && Math.abs(lightClickPosY - posY) < 15) {
                        e.preventDefault();
                        e.stopPropagation();
                        function doToggle() { toggleIo(fact.address, fact.type); }
                        if (fact.settings && fact.settings.ask)
                           confirmDialog(doToggle, 'Wirklich schalten', 'Schalten', 'Abbrechen');
                        else if (!lightToggledJustNow(key))
                           toggleIo(fact.address, fact.type);
                     }
                  }
                  if (lightClickTimeout)
                     clearTimeout(lightClickTimeout);
                  lightClickTimeout = lightClickPosX = lightClickPosY = null;
               });
         }

         break;
      }

      case 1:         // Chart
      case 13: {
         elem.className = "widgetChart widgetDropZone";
         let eTitle = document.createElement("div");
         let cls = setupMode ? 'mdi mdi-lead-pencil widget-edit' : '';
         eTitle.className = "widget-title " + cls + ' ' + titleClass;
         eTitle.innerHTML = title;
         eTitle.addEventListener("click", function(event) {titleClick(event.ctrlKey, key)}, false);
         elem.appendChild(eTitle);

         let ePeak = document.createElement("div");
         ePeak.setAttribute('id', 'peak' + fact.type + fact.address);
         ePeak.className = "chart-peak";
         elem.appendChild(ePeak);

         let eValue = document.createElement("div");
         eValue.setAttribute('id', 'value' + fact.type + fact.address);
         eValue.className = "chart-value";
         eValue.style.color = widget.color;
         elem.appendChild(eValue);

         let eChart = document.createElement("div");
         eChart.className = "chart-canvas-container";
         let cFact = fact;
         if (!setupMode && fact.record) {
            eChart.setAttribute("onclick", "toggleChartDialog('" + cFact.type + "'," + cFact.address + ")");
            $(eChart).css('cursor', 'pointer');
         }
         elem.appendChild(eChart);

         let eCanvas = document.createElement("canvas");
         eCanvas.setAttribute('id', 'widget' + fact.type + fact.address);
         eCanvas.className = "chart-canvas";
         eChart.appendChild(eCanvas);
         break;
      }

      case 3: {        // type 3 (Value)
         $(elem)
            .addClass("widgetValue widgetDropZone")
            .append($('<div></div>')
                    .addClass('widget-title ' + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : ''))
                    .addClass(titleClass)
                    .css('user-select', 'none')
                    .click(function(event) {titleClick(event.ctrlKey, key);})
                    .html(title))
            .append($('<div></div>')
                    .attr('id', 'widget' + fact.type + fact.address)
                    .addClass('widget-value')
                    .css('user-select', 'none')
                    .css('color', widget.color)
                    .css('cursor', !setupMode && fact.record ? 'pointer' : '')
                    .click(function() {
                       let cFact = fact;
                       if (!setupMode && fact.record)
                          toggleChartDialog(cFact.type, cFact.address, key);}))
            .append($('<div></div>')
                    .attr('id', 'peak' + fact.type + fact.address)
                    .css('user-select', 'none')
                    .addClass('chart-peak'));

         break;
      }
      case 4: {        // Gauge
         $(elem)
            .addClass("widgetGauge participation widgetDropZone")
            .append($('<div></div>')
                    .addClass('widget-title ' + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : ''))
                    .addClass(titleClass)
                    .css('user-select', 'none')
                    .click(function(event) {titleClick(event.ctrlKey, key);})
                    .html(title))
            .append($('<div></div>')
                    .attr('id', 'svgDiv' + fact.type + fact.address)
                    .addClass('widget-main-gauge')
                    .css('user-select', 'none')
                    .append($('<svg></svg>')
                            .attr('viewBox', '0 0 1000 600')
                            .attr('preserveAspectRatio', 'xMidYMin slice')
                            .append($('<path></path>')
                                    .attr('id', 'pb' + fact.type + fact.address))
                            .append($('<path></path>')
                                    .attr('id', 'pv' + fact.type + fact.address)
                                    .addClass('data-arc'))
                            .append($('<path></path>')
                                    .attr('id', 'pp' + fact.type + fact.address)
                                    .addClass('data-peak'))
                            .append($('<path></path>')
                                    .attr('id', 'pm' + fact.type + fact.address)
                                    .addClass('data-peak-min'))
                            .append($('<text></text>')
                                    .attr('id', 'value' + fact.type + fact.address)
                                    .addClass('gauge-value')
                                    .attr('font-size', "140")
                                    .attr('font-weight', 'bold')
                                    .attr('text-anchor', 'middle')
                                    .attr('alignment-baseline', 'middle')
                                    .attr('x', '500')
                                    .attr('y', '450'))
                            .append($('<text></text>')
                                    .attr('id', 'sMin' + fact.type + fact.address)
                                    .addClass('scale-text')
                                    .attr('text-anchor', 'middle')
                                    .attr('alignment-baseline', 'middle')
                                    .attr('x', '50')
                                    .attr('y', '550'))
                            .append($('<text></text>')
                                    .attr('id', 'sMax' + fact.type + fact.address)
                                    .addClass('scale-text')
                                    .attr('text-anchor', 'middle')
                                    .attr('alignment-baseline', 'middle')
                                    .attr('x', '950')
                                    .attr('y', '550'))));
         let cFact = fact;
         if (!setupMode && fact.record) {
            $(elem).click(function() {toggleChartDialog(cFact.type, cFact.address, key);});
            $(elem).css('cursor', 'pointer');
         }

         let divId = '#svgDiv' + fact.type + fact.address;
         $(divId).html($(divId).html());  // redraw to activate the SVG !!
         break;
      }

      case 5:            // Meter
      case 6: {          // MeterLevel
         initMeter(key, widget, fact, widget.scalemax, null);
         break;
      }

      case 7: {          // PlainText
         $(elem).addClass('widgetPlain widgetDropZone');
         if (setupMode)
            $(elem).append($('<div></div>')
                           .addClass('widget-title ' + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : ''))
                           .addClass(titleClass)
                           .css('user-select', 'none')
                           .css('padding', fact.type == 'WEA' ? '0px' : '')
                           .click(function(event) {titleClick(event.ctrlKey, key);})
                           .html(title));

         if (fact.type == 'WEA' && fact.address != 1) {
            if (fact.address == 2 || fact.address == 4)
               initWindy(key, widget, fact);
            else if (fact.address == 3)
               initWindyMap(key, widget, fact);
         }
         else {
            let wFact = fact;
            $(elem).append($('<div></div>')
                           .attr('id', 'widget' + fact.type + fact.address)
                           .addClass(fact.type == 'WEA' ? 'widget-weather' : 'widget-text')
                           .css('user-select', 'none')
                           .css('color', widget.color)
                           .css('height', 'inherit')
                           .click(function() { if (wFact.type == 'WEA' && wFact.address == 1) weatherForecast(); }));
         }
         break;
      }

      case 8: {     // 8 (Choice)
         console.log("choices:", fact.choices);
         $(elem)
            .addClass("widget widgetDropZone")
            .append($('<div></div>')
                    .addClass('widget-title ' + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : ''))
                    .addClass(titleClass)
                    .css('user-select', 'none')
                    .css('position', 'relative')
                    .click(function(event) {titleClick(event.ctrlKey, key);})
                    .html(title))
            .append($('<div></div>')
                    .attr('id', 'choice' + fact.type + fact.address)
                    .addClass('widget-choice rounded-border')
                    .css('color', widget.color))
         let choices = fact.choices.split(",");

         // option 'buttons': less than 5 choices stacked, otherwise a grid of equally wide buttons

         if (widget.buttons)
            $('#choice' + fact.type + fact.address)
               .addClass('widget-choice-buttons ' + (choices.length < 5 ? 'widget-choice-buttons-column' : 'widget-choice-buttons-measure'));

         for (c = 0; c < choices.length; ++c) {
            $('#choice' + fact.type + fact.address)
               .append($('<div></div>')
                       .attr('id', 'widget' + fact.type + fact.address + '_' + c)
                       .addClass('rounded-border' + (widget.buttons ? ' widget-choice-button' : ''))
                       .html(choices[c])
                       .click({ "value" : choices[c] }, function(event) {
                          socket.send({'event': 'toggleio', 'object': {'address': fact.address, 'type': fact.type, 'value': event.data.value, 'action': 'switch'}});
                       }));
         }

         if (widget.buttons && choices.length >= 5) {
            // measure the widest button (natural width), then switch to a grid whose columns
            // are all that wide -> as many equal columns as fit, the rest wraps

            let container = $('#choice' + fact.type + fact.address);
            let maxWidth = 0;
            container.children().each(function() { maxWidth = Math.max(maxWidth, $(this).outerWidth()); });
            container
               .removeClass('widget-choice-buttons-measure')
               .addClass('widget-choice-buttons-grid')
               .css('grid-template-columns', 'repeat(auto-fit, minmax(' + Math.ceil(maxWidth) + 'px, 1fr))');
         }

         break;
      }

      case 9:          // Symbol-Value
      case 12: {       // Symbol-Text
         $(elem)
            .addClass("widgetSymbolValue widgetDropZone")
            .append($('<div></div>')
                    .addClass('widget-title ' + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : titleClass))
                    // .addClass(titleClass)
                    .click(function(event) {titleClick(event.ctrlKey, key);})
                    .css('user-select', 'none')
                    .html(title))
            .append($('<button></button>')
                    .attr('id', 'button' + fact.type + fact.address)
                    .addClass('widget-main')
                    .attr('type', 'button')
                    .css('color', widget.color)
                    .css('user-select', 'none')
                    .css('border-radius', '100%')
                    .append($('<img></img>')
                            .attr('id', 'widget' + fact.type + fact.address)
                            .attr('draggable', false)
                            .addClass('rounded-border')
                            .css('user-select', 'none'))
                    .click(function() {
                       function doToggle() { toggleIo(fact.address, fact.type); }
                       if (fact.settings && fact.settings.ask)
                          confirmDialog(doToggle, 'Wirklich schalten', 'Schalten', 'Abbrechen');
                       else
                          toggleIo(fact.address, fact.type);
                    }))
            .append($('<div></div>')
                    .attr('id', 'value' + fact.type + fact.address)
                    .addClass('symbol-value')
                    .css('user-select', 'none')
                   );

         break;
      }

      case 10: {   // space
         $(elem).addClass("widgetSpacer widgetDropZone");
         $(elem).append($('<div></div>')
                        .addClass('widget-title ' + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : ''))
                        .addClass(titleClass)
                        .css('font-weight', 'bold')
                        .click(function(event) {titleClick(event.ctrlKey, key);})
                        .html(setupMode ? ' spacer' : title));
         // without title invisible outside of the setup mode

         if (!setupMode && (isEmpty(title) || title.trim() == ''))
            $(elem).css('background-color', 'transparent');

         if (widget.linefeed) {
            elem.removeAttribute('data-colspan');   // whole row, not limited by resizeDashboard()
            $(elem)
               .css('grid-column', '1 / -1')
               .css('grid-row', 'span 1')
               .css('flex-basis', '100%')
               .css('height', setupMode || (!isEmpty(title) && title != ' ') ? '40px' : '0px')
               .css('padding', '0px')
               .css('align-content', 'center')
               .css('margin', '0px');
         }
         break;
      }

      case 18: {   // HTML (snippet of the table 'htmlwidgets', htmlwidgets.js)
         initHtmlWidget(key, widget, elem, title, titleClass);
         break;
      }

      case 11: {   // actual time
         $(elem)
            .addClass("widgetPlain widgetDropZone")
            .append($('<div></div>')
                    .addClass('widget-title ' + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : ''))
                    .addClass(titleClass)
                    .css('user-select', 'none')
                    .click(function(event) {titleClick(event.ctrlKey, key);})
                    .html(setupMode ? ' time' : ''));
         $(elem).append($('<div></div>')
                        .attr('id', 'widget' + key)
                        .addClass('widget-time')
                        .css('color', widget.color)
                        .css('user-select', 'none')
                        .html(getTimeHtml()));
         setInterval(function() {
            let timeId = '#widget'+key.replace(':', '\\:');
            $(timeId).html(getTimeHtml());
         }, 1*1000);

         break;
      }

      case 15: {      // Level
         initLevel(key, widget, fact);
         break;
      }

      case 16: {      // Balance
         initBalance(key, widget, fact);
         break;
      }

      case 14: {      // Special Symbol
         $(elem)
            .addClass("widget widgetDropZone")
            .append($('<div></div>')
                    .addClass('widget-title ' + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : ''))
                    .addClass(titleClass)
                    .css('user-select', 'none')
                    .click(function(event) {titleClick(event.ctrlKey, key);})
                    .html(title));

         let maxWidth = 280;
         let html = '';
         let yStart = 14;
         let heightStart = 170;
         let symbolWidth = maxWidth;
         let xPos = 10;

         if (widget.barwidth != null) {
            symbolWidth = widget.barwidth;
            xPos = (maxWidth - symbolWidth) / 2 + 10;
         }

         if (widget.symbol == null || widget.symbol == 0) {
            console.log("widget.symbol: '" + widget.symbol + "'");
            // plain rectangular tank symbol

            html = '<svg xmlns="http://www.w3.org/2000/svg" style="height:100%;aspect-ratio:1/1;" viewBox="0 0 300 200">' +
               // äußerer Rahmen
               '  <rect x="' + xPos + '"   y="10"    width="' + symbolWidth + '" rx="5" ry="5" height="180" fill="white" style="fill:white;stroke:black;stroke-width:5" />' +
               // untere permanente Füllung wegen der Rundung in widget Farbe
               '  <rect x="' + (xPos +0.5) + '" y="178.5" width="' + (symbolWidth - 3) + '" rx="4.5" height="10" fill="' + widget.color + '" />' +
               // Rest weiß - leer
               '  <rect x="' + (xPos +0.5) + '" y="14"    width="' + (symbolWidth - 3) + '" height="169" fill="rgb(255 255 255)" />' +
               // Füllung in widget Farbe
               '  <rect id="svg' + key + '" x="' + (xPos +0.5) + '" y="14" width="' + (symbolWidth - 3) + '" height="170" fill="' + widget.color + '" />' +
               // die Oberfläche mit 3D Effekt
               '  <ellipse id="svgEllipse' + key + '" rx="' + ((symbolWidth-6)/2) + '" ry="' + 8 + '" cx="' + (xPos + symbolWidth/2) + '" cy="100" stroke="' + widget.color + '" fill="' + alterColor(widget.color, 'lighten', 100) + '" style="stroke-width:2" />' +
               // Text
               '  <ellipse rx="40" ry="20" cx="' + (xPos + symbolWidth - 20) + '" cy="10" stroke="' + widget.color + '" style="fill:white;stroke-width:2" />' +
               '  <text id="value' + key + '" x="' + (xPos + symbolWidth - 20) + '" y="11" fill="black" alignment-baseline="middle" dominant-baseline="middle" text-anchor="middle" font-family="Helvetica,Arial,sans-serif" font-size="18">--</text>' +
               '</svg>';
         }

         else if (widget.symbol == 1) {
            // ...
         }

         $(elem).append($('<div></div>')
                        .attr('id', 'widget' + key)
                        .data('yStart', yStart)
                        .data('heightStart', heightStart)
                        .addClass('widget-main')
                        .html(html)
                        .click(function() {
                           let cFact = fact;
                           if (!setupMode && fact.record)
                              toggleChartDialog(cFact.type, cFact.address, key);})
                       );

         break;
      }

      default: {      // type 2 (Text)
         $(elem)
            .addClass("widget widgetDropZone")
            .append($('<div></div>')
                    .addClass('widget-title ' + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : ''))
                    .addClass(titleClass)
                    .css('user-select', 'none')
                    .click(function(event) {titleClick(event.ctrlKey, key);})
                    .html(title))
            .append($('<div></div>')
                    .attr('id', 'widget' + fact.type + fact.address)
                    .css('user-select', 'none')
                    .css('color', widget.color)
                    .addClass('widget-txt'));

         let cFact = fact;
         if (!setupMode && fact.record) {
            $(elem).click(function() {toggleChartDialog(cFact.type, cFact.address, key);});
            $(elem).css('cursor', 'pointer');
         }
         break;
      }
   }
}

//***************************************************************************
// Init WindyApp Widget
//***************************************************************************

function initWindy(key, widget, fact)
{
   // https://windy.app/de/widgets

   // Fields:
   //   wind-direction, wind-speed, wind-gust, air-temp,
   //   clouds, precipitation, waves-direction, waves-height,
   //   waves-period, tides, moon-phase

   let html = '';

   // by spot id

   if (fact.address == 2) {
      html = '<div ' +
         '   data-windywidget="forecast"' +
         '   data-customcss="windy-dark.css"' +
         '   data-thememode="dark"' +            // white|dark
         '   data-tempunit="C"' +                // C|F
         '   data-windunit="bft"' +              // knots|bft|m/s|mph|km/h
         '   data-heightunit="m"' +              // m|ft
         '   data-spotid="' + config.windyAppSpotID + '"' +
         '   data-fields="wind-speed,wind-gust,wind-direction,air-temp,clouds,precipitation"' +
         '   data-appid="' + config.windyAppID + '"' +
         ' >' +
         '</div>' +
         '<script async="true" data-cfasync="false" type="text/javascript"' +
         '        src="windy_forecast_async.js?v1.4.6"></script>';
   }

   // by gps

   if (fact.address == 4) {
      keyLat = toKey("GPS", 0x01);
      keyLong = toKey("GPS", 0x2);

      if (allSensors[keyLat] && allSensors[keyLong]) {
         html = '<div ' +
            '   data-windywidget="forecast"' +
            '   data-customcss="windy-dark.css"' +
            '   data-thememode="dark"' +            // white|dark
            '   data-tempunit="C"' +                // C|F
            '   data-windunit="bft"' +              // knots|bft|m/s|mph|km/h
            '   data-heightunit="m"' +              // m|ft
            '   data-lat="' + allSensors[keyLat].value + '"' +
            '   data-lng="' + allSensors[keyLong].value + '"' +
            '   data-fields="wind-speed,wind-gust,wind-direction,air-temp,clouds,precipitation"' +
            '   data-appid="' + config.windyAppID + '"' +
            ' >' +
            '</div>' +
            '<script async="true" data-cfasync="false" type="text/javascript"' +
            '        src="windy_forecast_async.js?v1.4.6"></script>';
      }
      else {
         html = '<div style="padding:20px;color:red;">No GPS Data available</div> ';
      }
   }

   let elem = document.getElementById('div_' + key);

   // special - use the whole width and self adjusted height

   $(elem)
      .css('height', '')
      .css('width', '')
      .append($('<div></div>')
              .html(html));
}

function initWindyMap(key, widget, fact)
{
   return;

   let html = '<div ' +
       '  data-windywidget="map"' +
       '  data-thememode="white"' +
       '  data-spotid="' + config.windyAppSpotID + '"' +
       '  data-appid="' + config.windyAppID + '"' +
       '</div>' +
       '<script async="true" data-cfasync="false" type="text/javascript" src="//windy.app/widget3/windy_map_async.js?v289"></script>';

   let elem = document.getElementById('div_' + key);

   // special - use the whole width and self adjusted height

   $(elem).css('height', '');
   $(elem).css('width', '');
   $(elem).html(html);
}

//***************************************************************************
// Peak Text (min / max)
//***************************************************************************

//  separator: between min and max (' ' or '<br>'), withUnit: append the unit
//  (to each line when separated by <br>, otherwise once at the end)

function peakHtml(widget, sensor, separator, withUnit)
{
   let unit = withUnit && widget.unit != '' ? ' ' + widget.unit : '';
   let parts = [];

   if (widget.showpeakmin && sensor.peakmin != null)
      parts.push('<span class="peak-min">\u25BC' + sensor.peakmin.toFixed(2) + (separator == '<br>' ? unit : '') + '</span>');

   if (widget.showpeak && sensor.peakmax != null)
      parts.push((widget.showpeakmin ? '\u25B2' : '') + sensor.peakmax.toFixed(2) + unit);
   else if (parts.length && separator != '<br>')
      parts[0] = parts[0].replace('</span>', unit + '</span>');

   return parts.join(separator);
}

//***************************************************************************
// Init Meter Widget
//***************************************************************************

// Level - MDI symbol filled by percentage from the bottom (tank, battery, drop), drawn on a canvas
// in three layers like the HASP 'Level' widget (hasp.c): solid glyph as dark body, solid glyph in the
// bar color clipped to the fill height, outline glyph as frame; MDI has most symbols as pair
// 'name' / 'name-outline'. The fill refers to the drawn height of the glyph (ink bounds measured
// on the canvas), with the option 'fillrect' a rectangle behind the symbol is filled instead
// (frame symbols without a filled variant, e.g. mdi-crop-landscape as a tank)

var mdiGlyphCache = {};
const mdiFontFamily = '"Material Design Icons"';

function mdiName(symbol)
{
   return (symbol || '').replace(/^mdi:/, '').replace(/^mdi-/, '');
}

function mdiGlyph(name)   // the character of the MDI symbol or null if unknown (the ::before content of its css class)
{
   if (!name)
      return null;

   if (name in mdiGlyphCache)
      return mdiGlyphCache[name];

   let probe = $('<span></span>').addClass('mdi mdi-' + name).css('position', 'absolute').css('visibility', 'hidden').appendTo(document.body);
   let content = getComputedStyle(probe[0], '::before').content;
   probe.remove();

   let glyph = (!content || content == 'none' || content == 'normal') ? null : content.replace(/^["']|["']$/g, '');
   mdiGlyphCache[name] = glyph;

   return glyph;
}

function initLevel(key, widget, fact)
{
   let elem = document.getElementById('div_' + key);
   let titleClass = getWidgetTitleClass(widget, fact);
   let title = getWidgetTitle(widget, fact);
   let id = fact.type + fact.address;
   let name = mdiName(widget.symbol);
   let solid = name;
   let outline = '';

   if (name.endsWith('-outline')) {
      outline = name;
      solid = name.slice(0, -8);
      if (!mdiGlyph(solid))
         solid = name;
   }
   else if (mdiGlyph(name + '-outline'))
      outline = name + '-outline';

   let rectFill = widget.fillrect == true;
   let scaleMax = parseFloat(widget.scalemax) || 0;
   let scaleMin = parseFloat(widget.scalemin) || 0;

   if (scaleMax <= scaleMin)
      scaleMax = scaleMin + 100;

   $(elem)
      .addClass('widgetLevel widgetDropZone')
      .append($('<div></div>')
              .addClass('widget-title ' + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : ''))
              .addClass(titleClass)
              .css('user-select', 'none')
              .click(function(event) {titleClick(event.ctrlKey, key);})
              .html(title));

   let cFact = fact;
   let main = $('<div></div>')
       .addClass('widget-main-level')
       .css('user-select', 'none')
       .css('cursor', !setupMode && fact.record ? 'pointer' : '')
       .click(function() { if (!setupMode && cFact.record) toggleChartDialog(cFact.type, cFact.address, key); });

   $(elem).append(main);

   // the symbol takes the height of the main area, at most half of the width; texts right of it

   let size = Math.max(Math.floor(Math.min(main.height() - 4, main.width() / 2)), 24);
   let dpr = window.devicePixelRatio || 1;
   let canvas = document.createElement('canvas');

   canvas.setAttribute('id', 'widget' + id);
   canvas.width = canvas.height = Math.round(size * dpr);
   canvas.style.width = canvas.style.height = size + 'px';

   main.append(canvas)
      .append($('<div></div>')
              .addClass('level-texts')
              .append($('<div></div>')
                      .attr('id', 'widgetValue' + id)
                      .addClass('level-value')
                      .html('--' + (widget.unit ? ' ' + widget.unit : '')))
              .append($('<div></div>')
                      .attr('id', 'widgetPct' + id)
                      .addClass('level-pct')
                      .html('-- %')));

   $(elem).data('level', {
      'glyphSolid' : mdiGlyph(solid) || mdiGlyph('help-circle-outline'),
      'glyphFrame' : rectFill ? (mdiGlyph(name) || mdiGlyph('help-circle-outline')) : (outline && outline != solid ? mdiGlyph(outline) : null),
      'rectFill' : rectFill,
      'min' : scaleMin, 'max' : scaleMax,
      'size' : Math.round(size * dpr),
      'fontPx' : Math.round((size - 4) * dpr),
      'colorFill' : widget.barcolor ? widget.barcolor : '#059eeb',
      'colorFrame' : widget.color && widget.color != 'white' ? widget.color : 'var(--fontColor)',
      'ink' : null,
      'pct' : null
   });

   document.fonts.load('10px ' + mdiFontFamily).then(function() { drawLevel(key, fact); });
}

function levelInkBounds(ctx, glyph, size, fontPx)   // the drawn area of the glyph (alpha scan), the glyph is centered in the square
{
   ctx.clearRect(0, 0, size, size);
   ctx.font = fontPx + 'px ' + mdiFontFamily;
   ctx.textAlign = 'center';
   ctx.textBaseline = 'middle';
   ctx.fillStyle = '#000';
   ctx.fillText(glyph, size / 2, size / 2);

   let data = ctx.getImageData(0, 0, size, size).data;
   let b = { 'top' : size, 'bottom' : 0, 'left' : size, 'right' : 0 };

   for (let y = 0; y < size; y++)
      for (let x = 0; x < size; x++)
         if (data[(y * size + x) * 4 + 3] > 40) {
            if (y < b.top) b.top = y;
            if (y + 1 > b.bottom) b.bottom = y + 1;
            if (x < b.left) b.left = x;
            if (x + 1 > b.right) b.right = x + 1;
         }

   if (b.bottom <= b.top)
      b = { 'top' : 0, 'bottom' : size, 'left' : 0, 'right' : size };

   return b;
}

function drawLevel(key, fact)
{
   let l = $('#div_' + key.replace(':', '\\:')).data('level');
   let canvas = document.getElementById('widget' + fact.type + fact.address);

   if (l == null || canvas == null)
      return;

   let ctx = canvas.getContext('2d', { willReadFrequently: true });
   let size = l.size;

   if (l.ink == null)
      l.ink = levelInkBounds(ctx, l.rectFill ? l.glyphFrame : l.glyphSolid, size, l.fontPx);

   let ink = l.ink;
   let fill = Math.round((ink.bottom - ink.top) * (l.pct == null ? 0 : l.pct));
   let bodyColor = 'rgba(128,128,128,0.35)';
   let frameColor = l.colorFrame.startsWith('var(') ? getComputedStyle(document.documentElement).getPropertyValue('--fontColor') || '#fff' : l.colorFrame;

   ctx.clearRect(0, 0, size, size);
   ctx.font = l.fontPx + 'px ' + mdiFontFamily;
   ctx.textAlign = 'center';
   ctx.textBaseline = 'middle';

   if (l.rectFill) {
      ctx.fillStyle = bodyColor;
      ctx.fillRect(ink.left, ink.top, ink.right - ink.left, ink.bottom - ink.top);
      ctx.fillStyle = l.colorFill;
      ctx.fillRect(ink.left, ink.bottom - fill, ink.right - ink.left, fill);
      ctx.fillStyle = frameColor;
      ctx.fillText(l.glyphFrame, size / 2, size / 2);
   }
   else {
      ctx.fillStyle = bodyColor;
      ctx.fillText(l.glyphSolid, size / 2, size / 2);
      ctx.save();
      ctx.beginPath();
      ctx.rect(0, ink.bottom - fill, size, fill);
      ctx.clip();
      ctx.fillStyle = l.colorFill;
      ctx.fillText(l.glyphSolid, size / 2, size / 2);
      ctx.restore();

      if (l.glyphFrame) {
         ctx.fillStyle = '#707070';
         ctx.fillText(l.glyphFrame, size / 2, size / 2);
      }
   }
}

function updateLevel(sensor, widget, fact)
{
   let id = fact.type + fact.address;
   let key = toKey(fact.type, fact.address);
   let l = $('#div_' + key.replace(':', '\\:')).data('level');

   if (l == null || sensor.value == null)
      return;

   let pct = Math.min(Math.max((sensor.value - l.min) / (l.max - l.min), 0), 1);

   l.pct = pct;
   $('#widgetValue' + id).html(balanceFormat(sensor.value) + (widget.unit ? ' ' + widget.unit : ''));
   $('#widgetPct' + id).html(Math.round(pct * 100) + ' %');

   document.fonts.load('10px ' + mdiFontFamily).then(function() { drawLevel(key, fact); });
}

// Balance - horizontal bar with zero inside for bipolar values (battery current, grid power),
// negative values fill from zero to the left in widget.color, positive to the right in widget.colorOn;
// same options and look as the HASP 'Balance' widget (hasp.c): value above the bar, 5 scale marks below

function balanceFormat(value)
{
   return (Math.abs(value - Math.round(value)) < 0.05 || Math.abs(value) >= 1000) ? value.toFixed(0) : value.toFixed(1);
}

function initBalance(key, widget, fact)
{
   let elem = document.getElementById('div_' + key);
   let titleClass = getWidgetTitleClass(widget, fact);
   let title = getWidgetTitle(widget, fact);
   let id = fact.type + fact.address;
   let scaleMax = parseFloat(widget.scalemax) || 0;
   let scaleMin = parseFloat(widget.scalemin);

   if (scaleMax <= 0)                   // zero must be inside the range
      scaleMax = 100;

   if (isNaN(scaleMin) || scaleMin >= scaleMax)    // only if not set (or invalid) default to symmetric
      scaleMin = -scaleMax;

   // crit limits: marked as line in the bar, -1 / unset = off

   let critMin = (widget.critmin == null || widget.critmin == -1 || isNaN(parseFloat(widget.critmin))) ? null : parseFloat(widget.critmin);
   let critMax = (widget.critmax == null || widget.critmax == -1 || isNaN(parseFloat(widget.critmax))) ? null : parseFloat(widget.critmax);

   // the dashboard defaults an unset color to 'white' (initWidget), the dialog sets red / green on choosing the type

   let colorNeg = widget.color && widget.color != 'white' ? widget.color : '#ff5757';
   let colorPos = widget.colorOn ? widget.colorOn : '#4caf50';
   let pctOf = function(v) { return (v - scaleMin) / (scaleMax - scaleMin) * 100; };
   let zeroPct = Math.min(100, Math.max(0, pctOf(0)));      // fill starts at the edge if zero is outside the range

   // bar thickness like the level meter ('Balkenbreite'); 7 is the dialog default of other types, treated as unset

   let barWidth = parseInt(widget.barwidth);
   if (isNaN(barWidth) || barWidth == 7)
      barWidth = 22;
   barWidth = Math.min(Math.max(barWidth, 8), 60);

   let eScale = $('<div></div>').addClass('balance-scale');
   // zero inside the range: min, min/2, 0, max/2, max - otherwise only min and max (the halves would be
   // outside of or arbitrary within the bar); the crit values are shown at their line instead of close marks

   let marks = scaleMin < 0 && scaleMax > 0 ? [scaleMin, scaleMin / 2, 0, scaleMax / 2, scaleMax] : [scaleMin, scaleMax];
   let crits = [critMin, critMax].filter(function(c) { return c != null && c > scaleMin && c < scaleMax; });

   marks = marks.filter(function(m) {
      return !crits.some(function(c) { return Math.abs(pctOf(c) - pctOf(m)) < 12; });
   });

   for (let i = 0; i < marks.length; i++)
      eScale.append($('<span></span>')
                    .addClass('balance-mark')
                    .css('left', pctOf(marks[i]) + '%')
                    .css('color', marks[i] < 0 ? colorNeg : marks[i] > 0 ? colorPos : '')
                    .html(balanceFormat(marks[i])));

   for (let i = 0; i < crits.length; i++)
      eScale.append($('<span></span>')
                    .addClass('balance-mark')
                    .css('left', pctOf(crits[i]) + '%')
                    .css('color', '#ff3b30')
                    .html(balanceFormat(crits[i])));

   let cFact = fact;
   let chart = function() { if (!setupMode && cFact.record) toggleChartDialog(cFact.type, cFact.address, key); };

   $(elem)
      .addClass('widgetBalance widgetDropZone')
      .data('balance', { 'min' : scaleMin, 'max' : scaleMax, 'zero' : zeroPct,
                         'colorNeg' : colorNeg, 'colorPos' : colorPos, 'invert' : widget.invert == true })
      .append($('<div></div>')
              .addClass('widget-title ' + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : ''))
              .addClass(titleClass)
              .css('user-select', 'none')
              .click(function(event) {titleClick(event.ctrlKey, key);})
              .html(title))
      .append($('<div></div>')
              .addClass('widget-main-balance')
              .css('user-select', 'none')
              .css('cursor', !setupMode && fact.record ? 'pointer' : '')
              .click(chart)
              .append($('<div></div>')
                      .attr('id', 'widgetValue' + id)
                      .addClass('balance-value')
                      .html('--' + (widget.unit ? ' ' + widget.unit : '')))
              .append($('<div></div>')
                      .addClass('balance-bar')
                      .css('--bh', barWidth + 'px')
                      .append($('<div></div>')
                              .addClass('balance-trough')
                              .append($('<div></div>')
                                      .attr('id', 'widget' + id)
                                      .addClass('balance-fill')
                                      .css('left', zeroPct + '%')))
                      .append(scaleMin < 0 && scaleMax > 0 ? $('<div></div>')
                              .addClass('balance-zero')
                              .css('left', zeroPct + '%') : null)
                      .append(critMin == null ? null : $('<div></div>').addClass('balance-crit').css('left', pctOf(critMin) + '%'))
                      .append(critMax == null ? null : $('<div></div>').addClass('balance-crit').css('left', pctOf(critMax) + '%')))
              .append(eScale));
}

function updateBalance(sensor, widget, fact)
{
   let id = fact.type + fact.address;
   let b = $('#div_' + toKey(fact.type, fact.address).replace(':', '\\:')).data('balance');

   if (b == null || sensor.value == null)
      return;

   let v = b.invert ? -sensor.value : sensor.value;
   let pct = (Math.min(Math.max(v, b.min), b.max) - b.min) / (b.max - b.min) * 100;
   let color = v < 0 ? b.colorNeg : b.colorPos;

   $('#widget' + id)
      .toggleClass('neg', v < 0)
      .css('left', Math.min(pct, b.zero) + '%')
      .css('width', Math.abs(pct - b.zero) + '%')
      .css('background-color', color);

   $('#widgetValue' + id)
      .html(balanceFormat(sensor.value) + (widget.unit ? ' ' + widget.unit : ''))
      .css('color', sensor.value == 0 ? '' : color);
}

function initMeter(key, widget, fact, neededScaleMax, value)
{
   let radial = widget.widgettype == 5;
   let showValue = widget.showvalue == null || widget.showvalue == true;
   let titleClass = getWidgetTitleClass(widget, fact);
   let title = getWidgetTitle(widget, fact);
   let elem = document.getElementById('div_' + key);

   if (!elem) {
      console.log('element not found', 'div_' + key);
      return;
   }

   $(elem).empty();

   if (radial || !showValue)
      elem.className = 'widgetMeter widgetDropZone';
   else if (showValue)
      elem.className = 'widgetMeterLinear widgetDropZone';

   let eTitle = document.createElement("div");
   eTitle.className = "widget-title " + (setupMode ? 'mdi mdi-lead-pencil widget-edit' : titleClass);
   eTitle.addEventListener("click", function(event) {titleClick(event.ctrlKey, key)}, false);
   eTitle.innerHTML = title;
   elem.appendChild(eTitle);

   let main = document.createElement("div");
   main.className = "widget-main-meter";
   elem.appendChild(main);

   let canvas = document.createElement('canvas');
   canvas.setAttribute('id', 'widget' + fact.type + fact.address);
   let showPeak = widget.showpeak || widget.showpeakmin;

   if (radial && showPeak) {
      // no separate grid cell for the peak - min/max line above the gauge, the gauge moves down
      main.className += ' with-peak';
      let ePeak = document.createElement('div');
      ePeak.setAttribute('id', 'peak' + fact.type + fact.address);
      ePeak.className = 'widget-meter-peak-top';
      main.appendChild(ePeak);
      main.appendChild(canvas);
   }
   else if (!showValue && showPeak) {
      // linear gauge without value - overlay min/max on the gauge (title area)
      let wrap = document.createElement('div');
      wrap.className = 'widget-meter-canvas';
      wrap.appendChild(canvas);
      let ePeak = document.createElement('div');
      ePeak.setAttribute('id', 'peak' + fact.type + fact.address);
      ePeak.className = 'widget-meter-peak';
      wrap.appendChild(ePeak);
      main.appendChild(wrap);
   }
   else
      main.appendChild(canvas);

   let cFact = fact;
   if (!setupMode && fact.record) {
      $(canvas).click(function() {toggleChartDialog(cFact.type, cFact.address, key);});
      canvas.style.cursor = 'pointer';
   }

   if (!radial && showValue) {
      let _value = document.createElement("div");
      _value.setAttribute('id', 'widgetValue' + fact.type + fact.address);
      _value.className = "widget-main-value-lin";
      elem.appendChild(_value);
      $("#widgetValue" + fact.type + fact.address).css('color', widget.color);
      let ePeak = document.createElement("div");
      ePeak.setAttribute('id', 'peak' + fact.type + fact.address);
      ePeak.className = "widget-main-peak-lin";
      elem.appendChild(ePeak);
   }

   if (widget.scalemin == null)
      widget.scalemin = 0;

   let neededScaleMin = widget.scalemin;

//   if (neededScaleMin != 0)
//      console.log(neededScaleMin);

//   if (widget.scalemin < 0)
//      neededScaleMin = neededScaleMax*-1;

   let ticks = [];
   let scaleRange = neededScaleMax - neededScaleMin;
   let stepWidth = widget.scalestep;

   if (widget.rescale && widget.scalestep != null && widget.scalemax != neededScaleMax) {
      let factor = widget.scalemax / neededScaleMax;
      stepWidth = widget.scalestep / factor;

      if (neededScaleMax >= 10)
         stepWidth = Math.round(stepWidth);
   }

   if (!stepWidth) {
      let steps = 10;
      if (scaleRange <= 100)
         steps = scaleRange % 10 == 0 ? 10 : scaleRange % 5 == 0 ? 5 : scaleRange;
      if (steps < 10)
         steps = 10;
      if (!radial && widget.unit == '%')
         steps = 4;
      stepWidth = scaleRange / steps;
   }

   if (stepWidth <= 0)
      stepWidth = 1;

   stepWidth = Math.round(stepWidth*10) / 10;

   if (neededScaleMax < widget.scalemax)
      neededScaleMax += stepWidth;       // if we auto scale add one step

//   if (widget.scalemin < 0)
//      neededScaleMin = neededScaleMax*-1;

   let steps = -1;
   for (let step = neededScaleMin; step.toFixed(2) <= neededScaleMax; step += stepWidth) {
      ticks.push(step % 1 ? parseFloat(step).toFixed(1) : parseInt(step));
      steps++;
   }

   let highlights = {};
   let critmin = (widget.critmin == null || widget.critmin == -1) ? neededScaleMin : widget.critmin;
   let critmax = (widget.critmax == null || widget.critmax == -1) ? neededScaleMax : widget.critmax;
   let minColor = widget.unit == '°C' ? 'blue' : 'rgba(255,0,0,.6)';

   highlights = [
      { 'from': neededScaleMin, 'to': critmin,        'color': minColor },
      { 'from': critmin,        'to': critmax,        'color': 'rgba(0,255,0,.6)' },
      { 'from': critmax,        'to': neededScaleMax, 'color': 'rgba(255,0,0,.6)' }
   ];

   let bWidth = radial ? 0 : (widget.unit == '%' || widget.unit == 'l' ? 12 : 7);

   if (!radial && widget.barwidth != null)      // the radial meter has no setting for the bar width
      bWidth = widget.barwidth;

   let progressColor = widget.unit == '%' ? 'blue' : 'red';

   if (widget.barcolor != null && widget.barcolor != '')
      progressColor = widget.barcolor

   progressColor = getWidgetColor(widget, widget.colorConditionBar, progressColor, value);

   options = {
      renderTo: 'widget' + fact.type + fact.address,

      units: radial ? widget.unit : '',
      minValue: neededScaleMin,
      maxValue: neededScaleMax,
      majorTicks: ticks,
      minorTicks: 5,
      highlights: highlights,
      strokeTicks: false,
      highlightsWidth: radial ? 8 : 6,

      colorPlate: radial ? '#2177AD' : 'rgba(0,0,0,0)',
      colorBar: colorStyle.getPropertyValue('--scale'),        // 'gray',
      colorBarProgress: progressColor,
      colorBarStroke: 'red',
      colorMajorTicks: colorStyle.getPropertyValue('--scale'), // '#f5f5f5',
      colorMinorTicks: colorStyle.getPropertyValue('--scale'), // '#ddd',
      colorTitle: colorStyle.getPropertyValue('--scaleText'),
      colorUnits: colorStyle.getPropertyValue('--scaleText'),  // '#ccc',
      colorNumbers: colorStyle.getPropertyValue('--scaleText'),
      colorNeedle: 'rgba(240, 128, 128, 1)',
      colorNeedleEnd: 'rgba(255, 160, 122, .9)',

      fontNumbersSize: radial ? 34 : (showValue ? 40 : 30),
      title: !radial && !showValue && showPeak ? ' ' : false,   // reserve space for the peak overlay
      fontUnitsSize: 45,
      fontUnitsWeight: 'bold',
      borderOuterWidth: 0,
      borderMiddleWidth: 0,
      borderInnerWidth: 0,
      borderShadowWidth: 0,
      fontValueWeight: 'bold',
      valueBox: radial && showValue,
      valueInt: 0,
      valueDec: widget.unit == '%' ? 0 : 2,
      colorValueText: colorStyle.getPropertyValue('--scale'),
      colorValueBoxBackground: 'transparent',
      valueBoxStroke: 0,
      fontValueSize: 45,
      valueTextShadow: false,
      animationRule: 'bounce',
      animationDuration: 500,
      barWidth: bWidth,
      numbersMargin: -3,
      needle: radial,

      // linear gauge specials

      barBeginCircle: widget.unit == '°C' ? 15 : 0,

      tickSide: 'left',
      numberSide: 'left',
      needleSide: 'left',
      colorPlate: 'transparent'
   };

   // patch width and height due to strange behavior of (at least) the LinearGauge when wider than high

   if (radial && showPeak) {
      // the min/max line above takes height from the gauge - size the canvas to the remaining square
      let size = Math.min($(main).innerWidth(), $(main).innerHeight() - $('#peak' + fact.type + fact.address).outerHeight(true));
      options.width = options.height = Math.max(size, 50);
   }
   else if ($(main).innerHeight() < $(main).innerWidth())
      options.width = options.hight = $(main).innerHeight();

   if (dashboardGauges[fact.type + fact.address] != null) {
      dashboardGauges[fact.type + fact.address].destroy();
      dashboardGauges[fact.type + fact.address] = null;
   }

   if (radial)
      dashboardGauges[fact.type + fact.address] = new RadialGauge(options);
   else
      dashboardGauges[fact.type + fact.address] = new LinearGauge(options);

   dashboardGauges[fact.type + fact.address].draw();
}

function initLightColorDialog()
{
   // mode 'Farbe' (color wheel, RGB) | 'Weiß' (color temperature, the own white LEDs of RGB+CCT lights),
   //   the mode selection only for lights with a white mode (ctmin / ctmax of the daemon)

   $("#container").append($('<div></div>)')
                          .attr('id', 'lightColorDiv')
                          .addClass('rounded-border lightColorDiv')
                          .append($('<div></div>').attr('id', 'lightModeRow').addClass('lightModeRow')
                                  .append($('<label></label>')
                                          .append($('<input></input>').attr('type', 'radio').attr('name', 'lightMode').attr('value', 'color'))
                                          .append($('<span></span>').text('Farbe')))
                                  .append($('<label></label>')
                                          .append($('<input></input>').attr('type', 'radio').attr('name', 'lightMode').attr('value', 'white'))
                                          .append($('<span></span>').text('Weiß'))))
                          .append($('<div></div>').attr('id', 'lightColorPane')
                                  .append($('<input></input>)')
                                          .attr('id', 'lightColor')
                                          .addClass('lightColor')
                                          .attr('type', 'text')))
                          .append($('<div></div>').attr('id', 'lightWhitePane').addClass('lightWhitePane')
                                  .append($('<div></div>').addClass('lightDimRow')
                                          .append($('<span></span>').addClass('lightWhiteLabel').text('Warm'))
                                          .append($('<input></input>').attr('id', 'lightCt').attr('type', 'range').addClass('lightDim lightCt'))
                                          .append($('<span></span>').addClass('lightWhiteLabel').text('Kalt'))
                                          .append($('<span></span>').attr('id', 'lightCtValue').addClass('lightDimValue')))
                                  .append($('<div></div>').addClass('lightDimRow')
                                          .append($('<span></span>').addClass('mdi mdi-brightness-6 lightWhiteLabel'))
                                          .append($('<input></input>').attr('id', 'lightWhiteBri').attr('type', 'range')
                                                  .attr('min', '1').attr('max', '100').attr('step', '1').addClass('lightDim'))
                                          .append($('<span></span>').attr('id', 'lightWhiteBriValue').addClass('lightDimValue'))))
                          .append($('<button></button>)')
                                  .html('Ok')
                                  .click(function() { $('#lightColorDiv').css('display', 'none'); }))
                         );

   let options = {
      'cssClass' : 'lightColor',
      'layout' :  'block',
      'format' : 'hsv',
      'sliders' : 'wsvp',
      'autoResize' : false
   }

   $('#lightColor').wheelColorPicker(options);
   $('#lightColorDiv').css('display', 'none');

   // white mode: the slider runs from warm (left, high mired) to cold (right), its value is -mired

   $('#lightCt').on('input', function() {
      $('#lightCtValue').text(miredToKelvin(-$(this).val()) + ' K');
   });

   $('#lightWhiteBri').on('input', function() {
      $('#lightWhiteBriValue').text($(this).val() + ' %');
   });

   $('#lightCt, #lightWhiteBri').on('change', function() { sendLightWhite(); });   // on release

   $('input[name="lightMode"]').on('change', function() {
      showLightMode(this.value);

      if (this.value == 'white')
         sendLightWhite();
      else
         $('#lightColor').trigger('sliderup');    // the color of the wheel
   });

   $('#lightColor').on('sliderup', function(e) {
      if (!$(this).data('key'))
         return;

      let fact = valueFacts[$(this).data('key')];
      let hue = parseInt($(this).wheelColorPicker('getColor').h * 360);
      let sat = parseInt($(this).wheelColorPicker('getColor').s * 100);
      let bri = parseInt($(this).wheelColorPicker('getColor').v * 100);
      let obj = { 'action': 'color', 'type': fact.type, 'address': fact.address, 'hue': hue, 'saturation' : sat };

      // the brightness only if changed in the dialog - the value the dialog started with may be an
      //  intermediate one of a transition (the light reports them while dimming), sent it set the light dark

      if (bri != $(this).data('bri'))
         obj.bri = bri;

      console.log("color of " + fact.address + " changed to '" + hue + "' / " + bri + '% / ' + sat + '%');
      socket.send({ "event": "toggleio", "object": obj });
   });


   /*
     das stört den 'number' Input - z.B. Range auf der Chart Ansicht !!

     $('#container').on('mouseup', function(e) {
      e.preventDefault();
      if ($(e.target).attr('id') != 'lightColorDiv' && !$('#lightColorDiv').has(e.target).length)
         $('#lightColorDiv').css('display', 'none');
   }); */
}

// brightness dialog of the dimmable lights without color

function initLightDimDialog()
{
   $("#container").append($('<div></div>')
                          .attr('id', 'lightDimDiv')
                          .addClass('rounded-border lightColorDiv lightDimDiv')
                          .append($('<div></div>').attr('id', 'lightDimTitle').addClass('lightDimTitle'))
                          .append($('<div></div>').addClass('lightDimRow')
                                  .append($('<input></input>')
                                          .attr('id', 'lightDim')
                                          .attr('type', 'range').attr('min', '1').attr('max', '100').attr('step', '1')
                                          .addClass('lightDim'))
                                  .append($('<span></span>').attr('id', 'lightDimValue').addClass('lightDimValue')))
                          .append($('<button></button>')
                                  .html('Ok')
                                  .click(function() { $('#lightDimDiv').css('display', 'none'); }))
                         );

   $('#lightDimDiv').css('display', 'none');

   // ESC or a click / touch beside it closes the color / brightness dialog; only while it is open
   //   (a general handler disturbed other inputs, e.g. the range of the chart view), not just after
   //   the opening (the emulated mouse events following the long press of a touch)

   $(document).on('keydown', function(e) {
      if (e.key == 'Escape' && lightDialogOpen())
         closeLightDialogs();
   });

   $(document).on('mousedown touchstart', function(e) {
      if (!lightDialogOpen() || Date.now() - lightDialogOpenedAt < 600)
         return;
      if ($(e.target).closest('#lightColorDiv, #lightDimDiv').length)
         return;
      closeLightDialogs();
   });

   $('#lightDim').on('input', function() {
      $('#lightDimValue').text($(this).val() + ' %');
   });

   $('#lightDim').on('change', function() {      // on release of the slider
      let fact = valueFacts[$(this).data('key')];

      if (fact)
         toggleIo(fact.address, fact.type, parseInt($(this).val()));
   });
}

function showLightDimDialog(key)
{
   let sensor = allSensors[key] || {};
   let bri = sensor.bri != null ? sensor.bri : sensor.score != null ? sensor.score : 100;

   $('#lightDim').data('key', key).val(bri);
   $('#lightDimValue').text(bri + ' %');
   $('#lightDimTitle').text(factTitle(key));
   $('#lightDimDiv').css('display', 'block');
   lightDialogOpenedAt = Date.now();

   let posX = ($('#container').innerWidth() - $('#lightDimDiv').outerWidth()) / 2;
   let posY = ($('#container').innerHeight() - $('#lightDimDiv').outerHeight()) / 2;

   $('#lightDimDiv').css('left', posX + 'px');
   $('#lightDimDiv').css('top', posY + 'px');
}

var lightDialogOpenedAt = 0;
var lightTouchAt = 0;
var lightToggledAt = {};

// touch devices send emulated mouse events after a touch (mousedown / mouseup / mouseleave), not always
//   suppressed by preventDefault() of the touchend (e.g. if the browser took the touch as start of a
//   scroll) - they toggled the light a second time (seemed not to react or switched twice);
//   mouse events up to 1 s after a touch are ignored

function lightEmulatedMouse(e)
{
   if (e.type.startsWith('touch')) {
      lightTouchAt = Date.now();
      return false;
   }

   return Date.now() - lightTouchAt < 1000;
}

// a second toggle of the same light within 600 ms is a double event, not a second tap

function lightToggledJustNow(key)
{
   let now = Date.now();
   let just = lightToggledAt[key] && now - lightToggledAt[key] < 600;

   lightToggledAt[key] = now;
   return just;
}

function closeLightDialogs()
{
   $('#lightColorDiv').css('display', 'none');
   $('#lightDimDiv').css('display', 'none');
}

function lightDialogOpen()
{
   return $('#lightColorDiv').css('display') != 'none' || $('#lightDimDiv').css('display') != 'none';
}

function miredToKelvin(mired)
{
   return mired > 0 ? Math.round(1000000 / mired / 50) * 50 : 0;
}

function showLightMode(mode)
{
   $('#lightColorPane').css('display', mode == 'white' ? 'none' : 'block');
   $('#lightWhitePane').css('display', mode == 'white' ? 'block' : 'none');
}

function sendLightWhite()
{
   let fact = valueFacts[$('#lightColor').data('key')];

   if (!fact)
      return;

   let obj = { 'action': 'ct', 'type': fact.type, 'address': fact.address, 'ct': -parseInt($('#lightCt').val()) };
   let bri = parseInt($('#lightWhiteBri').val());

   if (bri != $('#lightWhiteBri').data('start'))     // the brightness only if changed (see the color wheel)
      obj.bri = bri;

   socket.send({ "event": "toggleio", "object": obj });
}

function showLightColorDialog(key)
{
   let sensor = allSensors[key];
   let hasWhite = sensor.ctmin > 0 && sensor.ctmax > sensor.ctmin;
   let mode = hasWhite && sensor.colormode == 'ct' ? 'white' : 'color';
   let bri = sensor.score != null ? sensor.score : 100;
   let ct = sensor.ct >= sensor.ctmin && sensor.ct <= sensor.ctmax ? sensor.ct : Math.round((sensor.ctmin + sensor.ctmax) / 2);

   $('#lightColor').data('key', key);
   $('#lightModeRow').css('display', hasWhite ? 'flex' : 'none');
   $('input[name="lightMode"][value="' + mode + '"]').prop('checked', true);
   showLightMode(mode);

   if (hasWhite) {
      $('#lightCt').attr('min', -sensor.ctmax).attr('max', -sensor.ctmin).attr('step', '1').val(-ct);
      $('#lightCtValue').text(miredToKelvin(ct) + ' K');
      $('#lightWhiteBri').val(Math.max(bri, 1)).data('start', Math.max(bri, 1));
      $('#lightWhiteBriValue').text(Math.max(bri, 1) + ' %');
   }

   $('#lightColorDiv').css('display', 'block');
   lightDialogOpenedAt = Date.now();
   $('#lightColor').wheelColorPicker('setColor', { 'h': sensor.hue/360.0, 's': sensor.sat/100.0, 'v': sensor.score/100.0 });
   $('#lightColor').data('bri', parseInt($('#lightColor').wheelColorPicker('getColor').v * 100));

   let posX = ($('#container').innerWidth() - $('#lightColorDiv').outerWidth()) / 2;
   let posY = ($('#container').innerHeight() - $('#lightColorDiv').outerHeight()) / 2;

   $('#lightColorDiv').css('left', posX + 'px');
   $('#lightColorDiv').css('top', posY + 'px');
}

function getTimeHtml()
{
   let now = new Date();

   // calc daytime every 60 seconds

   if (!daytimeCalcAt || now.getTime() >= daytimeCalcAt.getTime()+60000){
      daytimeCalcAt = now;
      let sun = sunTimes();     // of the daemon (position of the config or the GPS)

      if (sun) {
         let t = now.getTime() / 1000;
         isDaytime = sun.sunrise && sun.sunset ? t > sun.sunrise && t < sun.sunset : sun.sunUp;
      }
      else {
         let sunset = new Date().sunset(config.latitude.replace(',', '.'), config.longitude.replace(',', '.'));
         let sunrise = new Date().sunrise(config.latitude.replace(',', '.'), config.longitude.replace(',', '.'));
         isDaytime = daytimeCalcAt > sunrise && daytimeCalcAt < sunset;
      }
      // console.log("isDaytime: " + isDaytime + ' : ' + sunrise + ' : '+ sunset);
   }

   let cls = isDaytime ? 'mdi mdi-weather-sunny' : 'mdi mdi-weather-night';
   let date = new Date();

   return '<div>'
      + '<span class="' + cls + ' " style="color:orange;"></span>'
      + '<span> ' + fmtWeekdayDay(date) + '</span>'
      + '<span> ' + fmtMonthYear(date) + '</span>'
      + '</div>'
      + '<div style="font-size:2em">' + fmtTime(date) + '</div>';
}

function getWeatherHtml(symbolView, wfact, weather)
{
   // https://openweathermap.org/weather-conditions#Weather-Condition-Codes-2

   let html = '';

   if (symbolView) {
      let wIconRef = 'img/weather/' + weather.icon + '@4x.png';
      if (!images.find(img => img == wIconRef))
         wIconRef = 'http://openweathermap.org/img/wn/' + weather.icon + '@4x.png';
      html += '<div style="display:block;font-weight:bold;height:75%;padding-left:5px;"><span>' + weather.detail + '</span><img style="height:100%" src="' + wIconRef + '"></img></div>';
      html += '<div style="display:flex;justify-content:space-between;padding-top:3px;">';
      html += ' <span style="color:orange;font-weight:bold;padding-left:5px;">' + weather.temp.toFixed(1) + '°C</span>';
      html += ' <span class="mdi mdi-walk" style="color:orange;font-weight:bold;font-size:smaller;">' + weather.tempfeels.toFixed(1) + '</span>';
      html += '</div>';

   }
   else {
      let wIconRef = 'img/weather/' + weather.icon + '.png';
      if (!images.find(img => img == wIconRef))
         wIconRef = 'http://openweathermap.org/img/wn/' + weather.icon + '.png';
      html += '<div style="display:inline-flex;align-items:center;font-weight:bold;"><span><img src="' + wIconRef + '"></img></span><span>' + weather.detail + '</span></div>';
      html += '<div><span style="color:orange;font-weight:bold;">' + weather.temp.toFixed(1) + '°C</span>';
      html += '<span style="font-size:smaller;"> (' + weather.tempmin.toFixed(1) + ' / ' + weather.tempmax.toFixed(1) + ')' + '</span></div>';
      html += '<div>Luftdruck: <span style="color:orange;font-weight:bold;">' + weather.pressure + ' hPa' + '</span></div>';
      html += '<div>Luftfeuchte: <span style="color:orange;font-weight:bold;">' + weather.humidity + ' %' + '</span></div>';
      html += '<div>Wind: <span style="color:orange;font-weight:bold;">' + weather.windspeed + ' m/s' + '</span></div>';
   }

   return html;
}

function weatherForecast()
{
   let lastDay = '';
   let form = document.createElement("div");

   $(form).addClass('rounded-border weatherForecast');
   $(form).attr('tabindex', 0);
   $(form).click(function() { $(form).remove(); });
   $('#container').append(form);

   let showExtras = $(form).outerWidth() > 580;
   let html = '<div class="rounded-border" style="justify-content:center;font-weight:bold;background-color:#2f2f2fd1;">' + weatherData.city + '</div>';

   for (let i = 0; i < weatherData.forecasts.length; i++) {
      let weather = weatherData.forecasts[i];
      let day = fmtWeekdayDayMonth(new Date(weather.time*1000));
      let time = fmtHour(new Date(weather.time*1000));
      let wIconRef = 'img/weather/' + weather.icon + '.png';

      if (!images.find(img => img == wIconRef))
         wIconRef = 'http://openweathermap.org/img/wn/' + weather.icon + '.png';

      if (day != lastDay) {
         lastDay = day;
         html += '<div class="rounded-border" style="background-color:#2f2f2fd1;">' + day + '</div>';
      }

      let tempColor = weather.temp < 0 ? '#2c99eb' : (weather.temp > 20 ? 'red' : 'white');

      html += '<div class="rounded-border">';
      html += '<span>' + time + '</span>';
      html += '<span><img src="' + wIconRef + '"></img></span>';
      html += '<span class="mdi mdi-thermometer" style="color:' + tempColor + ';"> ' + weather.temp + ' °C</span>';
      if (showExtras)
         html += '<span class="mdi mdi-walk">' + weather.tempfeels + ' °C</span>';
      html += '<span class="mdi mdi-water-percent"> ' + weather.humidity + ' %</span>';
      html += '<span>' + weather.pressure + ' hPa</span>';
      html += '<span class="mdi mdi-weather-windy"> ' + weather.windspeed + ' m/s</span>';

      html += '</div>'
   }

   html += '</div>'

   $(form).html(html);
   $(form).focus();
   $(form).keydown(function(event) {
      if (event.which == 27 || event.which == 13) {
         event.preventDefault();
         $(form).remove();
      }
   });
}

// ---------------------------------------------------------------------------
// long touch = Ctrl+click for touch devices (no Ctrl key on the phone)
//   - widget title (resp. the whole HTML widget) -> titleClick(true) (widget setup / mode toggle)
//   - dashboard button -> dashboard setup
//   one delegated handler on the document (the widgets are re-created), only outside the setup mode
//   (there the pencils exist); the click following the long touch is suppressed
// ---------------------------------------------------------------------------

var longTouch = { 'timer' : null, 'x' : 0, 'y' : 0, 'firedAt' : 0, 'firedTouch' : false };

function longTouchTarget(el)
{
   if (setupMode)
      return null;

   let dash = $(el).closest('#dashboardMenu .buttonDashboard[id^="dash"]');

   if (dash.length) {
      if (!(localStorage.getItem(storagePrefix + 'Rights') & 0x04))
         return null;
      let id = dash.attr('id');
      return function() { dashboardSetup(id); };
   }

   if (!$(el).closest('.widget-title, .widgetHtml').length)
      return null;

   let div = $(el).closest('[id^="div_"]');

   if (!div.length)
      return null;

   let key = div.attr('id').substring(4);
   return function() { titleClick(true, key); };
}

function longTouchCancel()
{
   if (longTouch.timer)
      clearTimeout(longTouch.timer);

   longTouch.timer = null;
}

function initLongTouch()
{
   if (initLongTouch.done)
      return;

   initLongTouch.done = true;

   document.addEventListener('touchstart', function(e) {
      longTouchCancel();
      longTouch.firedTouch = false;

      if (e.touches.length != 1)
         return;

      let action = longTouchTarget(e.target);

      if (!action)
         return;

      longTouch.x = e.touches[0].pageX;
      longTouch.y = e.touches[0].pageY;
      longTouch.timer = setTimeout(function() {
         longTouch.timer = null;
         longTouch.firedAt = Date.now();
         longTouch.firedTouch = true;
         action();
      }, 600);
   }, { passive: true });

   document.addEventListener('touchmove', function(e) {
      // moved (e.g. scrolling) -> no long touch
      if (longTouch.timer && (Math.abs(e.touches[0].pageX - longTouch.x) >= 15 ||
                              Math.abs(e.touches[0].pageY - longTouch.y) >= 15))
         longTouchCancel();
   }, { passive: true });

   document.addEventListener('touchend', function(e) {
      longTouchCancel();
      if (longTouch.firedTouch && e.cancelable)
         e.preventDefault();             // no click after the long touch
      longTouch.firedTouch = false;
   }, { passive: false });

   document.addEventListener('touchcancel', function(e) {
      longTouchCancel();
      longTouch.firedTouch = false;
   }, { passive: true });

   // fallback: suppress the (emulated) click resp. the context menu just after a long touch

   document.addEventListener('click', function(e) {
      if (Date.now() - longTouch.firedAt < 800) {
         e.preventDefault();
         e.stopPropagation();
      }
   }, true);

   document.addEventListener('contextmenu', function(e) {
      if (longTouch.timer || Date.now() - longTouch.firedAt < 800)
         e.preventDefault();
   }, true);
}

function titleClick(ctrlKey, key)
{
   let fact = valueFacts[key];
   let widget = dashboards[actDashboard].widgets[key];
   let hasMode = fact && fact.outputModes == 3 //  -> ooUser and ooAuto are set

   // console.log("titleClick: ", ctrlKey, key, "fact.outputModes", fact ? fact.outputModes : "-");

   if (setupMode) {
      widgetSetup(key);
   }
   else if (ctrlKey && hasMode) {
      if (fact && localStorage.getItem(storagePrefix + 'Rights') & fact.rights)
         // console.log("toggleMode(", fact.address, fact.type, ')');
         toggleMode(fact.address, fact.type);
   }
   else if (ctrlKey) {
      // widget setup without the setup mode (storing the dashboard needs 'full control')
      if (localStorage.getItem(storagePrefix + 'Rights') & 0x04)
         widgetSetup(key);
   }
   else {
      sensorInfoDialog(key, widget);
   }
}

// info dialog of a sensor (click on the title of a widget, button 'Info' of the sensor setup),
//   without widget (sensor setup) the unit / widget type of the fact are taken

function sensorInfoDialog(key, widget)
{
   let fact = valueFacts[key];
   let hasMode = fact && fact.outputModes == 3 //  -> ooUser and ooAuto are set

   if (!fact)
      return;

   if (!widget)
      widget = { 'unit': fact.unit };

   let sensor = allSensors[key];

   if (sensor == null)
      return;

   let now = new Date();
   let lastUpd = new Date(sensor.last * 1000);
   let updBefore = prettyDuration(Math.round((now - (daemonState.timeOffset || 0) * 1000 - lastUpd)/1000));
   let changedAt = new Date(sensor.changedAt * 1000);
   let chgBefore = prettyDuration(Math.round((now - (daemonState.timeOffset || 0) * 1000 - changedAt)/1000));
   let peakMaxTime = new Date(sensor.peakmaxtime * 1000);
   let peakMinTime = new Date(sensor.peakmintime * 1000);
   let form = document.createElement("div");

   // time control (ranges of the outputs / actors) -> tab 'Regeln'

   let timeItems = [];

   if (fact.settings && Array.isArray(fact.settings.times))
      fact.settings.times.forEach((t, i) => timeItems.push(Object.assign(timeRangeItem(t, fact), { 'rangeIndex': i })));

   let rules = timeItems.concat(lightRulesFor(key));     // time control, light rules of the sensor / switching this light (tab 'Regeln')
   let hasRules = rules.length > 0 || sensor.offAt || sensor.shade;

   let value = sensor.value + ' ' + widget.unit;

   if (fact.widget && (fact.widget.widgettype == 0 || fact.widget.widgettype == 9 || fact.widget.widgettype == 12))
      value = sensor.value ? "On" :  "Off";

   if (!isEmpty(sensor.text))
      value = value + " / '" + sensor.text + "'";

   // console.log(sensor);
   // console.log(fact);

   // tabs: data, optional rules (light rules of the sensor / switching this light, see lightrules.c),
   //   optional JSON (the daemon holds the raw message of the sensor)

   let tabs = $('<div></div>').attr('id', 'sensorInfoTabs');
   let dataTab = $('<div></div>').attr('id', 'sensorInfoData');
   let withTabs = sensor.hasjson || hasRules;

   if (withTabs) {
      let ul = $('<ul></ul>')
          .append($('<li></li>').append($('<a></a>').attr('href', '#sensorInfoData').html('Daten')));

      if (hasRules)
         ul.append($('<li></li>').append($('<a></a>').attr('href', '#sensorInfoRules').html('Regeln')));

      if (sensor.hasjson)
         ul.append($('<li></li>').append($('<a></a>').attr('href', '#sensorInfoJson').html('JSON')));

      tabs.append(ul);
   }

   tabs.append(dataTab);

   if (hasRules) {
      let rulesTab = $('<div></div>').attr('id', 'sensorInfoRules').addClass('sensorRules');

      if (sensor.offAt)
         rulesTab.append($('<div></div>').attr('id', 'sensorRulesOffTimer').addClass('sensorRulesTimer')
                         .text('Aus-Timer läuft bis ' + new Date(sensor.offAt * 1000).toLocaleTimeString(dateLocale)));

      if (sensor.shade)
         rulesTab.append($('<div></div>').addClass('sensorRulesTimer').text('Beschattung: ' + sensor.shade));

      // next action of the time control, filled by the daemon (event 'nextactions')

      if (timeItems.length || sensor.offAt)
         rulesTab.prepend($('<div></div>').attr('id', 'sensorRulesNext').addClass('sensorRulesTimer')
                         .text('Nächste Aktion: wird berechnet ...'));

      for (const rule of rules) {
         let block = $('<div></div>').addClass('sensorRule')
             .append($('<div></div>').addClass('sensorRuleTitle').text(rule.title));

         for (const [label, text] of rule.rows)
            block.append($('<div></div>').addClass('sensorRuleRow')
                         .append($('<span></span>').addClass('sensorRuleLabel').text(label))
                         .append($('<span></span>').text(text)));

         if (rule.rangeIndex != null)
            block.append($('<div></div>').addClass('sensorRuleRow')
                         .append($('<span></span>').addClass('sensorRuleLabel').text('Nächste'))
                         .append($('<span></span>').attr('id', 'sensorRuleNext' + rule.rangeIndex).text('–')));

         rulesTab.append(block);
      }

      tabs.append(rulesTab);
   }

   if (sensor.hasjson)
      tabs.append($('<div></div>').attr('id', 'sensorInfoJson')
                  .append($('<div></div>').attr('id', 'sensorInfoJsonHead').addClass('sensorJsonHead').html('wird geladen ...'))
                  .append($('<pre></pre>').attr('id', 'sensorInfoJsonBody').addClass('sensorJsonBody')));

   $(form).append(tabs);

   $(dataTab).append($('<div></div>')
                  .css('z-index', '9999')
                  .css('minWidth', '40vh')
                  .append($('<div></div>')
                          .css('display', 'flex')
                          .css('margin-bottom', '4px')
                          .append($('<span></span>')
                                  .css('width', '30%')
                                  .css('text-align', 'end')
                                  .css('margin-right', '10px')
                                  .html('Sensor'))
                          .append($('<span></span>')
                                  .append($('<div></div>')
                                          .css('font-style', 'italic')
                                          .addClass('rounded-border')
                                          .html(key + ' (' + parseInt(key.split(":")[1]) + ')')
                                         )))

                  .append($('<div></div>')
                          .css('display', 'flex')
                          .css('margin-bottom', '4px')
                          .append($('<span></span>')
                                  .css('width', '30%')
                                  .css('text-align', 'end')
                                  .css('margin-right', '10px')
                                  .html('Title'))
                          .append($('<span></span>')
                                  .append($('<div></div>')
                                          .css('font-style', 'italic')
                                          .addClass('rounded-border')
                                          .html(fact.title)
                                         )))
                  .append($('<div></div>')
                          .css('display', 'flex')
                          .css('margin-bottom', '4px')
                          .append($('<span></span>')
                                  .css('width', '30%')
                                  .css('text-align', 'end')
                                  .css('margin-right', '10px')
                                  .html('User Title'))
                          .append($('<span></span>')
                                  .append($('<div></div>')
                                          .css('font-style', 'italic')
                                          .addClass('rounded-border')
                                          .html(fact.usrtitle)
                                         )))
                  .append($('<div></div>')
                          .css('display', 'flex')
                          .css('margin-bottom', '4px')
                          .append($('<span></span>')
                                  .css('width', '30%')
                                  .css('text-align', 'end')
                                  .css('margin-right', '10px')
                                  .html('Value'))
                          .append($('<span></span>')
                                  .append($('<div></div>')
                                          .addClass('rounded-border')
                                          .html(value)
                                         )))
                  .append($('<div></div>')
                          .css('display', !isNil(sensor.peakmin) ? 'flex' : 'none')
                          .css('margin-bottom', '4px')
                          .append($('<span></span>')
                                  .css('width', '30%')
                                  .css('text-align', 'end')
                                  .css('margin-right', '10px')
                                  .html('Peak Min'))
                          .append($('<span></span>')
                                  .append($('<div></div>')
                                          .attr('id', 'dlgPeakMin')
                                          .addClass('rounded-border')
                                          .append(!isNil(sensor.peakmin) ? sensor.peakmin + ' ' + widget.unit + ' ' : '- ')
                                          .append($('<span></span>')
                                                  .css('font-size', 'smaller')
                                                  .css('color', '#3d3737')
                                                  .html(peakMinTime.toLocaleString('de-DE')))
                                         )))
                  .append($('<div></div>')
                          .css('display', !isNil(sensor.peakmax) ? 'flex' : 'none')
                          .css('margin-bottom', '4px')
                          .append($('<span></span>')
                                  .css('width', '30%')
                                  .css('text-align', 'end')
                                  .css('margin-right', '10px')
                                  .html('Peak Max'))
                          .append($('<span></span>')
                                  .append($('<div></div>')
                                          .attr('id', 'dlgPeakMax')
                                          .addClass('rounded-border')
                                          .append(!isNil(sensor.peakmax) ? sensor.peakmax + ' ' + widget.unit + ' ' : '- ')
                                          .append($('<span></span>')
                                                  .css('font-size', 'smaller')
                                                  .css('color', '#3d3737')
                                                  .html(peakMaxTime.toLocaleString('de-DE')))
                                         )))
                  .append($('<div></div>')
                          .css('display', 'flex')
                          .css('margin-bottom', '4px')
                          .append($('<span></span>')
                                  .css('width', '30%')
                                  .css('text-align', 'end')
                                  .css('margin-right', '10px')
                                  .html('Last Update'))
                          .append($('<span></span>')
                                  .append($('<div></div>')
                                          .addClass('rounded-border')
                                          .append(lastUpd.toLocaleString('de-DE') + ' ')
                                          .append($('<span></span>')
                                                  .css('font-size', 'smaller')
                                                  .css('color', '#3d3737')
                                                  .html(updBefore))
                                         )))
                  .append($('<div></div>')
                          .css('display', 'flex')
                          .css('margin-bottom', '4px')
                          .append($('<span></span>')
                                  .css('width', '30%')
                                  .css('text-align', 'end')
                                  .css('margin-right', '10px')
                                  .html('Changed'))
                          .append($('<span></span>')
                                  .append($('<div></div>')
                                          .addClass('rounded-border')
                                          .append(changedAt.toLocaleString('de-DE') + ' ')
                                          .append($('<span></span>')
                                                  .css('font-size', 'smaller')
                                                  .css('color', '#3d3737')
                                                  .html(chgBefore))
                                         )))
                  .append($('<div></div>')         // seconds without data until invalid (daemon, sensor2Json())
                          .css('display', sensor.timeout != null ? 'flex' : 'none')
                          .css('margin-bottom', '4px')
                          .append($('<span></span>')
                                  .css('width', '30%')
                                  .css('text-align', 'end')
                                  .css('margin-right', '10px')
                                  .html('Ungültig nach'))
                          .append($('<span></span>')
                                  .append($('<div></div>')
                                          .addClass('rounded-border')
                                          .append(sensor.timeout > 0 ? prettyDuration(sensor.timeout) + ' ohne Daten ' : 'nie (keine Zeitüberwachung) ')
                                          .append($('<span></span>')
                                                  .css('font-size', 'smaller')
                                                  .css('color', sensor.valid ? '#5b8c5a' : '#b55a5a')   // gedämpftes Grün / Rot
                                                  .html(sensor.valid ? 'gültig' : 'aktuell ungültig'))
                                         )))

                  .append($('<div></div>')
                          .css('display', sensor.battery ? 'flex' : 'none')
                          .css('margin-bottom', '4px')
                          .append($('<span></span>')
                                  .css('width', '30%')
                                  .css('text-align', 'end')
                                  .css('margin-right', '10px')
                                  .html('Battery'))
                          .append($('<span></span>')
                                  .append($('<div></div>')
                                          .addClass('rounded-border')
                                          .append(batterySymbol(sensor.battery))
                                          .append($('<span></span>').html(sensor.battery ? ' ' + sensor.battery + ' %' :  '-'))
                                         )))

                  .append($('<div></div>')         // deCONZ: the MAC as the deCONZ log shows it
                          .css('display', fact.mac ? 'flex' : 'none')
                          .css('margin-bottom', '4px')
                          .append($('<span></span>')
                                  .css('width', '30%')
                                  .css('text-align', 'end')
                                  .css('margin-right', '10px')
                                  .html('MAC'))
                          .append($('<span></span>')
                                  .append($('<div></div>')
                                          .addClass('rounded-border')
                                          .css('user-select', 'text')
                                          .text(fact.mac || '')
                                         )))
                 );

   let btns = {};

   if (!isNil(sensor.peakmax)) {
      btns['Reset Peak'] = function() {
         $('#dlgPeakMax').html('-');
         $('#dlgPeakMin').html('-');
         socket.send({ "event" : "command", "object" : { "what" : 'peak', "type": fact.type, "address": fact.address } });
         let sensor = allSensors[key];
         sensor.peakmax = null;
         sensor.peakmin = null;
      };
   }

   if (hasMode) {
      btns['Manuel/Auto'] = function () {
         if (fact && localStorage.getItem(storagePrefix + 'Rights') & fact.rights)
            toggleMode(fact.address, fact.type);
         $(this).dialog('close');
      };
   }

   btns['Ok'] = function () { $(this).dialog('close'); };

   $(form).dialog({
      modal: true,
      closeOnEscape: true,
      hide: "fade",
      width: "auto",
      title: "Info - " + key + ' (' + parseInt(key.split(":")[1]) + ')',
      buttons: btns,
      open: function() {
         if (withTabs) {
            $('#sensorInfoTabs').tabs({
               active: 0,                       // always start with 'Daten' (else jQuery UI picks the tab matching location.hash)
               activate: function(event, ui) {
                  if (ui.newPanel.attr('id') == 'sensorInfoJson')
                     socket.send({ "event" : "command", "object" : { "what" : 'sensorjson', "type": fact.type, "address": fact.address } });
               }
            });

            // the dialog is sized by its content ('auto'); keep the width of the data tab
            // when switching to the (initially almost empty) JSON tab

            let tabsWidth = $('#sensorInfoTabs').width();
            $('#sensorInfoTabs').css('min-width', tabsWidth + 'px');

            // the rules may go up to 60vw but at least fill the dialog (on a phone the data tab is wider than 60vw)

            $('#sensorInfoRules').css('max-width', Math.max(tabsWidth, window.innerWidth * 0.6) + 'px');

            // fetch the JSON right away (small), so it is there when the tab is opened

            if (sensor.hasjson)
               socket.send({ "event" : "command", "object" : { "what" : 'sensorjson', "type": fact.type, "address": fact.address } });

            // the next actions of the time control (calculated by the daemon with the sun times of their days)
            //   and the off timers of the light rules

            if (timeItems.length || sensor.offAt)
               socket.send({ "event" : "command", "object" : { "what" : 'nextactions', "type": fact.type, "address": fact.address } });
         }
      },
      close: function() { $(this).dialog('destroy').remove();}
   });
}

// event 'nextactions' - the next automatic actions of an output / actor (info dialog, tab 'Regeln'):
//   time control: 'next' the earliest one, 'ranges' the next one of each time range, 'note' why there is none,
//   'timers' the running off timers of the light rules (e.g. after a motion) switching off this light

function showNextActions(obj)
{
   if (!$('#sensorRulesNext').length)
      return;

   let blind = obj.type == 'HMB';

   let actionText = function(a) {
      let text = blind ? (a.in ? 'Zu' : 'Auf') : (a.in ? 'An' : 'Aus');

      if (a.level != null)
         text += ' (' + a.level + ' %)';
      else if (a.bri != null)
         text += ' (' + a.bri + ' %)';

      return text + ' – ' + nextActionTime(a.at);
   };

   let ruleTitle = function(ruleKey) {          // 'DZS:0x21#0' -> 'Flur: Bewegung → An'
      let [sensorKey, index] = ruleKey.split('#');
      let f = valueFacts[sensorKey];
      let rule = f && f.settings && Array.isArray(f.settings.rules) ? f.settings.rules[parseInt(index)] : null;
      return factTitle(sensorKey) + (rule ? ': ' + lightRuleItem(rule).title : '');
   };

   // the off timers of the light rules

   let timers = (obj.timers || []).slice().sort((a, b) => a.at - b.at);

   if (timers.length)
      $('#sensorRulesOffTimer').html(timers.map(t => $('<div></div>')
                                                 .text('Aus-Timer (' + ruleTitle(t.rule) + '): Aus – ' + nextActionTime(t.at, true) +
                                                       (t.motion ? ', frühestens – verlängert, solange Bewegung erkannt wird' : ''))[0]));

   // the earliest action of all

   let candidates = [];

   if (obj.next)
      candidates.push({ 'at': obj.next.at, 'text': actionText(obj.next) + ' (Zeitsteuerung)' });

   for (const t of timers)
      candidates.push({ 'at': t.at, 'text': 'Aus – ' + nextActionTime(t.at, true) + ' (Aus-Timer' + (t.motion ? ', frühestens' : '') + ')' });

   candidates.sort((a, b) => a.at - b.at);

   if (candidates.length)
      $('#sensorRulesNext').text('Nächste Aktion: ' + candidates[0].text);
   else if (obj.note)
      $('#sensorRulesNext').text('Nächste Aktion: keine (' + obj.note + ')');
   else
      $('#sensorRulesNext').text('Nächste Aktion: keine innerhalb einer Woche');

   (obj.ranges || []).forEach((a, i) => $('#sensorRuleNext' + i).text(a ? actionText(a) : '–'));
}

// time of an action as 'heute 22:30', 'morgen 07:15' or 'Sa. 08.11. 07:15' (optional with seconds)

function nextActionTime(at, withSeconds)
{
   let d = new Date(at * 1000);
   let today = new Date();
   let days = Math.round((new Date(d).setHours(0, 0, 0, 0) - today.setHours(0, 0, 0, 0)) / 86400000);
   let time = d.toLocaleTimeString(dateLocale, withSeconds ? { hour: '2-digit', minute: '2-digit', second: '2-digit' }
                                                           : { hour: '2-digit', minute: '2-digit' });

   if (days == 0)
      return 'heute ' + time;
   if (days == 1)
      return 'morgen ' + time;

   return d.toLocaleDateString(dateLocale, { weekday: 'short', day: '2-digit', month: '2-digit' }) + ' ' + time;
}

// event 'sensorjson' - the last raw message of a sensor (info dialog, tab JSON)

function showSensorJson(obj)
{
   if (!$('#sensorInfoJson').length)
      return;

   let head = obj.json ? (obj.topic ? obj.topic + ' · ' : '') + new Date(obj.time * 1000).toLocaleString('de-DE') : 'keine Nachricht vorhanden';
   let body = '';

   if (obj.json) {
      try {
         body = JSON.stringify(JSON.parse(obj.json), null, 2);
      }
      catch (e) {
         body = obj.json;      // not (valid) JSON - show as is
      }
   }

   $('#sensorInfoJsonHead').text(head);
   $('#sensorInfoJsonBody').text(body);
}

function updateDashboard(widgets, refresh)
{
   if (widgets != null && dashboards[actDashboard] != null) {
      for (let key in widgets) {
         if (dashboards[actDashboard].widgets[key] == null)
            continue;

         updateWidget(widgets[key], refresh, dashboards[actDashboard].widgets[key]);
      }

      updateHtmlWidgets(widgets);
   }
}

//***************************************************************************
// Widget Symbol
//   state, image / MDI classes and color of a symbol widget (Symbol, SymbolValue, SymbolText),
//   shared by the dashboard (updateWidget()) and the page 'List'
//   -> { state, image: '<path>' or '', classes: 'mdi mdi-..' or '', color }
//***************************************************************************

function widgetSymbolOf(fact, widget, sensor)
{
   // a stale sensor (e.g. device offline) is displayed as 'off': symbol, color and no animation

   let state = sensor.valid && (fact.type != 'HMB' ? sensor.value != 0 : sensor.value == 100);
   let image = '';
   let classes = '';

   if (sensor.image != null) {                                    // own image got by data
      if (sensor.image.indexOf('mdi:') != 0)
         image = sensor.image;
      else
         classes = sensor.image.replace(':', ' ');
   }
   else if (state && widget.symbolOn && widget.symbolOn != '')    // MDI Icon ON
      classes = widget.symbolOn.replace(':', ' ');
   else if (!state && widget.symbol && widget.symbol != '')       // MDI Icon OFF
      classes = widget.symbol.replace(':', ' ');
   else if (widget.symbol && widget.symbol != '')                 // MDI Icon
      classes = widget.symbol.replace(':', ' ');
   else                                                           // image by patch as <img> element
      image = state ? widget.imgon : widget.imgoff;

   if (state && widget.spin && classes != '')                     // option 'Aktivität anzeigen': the symbol turns while on
      classes += ' mdispin';

   widget.colorOn = widget.colorOn == null ? symbolOnColorDefault : widget.colorOn;

   let color = state ? widget.colorOn : widget.color;

   color = getWidgetColor(widget, widget.colorCondition, color, sensor.value);

   if (sensor.hue)
      color = tinycolor({ 'h': sensor.hue, 's': sensor.sat, 'v': sensor.score }).toHslString();
   else if (sensor.color)
      color = sensor.color;

   return { 'state': state, 'image': image, 'classes': classes, 'color': color };
}

function updateWidget(sensor, refresh, widget)
{
   if (sensor == null)
      return ;

   let key = toKey(sensor.type, sensor.address);
   fact = valueFacts[key];

   // console.log("updateWidget " + fact.name + " of type " + widget.widgettype);
   // console.log("updateWidget" + JSON.stringify(sensor, undefined, 4));

   if (fact == null) {
      console.log("Fact for widget '" + key + "' not found, ignoring");
      return ;
   }
   if (widget == null) {
      console.log("Widget '" + key + "' not found, ignoring");
      return;
   }

   if (widget.unit == '')
      widget.unit = fact.unit;

   let widgetDiv = $('#div_' + key.replace(':', '\\:'));

   if (widget.range > 2)
   {
      // console.log("widget.range", key, widget.range);
      sensor.valid = true;
   }

   if (sensor.type == 'WEA' && (sensor.address == 2 || sensor.address == 3 || sensor.address == 4)) // Windy App
      sensor.valid = true;

   if (sensor.type == 'WEA' && sensor.address == 1)
      console.log("WEA 1: valid", String(sensor.valid));

   widgetDiv.css('opacity', sensor.valid ? '100%' : '45%');
   // $('#div_' + key.replace(':', '\\:')).css('opacity', sensor.valid ? '100%' : '25%');

   if (widget.widgettype == 0 || widget.widgettype == 9 || widget.widgettype == 12)         // Symbol, Symbol-Value, Symbol-Text
   {
      // console.log("sensor: ", JSON.stringify(sensor));

      let sym = widgetSymbolOf(fact, widget, sensor);    // shared with the page 'List'
      let state = sym.state;
      let image = sym.image;
      let classes = sym.classes;

      if (classes != '')                                             // MDI symbol, no <img> element
         $("#widget" + fact.type + fact.address).remove();

      if (image != '')
         $("#widget" + fact.type + fact.address).attr("src", image);
      else {
         $("#button" + fact.type + fact.address).removeClass();
         $("#button" + fact.type + fact.address).addClass('widget-main');
         $("#button" + fact.type + fact.address).addClass(classes);

         let fontSize = Math.min(widgetDiv.height(), widgetDiv.width()) * 0.6;
         $("#button" + fact.type + fact.address).css('font-size', fontSize + 'px');
      }

      $("#button" + fact.type + fact.address).css('background-color', sensor.working ? '#7878787878' : 'transparent');

      // $('#div_'+key.replace(':', '\\:')).css('background-color', (fact.outputModes == 3 && sensor.mode == 'manual') ? '#a27373' : '');
      widgetDiv.css('background-color', (fact.outputModes == 3 && sensor.mode == 'manual') ? '#a27373' : '');

      $("#button" + fact.type + fact.address).css('color', sym.color);

      if (widget.widgettype == 9) {
         $("#value" + fact.type + fact.address).text(sensor.value.toFixed(widget.unit == '%' || widget.unit == '' ? 0 : 2) + (widget.unit != '' ? ' ' : '') + widget.unit);
         applyMarquee(document.getElementById('value' + fact.type + fact.address));
      }
      else if (widget.widgettype == 12 && sensor.text != null) {
         $("#value" + fact.type + fact.address).text(sensor.text.replace(/(?:\r\n|\r|\n)/g, '<br>'));
         applyMarquee(document.getElementById('value' + fact.type + fact.address));
      }

//      let prs = $('#progressBar' + fact.type + fact.address);
//      $('#progress' + fact.type + fact.address).css('display', fact.options & 0x02 && sensor.value ? 'block' : 'none');
//
//      if (sensor.score && prs != null)
//         $(prs).css('width', sensor.score + '%');
//
//      if (sensor.mode == "auto" && sensor.next > 0) {
//         let pWidth = 100;
//         let s = sensor;
//         let id = fact.type + fact.address;
//         let iid = setInterval(progress, 200);
//
//         function progress() {
//            if (pWidth <= 0) {
//               clearInterval(iid);
//            } else {
//               let d = new Date();
//               pWidth = 100 - ((d/1000 - s.last) / ((s.next - s.last) / 100));
//               document.getElementById("progressBar" + id).style.width = pWidth + "%";
//            }
//         }
//      }
   }
   else if (widget.widgettype == 1 || widget.widgettype == 13)    // Chart
   {
      if (widget.showpeak || widget.showpeakmin)
         $("#peak" + fact.type + fact.address).html(peakHtml(widget, sensor, ' ', true));

      $("#value" + fact.type + fact.address).text(sensor.value.toFixed(2) + " " + widget.unit);

      if (refresh) {
         let jsonRequest = {};
         if (!widget.range)
            widget.range = 1;
         prepareChartRequest(jsonRequest, toKey(fact.type, fact.address), 0, widget.range, widget.widgettype == 13 ? "chartwidgetbar" : "chartwidget");
         socket.send({ "event" : "chartdata", "object" : jsonRequest });
      }
   }
   else if (widget.widgettype == 8)    // Choice
   {
      if (!sensor.text)
         sensor.text = sensor.value;
      let choices = fact.choices.split(",");

      for (let c = 0; c < choices.length; ++c) {
         let $element = $("#widget" + fact.type + fact.address + '_' + c);
         let isSelected = (sensor.text == choices[c]);

         if (widget.buttons)
            $element.toggleClass('widget-choice-button-active', isSelected);
         else
            $element.css('background-color', isSelected ? 'gray' : "");

         if (isSelected && $element.length) {
            // Findet das spezifische, scrollbare DIV dieses Widgets
            let $container = $element.closest('.widget-choice'); // Passen Sie den CSS-Selektor an Ihr DIV an

            if ($container.length) {
               // Position des Elements relativ zum sichtbaren oberen Rand des Containers
               let elementTop = $element.position().top;
               let containerHeight = $container.height();
               let elementHeight = $element.outerHeight();

               // Berechnet den exakten Scroll-Wert für die Zentrierung IM Container
               let targetScroll = $container.scrollTop() + elementTop - (containerHeight / 2) + (elementHeight / 2);

               // Scrollt NUR den Container, die restliche Webseite bleibt starr
               $container.stop().animate({
                  scrollTop: targetScroll
               }, 300); // 300ms für weiches Scrollen. Für sofortigen Sprung: $container.scrollTop(targetScroll);
            }
         }
      }
   }
   else if (widget.widgettype == 88888)    // Choice
   {
      // let text = sensor.text.replace(/(?:\r\n|\r|\n)/g, '<br>');

      if (!sensor.text)
         sensor.text = sensor.value;
      let choices = fact.choices.split(",");
      for (let c = 0; c < choices.length; ++c) {
         let $element = $("#widget" + fact.type + fact.address + '_' + c);
         let isSelected = (sensor.text == choices[c]);
         $element.css('background-color', isSelected ? 'gray' : "");

         // in den sichtbaren Bereich scrollen

         if (isSelected && $element.length) {
            $element[0].scrollIntoView({
               behavior: 'smooth',    // 'smooth' für Animation, 'auto' für sofortigen Sprung
               block: 'center'        // Erzwingt die Positionierung in der Mitte
               // block: 'nearest'    // Scrollt nur, wenn das Element nicht bereits sichtbar ist
            });
         }
      }
   }
   else if (widget.widgettype == 2 || widget.widgettype == 7)    // Text, PlainText
   {
      if (sensor.type == 'WEA' && sensor.address == 1 && sensor.text != null) { // Open Weather Map
         let bigView = false;
         let wfact = fact;
         weatherData = JSON.parse(sensor.text);
         $("#widget" + fact.type + fact.address).html(getWeatherHtml(bigView, wfact, weatherData));
         if (weatherInterval)
            clearInterval(weatherInterval);
         if (weatherData && config.toggleWeatherView != 0) {
            weatherInterval = setInterval(function() {
               bigView = !bigView;
               $("#widget" + wfact.type + wfact.address).html(getWeatherHtml(bigView, wfact, weatherData));
            }, 5*1000);
         }
      }
      else if (sensor.type == 'WEA' && (sensor.address == 2 || sensor.address == 3)) { // Windy App

      }
      else if (sensor.text != null) {
         $("#widget" + fact.type + fact.address).css('color', getWidgetColor(widget, widget.colorCondition, widget.color, sensor.text));
         if (widget.widgettype == 7 && sensor.text.indexOf('\n') != -1) {
            // show plain with tabs text as table

            let lines = sensor.text.split(/(?:\r\n|\r|\n)/);
            let html = '';

            for (l = 0; l < lines.length; ++l) {
               let tabs = lines[l].split(/(?:\t)/);
               let tabHtml = '';
               for (t = 0; t < tabs.length; ++t)
                  tabHtml += '<td>' + tabs[t] + '</td>';
               html += '<tr>' + tabHtml + '</tr>\n';
            }

            $("#widget" + fact.type + fact.address).html('<table>' + html + '</table>');
         }
         else {
            let text = sensor.text.replace(/(?:\r\n|\r|\n)/g, '<br>');
            $("#widget" + fact.type + fact.address).html(text);
         }
      }
   }
   else if (widget.widgettype == 3)      // Value
   {
      $("#widget" + fact.type + fact.address).css('color', getWidgetColor(widget, widget.colorCondition, widget.color, sensor.value));
      $("#widget" + fact.type + fact.address).html(sensor.value + " " + widget.unit);
      if (widget.showpeak || widget.showpeakmin)
         $("#peak" + fact.type + fact.address).html(peakHtml(widget, sensor, ' ', true));
   }
   else if (widget.widgettype == 4)      // Gauge
   {
      let value = sensor.value.toFixed(2);
      let scaleMax = !widget.scalemax || widget.unit == '%' ? 100 : widget.scalemax.toFixed(0);
      let scaleMin = value >= 0 ? "0" : Math.ceil(value / 5) * 5 - 5;
      let _peak = sensor.peakmax != null ? sensor.peakmax : 0;
      let _peakMin = sensor.peakmin != null ? sensor.peakmin : 0;
      if (scaleMax < Math.ceil(value))
         scaleMax = value;
      if (widget.showpeak != null && widget.showpeak && scaleMax < Math.ceil(_peak))
         scaleMax = _peak.toFixed(0);
      if (widget.showpeakmin && _peakMin < scaleMin)
         scaleMin = Math.floor(_peakMin / 5) * 5;
      $("#sMin" + fact.type + fact.address).text(scaleMin);
      $("#sMax" + fact.type + fact.address).text(scaleMax);
      $("#value" + fact.type + fact.address).text(value + " " + widget.unit);
      let ratio = (value - scaleMin) / (scaleMax - scaleMin);
      let peak = (_peak.toFixed(2) - scaleMin) / (scaleMax - scaleMin);
      let peakMin = (_peakMin.toFixed(2) - scaleMin) / (scaleMax - scaleMin);

      $("#pb" + fact.type + fact.address).attr("d", "M 950 500 A 450 450 0 0 0 50 500");
      $("#pv" + fact.type + fact.address).attr("d", svg_circle_arc_path(500, 500, 450 /*radius*/, -90, ratio * 180.0 - 90));
      if (widget.showpeak != null && widget.showpeak)
         $("#pp" + fact.type + fact.address).attr("d", svg_circle_arc_path(500, 500, 450 /*radius*/, peak * 180.0 - 91, peak * 180.0 - 90));
      if (widget.showpeakmin)
         $("#pm" + fact.type + fact.address).attr("d", svg_circle_arc_path(500, 500, 450 /*radius*/, peakMin * 180.0 - 90, peakMin * 180.0 - 89));
   }
   else if (widget.widgettype == 5 || widget.widgettype == 6)    // Meter
   {
      if (sensor.value != null) {
         let value = sensor.value;
         let reInitMeter = false;

         if (widgetDiv.data('scalemax') != widget.scalemax)
            widgetDiv.data('scalemax', widget.scalemax);

         if (widget.rescale) {
            let neededScaleMax = widget.scalemax;

            if (value <= 0)
               ;
            else if (value < parseInt(widget.scalemax / 100))
               neededScaleMax = parseInt(widget.scalemax / 100);
            else if (value < parseInt(widget.scalemax / 80))
               neededScaleMax = parseInt(widget.scalemax / 80);
            else if (value < parseInt(widget.scalemax / 40))
               neededScaleMax = parseInt(widget.scalemax / 40);
            else if (value < parseInt(widget.scalemax / 20))
               neededScaleMax = parseInt(widget.scalemax / 20);
            else if (value < parseInt(widget.scalemax / 10))
               neededScaleMax = parseInt(widget.scalemax / 10);
            else if (value < parseInt(widget.scalemax / 4))
               neededScaleMax = parseInt(widget.scalemax / 4);
            else if (value < parseInt(widget.scalemax / 2))
               neededScaleMax = parseInt(widget.scalemax / 2);
            else if (value < parseInt(widget.scalemax / 1.5))
               neededScaleMax = parseInt(widget.scalemax / 1.5);

            if (widgetDiv.data('scalemax') != neededScaleMax) {
               widgetDiv.data('scalemax', neededScaleMax);
               reInitMeter = true;
               // initMeter(key, widget, fact, neededScaleMax, sensor.value);
            }
         }

         let progressColor = getWidgetColor(widget, widget.colorConditionBar, widget.barcolor, value);

         if (widgetDiv.data('progressColor') != progressColor) {
            widgetDiv.data('progressColor', progressColor);
            reInitMeter = true;
         }

         if (reInitMeter)
            initMeter(key, widget, fact, widgetDiv.data('scalemax'), sensor.value);

         if (widget.widgettype == 6) {
            $("#widgetValue" + fact.type + fact.address).css('color', getWidgetColor(widget, widget.colorCondition, widget.color, value));
            $('#widgetValue' + fact.type + fact.address).html(value.toFixed(widget.unit == '%' ? 0 : 1) + ' ' + widget.unit);
         }

         let gauge = dashboardGauges[fact.type + fact.address];

         if (gauge != null)
            gauge.value = value;
         else
            console.log("Missing gauge instance for " + '#widget' + fact.type + fact.address);

         if (widget.showpeak || widget.showpeakmin) {
            let ePeak = $("#peak" + fact.type + fact.address);

            if (ePeak.hasClass('widget-main-peak-lin'))      // own grid cell right of the linear gauge, two lines
               ePeak.html(peakHtml(widget, sensor, '<br>', true));
            else                                             // one line above / on the gauge, the radial one shows the unit itself
               ePeak.html(peakHtml(widget, sensor, ' ', widget.widgettype != 5));
         }
      }
      else
         console.log("Missing value for " + '#widget' + fact.type + fact.address);
   }
   else if (widget.widgettype == 15)    // Level
   {
      updateLevel(sensor, widget, fact);
   }
   else if (widget.widgettype == 16)    // Balance
   {
      updateBalance(sensor, widget, fact);
   }
   else if (widget.widgettype == 14) {  // Special Symbol
      let div = document.getElementById('widget' + key);
      let l = document.getElementById('svg' + key);
      let v = document.getElementById('value' + key);
      let y = parseFloat($(div).data('yStart'));
      let fullPx = parseFloat($(div).data('heightStart'));
      let percent = sensor.value;
      if (widget.unit != '%')
         percent = sensor.value / (widget.scalemax / 100);
      let levelPx = Math.round((fullPx / 100) * percent);

      v.innerHTML = sensor.value + ' ' + widget.unit;
      l.setAttribute('y', fullPx - levelPx + y);
      l.setAttribute('height', levelPx);

      let ellipse = document.getElementById('svgEllipse' + key);

      if (ellipse != null)
         ellipse.setAttribute('cy', fullPx - levelPx + y);

      // console.log("value:", sensor.value, "levelPx:", levelPx);
   }
}

var filterExpression = null;
var addFilterString = '';

function addWidget()
{
   // console.log("add widget ..");

   let form = document.createElement("div");

   $(form)
      .append($('<div></div>')
              .append($('<span></span>')
                      .addClass('labelB2')
                      .html('Filter'))
              .append($('<span></span>')
                      .append($('<input></input>')
                              .attr('id', 'incFilter')
                              .attr('type', 'search')
                              .attr('placeholder', 'expression...')
                              .addClass('rounded-border input')
                              .val(addFilterString)
                                  .on('input', function() {updateSelection();})
                             )))
      .append($('<div></div>')
              .css('margin-top', '10px')
              .append($('<span></span>')
                      .addClass('labelB2')
                      .css('vertical-align', 'top')
                      .html('Widget'))
              .append($('<span></span>')
                      .append($('<select></select>')
                              .addClass('rounded-border input')
                              .attr('id', 'widgetKey')
                              .attr('multiple', 'multiple')
                              .attr('size', 15)
                              .css({'height': 'auto', 'min-width': '22em', 'vertical-align': 'top'})   // .input fixes height to 32px
                              .on('change', function() { rememberSelection(); })
                              .on('dblclick', function(event) {
                                 // double click: only this entry, added at once
                                 let $option = $(event.target).closest('option');
                                 if (!$option.length)
                                    return;
                                 selectedKeys.clear();
                                 $('#widgetKey option').prop('selected', false);
                                 $option.prop('selected', true);
                                 $(form).parent().find(".ui-dialog-buttonpane button:contains('Ok')").click();
                              })
                             )))
      .append($('<div></div>')
              .css('margin-top', '10px')
              .append($('<span></span>')
                      .addClass('labelB2')
                      .html('Ausgewählt'))
              .append($('<span></span>')
                      .attr('id', 'widgetSelected')
                      .css({'margin-left': '8px', 'font-size': '16px', 'font-weight': 'bold'})
                      .html('0')));

   // selection survives filter changes: keys selected under one filter stay
   // selected while the user narrows down the list with another filter

   let selectedKeys = new Set();

   function rememberSelection() {
      $('#widgetKey option').each(function() {
         if ($(this).is(':selected'))
            selectedKeys.add($(this).val());
         else
            selectedKeys.delete($(this).val());
      });
      $('#widgetSelected').html(selectedKeys.size);
   }

   $(form).dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      hide: "fade",
      width: "auto",
      title: "Add Widget",
      open: function() {
         updateSelection();
      },
      buttons: {
         'Abbrechen': function () {
            $(this).dialog('close');
         },
         'Ok': function () {
            let json = {};
            console.log("store widget");
            $('#widgetContainer > div').each(function () {
               let key = $(this).attr('id').substring($(this).attr('id').indexOf("_") + 1);
               json[key] = dashboards[actDashboard].widgets[key];
            });

            rememberSelection();

            for (let selected of selectedKeys) {
               if (selected == 'ALL') {
                  for (let key in valueFacts) {
                     if (!valueFacts[key].state)   // use only active facts
                        continue;
                     if (filterExpression && !filterExpression.test(valueFacts[key].title) &&
                         !filterExpression.test(valueFacts[key].usrtitle) &&
                         !filterExpression.test(valueFacts[key].type))
                        continue;
                     if (dashboards[actDashboard].widgets[key] != null)
                        continue;
                     json[key] = "";
                  }
               }
               else
                  json[selected] = "";
            }

            socket.send({ "event" : "storedashboards", "object" : { [actDashboard] : { 'title' : dashboards[actDashboard].title, 'widgets' : json } } });
            socket.send({ "event" : "forcerefresh", "object" : { 'action' : 'dashboards' } });
            $(this).dialog('close');
         }
      },
      close: function() { $(this).dialog('destroy').remove(); }
   });

   function updateSelection() {
      let addrSpacer = -1;
      let addrTime = -1;
      let addrHtml = -1;
      $('#widgetKey').empty();
      // console.log("update selection: " + $('#incFilter').val());
      for (let key in dashboards[actDashboard].widgets) {
         n = parseInt(key.split(":")[1]);
         if (key.split(":")[0] == 'SPACER' && n > addrSpacer)
            addrSpacer = n;
         if (key.split(":")[0] == 'TIME' && n > addrTime)
            addrTime = n;
         if (key.split(":")[0] == 'HTML' && n > addrHtml)
            addrHtml = n;
      }
      $('#widgetKey').append($('<option></option>')
                             .val('SPACER:0x' + (addrSpacer + 1).toString(16))
                             .html('Spacer'));
      $('#widgetKey').append($('<option></option>')
                             .val('TIME:0x' + (addrTime + 1).toString(16))
                             .html('Time'));
      $('#widgetKey').append($('<option></option>')
                             .val('HTML:0x' + (addrHtml + 1).toString(16))
                             .html('HTML'));

      let jArray = [];
      filterExpression = null;
      addFilterString = $('#incFilter').val();

      if ($('#incFilter').val() != '')
         filterExpression = new RegExp(addFilterString);

      for (let key in valueFacts) {
         if (!valueFacts[key].state)   // use only active facts here
            continue;
         // if (dashboards[actDashboard].widgets[key] != null)
         //    continue;
         if (filterExpression && !filterExpression.test(valueFacts[key].title) &&
             !filterExpression.test(valueFacts[key].usrtitle) &&
             !filterExpression.test(valueFacts[key].type))
            continue;

         jArray.push([key, valueFacts[key]]);
      }
      jArray.push(['ALL', { 'title': '- ALLE -'}]);
      jArray.sort(function(a, b) {
         let A = (a[1].usrtitle ? a[1].usrtitle : a[1].title).toLowerCase();
         let B = (b[1].usrtitle ? b[1].usrtitle : b[1].title).toLowerCase();
         if (B > A) return -1;
         if (A > B) return  1;
         return 0;

      });
      for (let i = 0; i < jArray.length; i++) {
         let key = jArray[i][0];
         let title = jArray[i][1].usrtitle ? jArray[i][1].usrtitle : jArray[i][1].title;

         if (valueFacts[key] != null)
            title += ' / ' + valueFacts[key].type;
         if (jArray[i][1].unit != null && jArray[i][1].unit != '')
            title += ' [' + jArray[i][1].unit + ']';

         $('#widgetKey').append($('<option></option>')
                                .val(jArray[i][0])
                                .prop('selected', selectedKeys.has(jArray[i][0]))
                                .html(title));
      }

      $('#widgetSelected').html(selectedKeys.size);
   }
}

function toggleChartDialog(type, address)
{
   let dialog = document.querySelector('dialog');
   dialog.style.position = 'fixed';
   console.log("chart for " + type + address);

   if (type != "" && !dialog.hasAttribute('open')) {
      let canvas = document.querySelector("#chartDialog");
      canvas.getContext('2d').clearRect(0, 0, canvas.width, canvas.height);
      chartDialogSensor = type + address;
      chartDialogKey = toKey(type, address);
      chartDialogDayOffset = 0;

      // show the dialog

      dialog.setAttribute('open', 'open');

      requestChartDialogData();

      // EventListener für ESC- und Cursor-Tasten

      document.addEventListener('keydown', chartDialogKeyDown);

      // only hide the background *after* you've moved focus out of
      //   the content that will be "hidden"

      let div = document.createElement('div');
      div.id = 'backdrop';
      document.body.appendChild(div);
   }
   else {
      chartDialogSensor = "";
      chartDialogKey = "";
      chartDialogDayOffset = 0;
      document.removeEventListener('keydown', chartDialogKeyDown);
      dialog.removeAttribute('open');
      let div = document.querySelector('#backdrop');
      div.parentNode.removeChild(div);
   }
}

//***************************************************************************
// Chart Dialog Daten anfordern / blättern
//***************************************************************************

function requestChartDialogData()
{
   // der Sensor muss nicht selbst als Widget auf dem Dashboard liegen
   // (z.B. data-chart eines HTML Widgets) -> dann Default 1 Tag

   let widget = dashboards[actDashboard].widgets[chartDialogKey];
   let range = parseFloat(widget && widget.range) || 1;
   let start = 0;    // 0 -> der Server nimmt 'jetzt - range'

   if (chartDialogDayOffset > 0)
      start = new Date(Date.now() - (range + chartDialogDayOffset) * 24*60*60*1000);

   let jsonRequest = {};
   prepareChartRequest(jsonRequest, chartDialogKey, start, range, "chartdialog");
   socket.send({ "event" : "chartdata", "object" : jsonRequest });

   updateChartDialogRange(start, range);
}

function chartDialogPage(direction)
{
   if (chartDialogKey == "")
      return ;

   if (direction == 0)                    // zurück zum aktuellen Zeitraum
      chartDialogDayOffset = 0;
   else
      chartDialogDayOffset = Math.max(0, chartDialogDayOffset - direction);

   requestChartDialogData();
}

function updateChartDialogRange(start, range)
{
   let to = start == 0 ? new Date() : new Date(start.getTime() + range*24*60*60*1000);
   let from = start == 0 ? new Date(to.getTime() - range*24*60*60*1000) : start;
   let format = { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit' };

   $("#chartDialogRange").html(from.toLocaleString([], format) + ' &ndash; ' + to.toLocaleString([], format)
                               + (chartDialogDayOffset == 0 ? ' (aktuell)' : ''));
   $("#chartDialogNext").prop('disabled', chartDialogDayOffset == 0);
}

function chartDialogKeyDown(event)
{
   if (event.key == 'Escape' || event.keyCode == 27)
      toggleChartDialog("", 0);
   else if (event.key == 'ArrowLeft')
      chartDialogPage(-1);
   else if (event.key == 'ArrowRight')
      chartDialogPage(1);
}

// ---------------------------------
// gauge stuff ...

function polar_to_cartesian(cx, cy, radius, angle)
{
   let radians = (angle - 90) * Math.PI / 180.0;
   return [Math.round((cx + (radius * Math.cos(radians))) * 100) / 100, Math.round((cy + (radius * Math.sin(radians))) * 100) / 100];
};

function svg_circle_arc_path(x, y, radius, start_angle, end_angle)
{
   let start_xy = polar_to_cartesian(x, y, radius, end_angle);
   let end_xy = polar_to_cartesian(x, y, radius, start_angle);
   return "M " + start_xy[0] + " " + start_xy[1] + " A " + radius + " " + radius + " 0 0 0 " + end_xy[0] + " " + end_xy[1];
};

// ---------------------------------
// drag&drop stuff ...


function dragDashboard(ev)
{
   // console.log("drag: " + ev.target.getAttribute('id'));
   ev.originalEvent.dataTransfer.setData("source", ev.target.getAttribute('id'));
}

function dragOverDashboard(ev)
{
   event.preventDefault();
   let target = ev.target;
   target.setAttribute('drop-active', true);
}

function dragLeaveDashboard(ev)
{
   event.preventDefault();
   let target = ev.target;
   target.removeAttribute('drop-active', true);
}

function dropDashboard(ev)
{
   ev.preventDefault();
   let target = ev.target;
   target.removeAttribute('drop-active', true);
   // console.log("drop: " + ev.target.getAttribute('id'));

   let source = document.getElementById(ev.originalEvent.dataTransfer.getData("source"));

   if (source.dataset.dragtype == 'widget') {
      let key = source.getAttribute('id').substring(source.getAttribute('id').indexOf("_") + 1);
      // console.log("drag widget " + key + " from dashboard " + parseInt(actDashboard) + " to " + parseInt(target.getAttribute('id')));
      source.remove();
      socket.send({ "event" : "storedashboards", "object" :
                    { 'action' : 'move',
                      'key' : key,
                      'from' : parseInt(actDashboard),
                      'to': parseInt(target.getAttribute('id').substring(4)) } });

      return;
   }

   if (source.dataset.dragtype != 'dashboard') {
      console.log("drag source not a dashboard");
      return;
   }

   // dragged from the left -> behind the target, from the right -> in front of it

   if (source.compareDocumentPosition(target) & Node.DOCUMENT_POSITION_FOLLOWING)
      target.after(source);
   else
      target.before(source);

   // -> The order for json objects follows a certain set of rules since ES2015,
   //    but it does not (always) follow the insertion order. Simply,
   //    the iteration order is a combination of the insertion order for strings keys,
   //    and ascending order for number keys
   // terefore we use a array instead

   let jOrder = [];
   let i = 0;

   $('#dashboardMenu > button').each(function () {
      if ($(this).attr('data-dragtype') == 'dashboard') {
         let did = $(this).attr('id');
         console.log("add: " + did);
         jOrder[i++] = parseInt(did.substring(4));
      }
   });

   socket.send({ "event" : "storedashboards", "object" : { 'action' : 'order', 'order' : jOrder } });
   socket.send({ "event" : "forcerefresh", "object" : { 'action' : 'dashboards' } });
}

// widgets

function dragWidget(ev)
{
   console.log("drag: " + ev.target.getAttribute('id'));
   ev.dataTransfer.setData("source", ev.target.getAttribute('id'));
}

function dragOver(ev)
{
   event.preventDefault();
   let target = ev.target;
   while (target) {
      if (target.dataset.droppoint)
         break;
      target = target.parentElement;
   }
   target.setAttribute('drop-active', true);
}

function dragLeave(ev)
{
   event.preventDefault();
   let target = ev.target;
   while (target) {
      if (target.dataset.droppoint)
         break;
      target = target.parentElement;
   }
   target.removeAttribute('drop-active', true);
}

function dropWidget(ev)
{
   ev.preventDefault();
   let target = ev.target;
   while (target) {
      if (target.dataset.droppoint)
         break;
      target = target.parentElement;
   }
   target.removeAttribute('drop-active', true);

   let source = document.getElementById(ev.dataTransfer.getData("source"));

   if (source.dataset.dragtype != 'widget') {
      console.log("drag source not a widget");
      return;
   }

   console.log("drop element: " + source.getAttribute('id') + ' on ' + target.getAttribute('id'));
   // dragged from the left -> behind the target, from the right -> in front of it

   if (source.compareDocumentPosition(target) & Node.DOCUMENT_POSITION_FOLLOWING)
      target.after(source);
   else
      target.before(source);

   let json = {};
   $('#widgetContainer > div').each(function () {
      let key = $(this).attr('id').substring($(this).attr('id').indexOf("_") + 1);
      json[key] = dashboards[actDashboard].widgets[key];
   });

   socket.send({ "event" : "storedashboards", "object" : { [actDashboard] : { 'title' : dashboards[actDashboard].title, 'widgets' : json } } });
   socket.send({ "event" : "forcerefresh", "object" : { 'action' : 'dashboards' } });
}

//***************************************************************************
// Dashboard Export / Import
//   file: { "type": "homectld-dashboards", "version": 1, "exported": "<iso time>",
//           "dashboards": [ { "title", "symbol", "group", "order", "options": {...},
//                             "widgets": { "<type>:<address>": {...}, ... } }, ... ] }
//   HTML widgets additionally carry 'snippetName', on import the snippet is
//   assigned by this name (the snippet ids differ between instances)
//***************************************************************************

function dashboardExportSelect()      // choose the dashboards to export (menu dialog)
{
   let ids = Object.keys(dashboards).sort((a, b) => (dashboards[a].order || 0) - (dashboards[b].order || 0));

   if (!ids.length) {
      showInfoDialog({ 'status': -1, 'message': 'Keine Dashboards vorhanden' });
      return;
   }

   let form = document.createElement('div');

   // switches as in the rest of the webif: hidden checkbox + '<label for>' (checkbox.css)

   let switchRow = function(inputId, $text) {
      return $('<div></div>')
         .css('padding', '2px 0px')
         .append($('<input></input>').attr('type', 'checkbox').attr('id', inputId).prop('checked', true))
         .append($('<label></label>')
                 .attr('for', inputId)
                 .css('align-items', 'center')
                 .css('gap', '6px')
                 .append($text));
   };

   let $list = $('<div></div>')
       .css('max-height', '60vh')
       .css('overflow-y', 'auto')
       .css('margin', '6px 0px 0px 0px');

   for (let id of ids) {
      let $row = switchRow('dashExp_' + id,
                           [ $('<span></span>').addClass(dashboards[id].symbol ? dashboards[id].symbol.replace(':', ' ') : ''),
                             $('<span></span>').text(dashboards[id].title || '(ohne Titel)') ]);
      $row.find('input').addClass('dashExportSel').val(id);
      $list.append($row);
   }

   let $all = switchRow('dashExportAll', $('<span></span>').css('font-weight', 'bold').text('Alle'));

   $(form)
      .append($all)
      .append($('<hr></hr>').css('margin', '4px 0px').css('border', '0').css('border-top', '1px solid var(--neutral2)'))
      .append($list);

   $('#dashExportAll', form).change(function() { $('.dashExportSel', form).prop('checked', this.checked); });

   $list.find('.dashExportSel').change(function() {
      $('#dashExportAll', form).prop('checked', $('.dashExportSel:checked', form).length == ids.length);
   });

   $(form).dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      width: 'auto',
      title: 'Dashboards exportieren',
      buttons: {
         'Abbrechen': function() { $(this).dialog('close'); },
         'Exportieren': function() {
            let sel = $('.dashExportSel:checked').map(function() { return $(this).val(); }).get();

            if (!sel.length) {
               showInfoDialog({ 'status': -1, 'message': 'Kein Dashboard gewählt' });
               return;
            }

            dashboardExport(sel);
            $(this).dialog('close');
         }
      },
      close: function() { $(this).dialog('destroy').remove(); }
   });
}

// dashboards may have no title (symbol only) -> identified by the symbol

function dashboardIdentKey(title, symbol)
{
   title = (title || '').trim();
   return title != '' ? 'T:' + title : 'S:' + (symbol || '');
}

function dashboardDisplayName(title, symbol)
{
   title = (title || '').trim();

   if (title != '')
      return title;

   return symbol ? symbol.replace(/^.*:/, '').replace(/^mdi-/, '') + ' (ohne Titel)' : '(ohne Titel)';
}

function dashboardExport(ids)
{
   let list = [];

   ids = ids.map(id => String(id)).filter(id => dashboards[id] != null);
   ids.sort((a, b) => (dashboards[a].order || 0) - (dashboards[b].order || 0));

   for (let id of ids) {
      let d = dashboards[id];
      let widgets = {};

      for (let key in d.widgets || {}) {
         let w = Object.assign({}, d.widgets[key]);

         if (w.snippet != null && typeof htmlWidgets != 'undefined' && htmlWidgets[w.snippet])
            w.snippetName = htmlWidgets[w.snippet].name;

         widgets[key] = w;
      }

      list.push({ 'title': d.title || '', 'symbol': d.symbol || '', 'group': d.group || 0, 'order': d.order || 0,
                  'options': d.options || {}, 'widgets': widgets });
   }

   if (!list.length)
      return;

   let data = { 'type': 'homectld-dashboards', 'version': 1, 'exported': new Date().toISOString(), 'dashboards': list };
   let fileName = list.length == 1
       ? 'dashboard-' + dashboardDisplayName(list[0].title, list[0].symbol).replace(/[^\w\-äöüÄÖÜß]+/g, '_').replace(/^_+|_+$/g, '') + '.json'
       : 'dashboards-' + new Date().toISOString().substring(0, 10) + '.json';

   let url = URL.createObjectURL(new Blob([JSON.stringify(data, undefined, 2)], { type: 'application/json' }));
   let a = document.createElement('a');
   a.href = url;
   a.download = fileName;
   document.body.appendChild(a);
   a.click();
   a.remove();
   setTimeout(function() { URL.revokeObjectURL(url); }, 1000);
}

function dashboardImportSelect()       // file selection (menu dialog)
{
   let input = document.createElement('input');
   input.type = 'file';
   input.accept = '.json,application/json';
   input.style.display = 'none';
   input.addEventListener('change', function() {
      if (input.files.length)
         dashboardImportFile(input.files[0]);
      input.remove();
   });
   document.body.appendChild(input);
   input.click();
}

function dashboardImportFile(file)
{
   let reader = new FileReader();

   reader.onload = function() {
      let list = null;

      try {
         let data = JSON.parse(reader.result);

         if (data && data.type && data.type != 'homectld-dashboards')
            throw new Error('keine Dashboard Datei (type \'' + data.type + '\')');

         list = Array.isArray(data) ? data : data.dashboards;

         if (!Array.isArray(list))
            throw new Error('keine Dashboards gefunden');

         list = list.filter(d => d && typeof d == 'object' &&
                            ((typeof d.title == 'string' && d.title.trim() != '') || (typeof d.symbol == 'string' && d.symbol != '')) &&
                            (d.widgets == null || (typeof d.widgets == 'object' && !Array.isArray(d.widgets))));

         if (!list.length)
            throw new Error('keine Dashboards mit Titel oder Symbol gefunden');
      }
      catch (e) {
         showInfoDialog({ 'status': -1, 'message': 'Import von \'' + file.name + '\' fehlgeschlagen: ' + e.message });
         return;
      }

      dashboardImport(list);
   };

   reader.readAsText(file);
}

// existing titles (symbol if no title) -> ask: overwrite or as copy

function dashboardImport(list)
{
   let byTitle = {};

   for (let d of list)
      d.title = typeof d.title == 'string' ? d.title.trim() : '';

   for (let id in dashboards)
      byTitle[dashboardIdentKey(dashboards[id].title, dashboards[id].symbol)] = id;

   // widgets without sensor on this instance (SPACER, TIME, HTML, ... are no sensors)

   let unknown = 0;

   for (let d of list) {
      for (let key in d.widgets || {}) {
         if (valueFacts[key] == null && !/^(SPACER|TIME|HTML):/.test(key))
            unknown++;
      }
   }

   let conflicts = [...new Set(list.filter(d => byTitle[dashboardIdentKey(d.title, d.symbol)] != null)
                                   .map(d => dashboardDisplayName(d.title, d.symbol)))];
   let form = document.createElement('div');

   $(form).append($('<div></div>').text(list.length + ' Dashboard(s) in der Datei: ' + list.map(d => dashboardDisplayName(d.title, d.symbol)).join(', ')));

   if (unknown)
      $(form).append($('<div></div>')
                     .css('margin-top', '8px')
                     .text(unknown + ' Widget(s) beziehen sich auf Sensoren, die es hier nicht gibt'));

   let buttons = { 'Abbrechen': function() { $(this).dialog('close'); } };

   if (conflicts.length) {
      $(form)
         .append($('<div></div>').css('margin-top', '8px').text('Diese gibt es schon:'))
         .append($('<ul></ul>').append(conflicts.map(n => $('<li></li>').text(n))))
         .append($('<div></div>')
                 .html('<b>Überschreiben</b>: Einstellungen und Widgets der vorhandenen werden ersetzt<br>' +
                       '<b>Als Kopie</b>: sie werden zusätzlich angelegt, mit Titel z. B. als \'' +
                       $('<div></div>').text(conflicts[0]).html() + ' (2)\''));
      buttons['Als Kopie'] = function() { dashboardImportSend(list, byTitle, 'copy'); $(this).dialog('close'); };
      buttons['Überschreiben'] = function() { dashboardImportSend(list, byTitle, 'overwrite'); $(this).dialog('close'); };
   }
   else {
      buttons['Importieren'] = function() { dashboardImportSend(list, byTitle, 'copy'); $(this).dialog('close'); };
   }

   $(form).dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      width: 'auto',
      title: 'Dashboards importieren',
      buttons: buttons,
      close: function() { $(this).dialog('destroy').remove(); }
   });
}

function dashboardImportSend(list, byTitle, mode)
{
   let taken = new Set(Object.keys(byTitle));     // identity keys (see dashboardIdentKey)
   let snippetByName = {};

   if (typeof htmlWidgets != 'undefined') {
      for (let id in htmlWidgets)
         snippetByName[htmlWidgets[id].name] = parseInt(id);
   }

   for (let d of list) {
      let title = d.title;
      let key = dashboardIdentKey(title, d.symbol);
      let id = -1;

      if (byTitle[key] != null && mode == 'overwrite')
         id = parseInt(byTitle[key]);
      else if (title != '' && taken.has(key)) {    // without title the copy stays without title
         let n = 2;
         while (taken.has(dashboardIdentKey(title + ' (' + n + ')')))
            n++;
         title = title + ' (' + n + ')';
      }

      taken.add(dashboardIdentKey(title, d.symbol));

      let widgets = {};

      for (let key in d.widgets || {}) {
         let w = d.widgets[key];

         if (w == null || typeof w != 'object' || Array.isArray(w))
            w = {};
         else
            w = Object.assign({}, w);

         if (w.snippetName != null) {
            if (snippetByName[w.snippetName] != null)
               w.snippet = snippetByName[w.snippetName];
            delete w.snippetName;
         }

         if (!Object.keys(w).length)
            w = '';            // empty -> the daemon creates the default options of the widget

         widgets[key] = w;
      }

      let options = d.options && typeof d.options == 'object' && !Array.isArray(d.options) ? d.options : {};

      // one message per dashboard, the key -1 (new dashboard) can only be used once per object

      socket.send({ 'event': 'storedashboards', 'object': { [id]: { 'title': title, 'symbol': d.symbol || '',
                                                                   'group': parseInt(d.group) || 0,
                                                                   'options': options, 'widgets': widgets } } });
   }

   socket.send({ 'event': 'forcerefresh', 'object': { 'action': 'dashboards' } });
}
