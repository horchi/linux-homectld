/*
 *  vdr.js
 *
 *  (c) 2020-2024 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

// SVG icons (stroke based, follow currentColor)

const vdrIcons = {
   menu:  '<path d="M4 7h16M4 12h16M4 17h16"/>',
   info:  '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.01"/>',
   back:  '<path d="M9 14l-5-5 5-5"/><path d="M4 9h10a6 6 0 0 1 0 12h-3"/>',
   up:    '<path d="M6 15l6-6 6 6"/>',
   down:  '<path d="M6 9l6 6 6-6"/>',
   left:  '<path d="M15 6l-6 6 6 6"/>',
   right: '<path d="M9 6l6 6-6 6"/>',
   plus:  '<path d="M12 5v14M5 12h14"/>',
   minus: '<path d="M5 12h14"/>',
   mute:  '<path d="M4 9v6h4l5 4V5L8 9z"/><path d="M16 9l5 6M21 9l-5 6"/>',
   list:  '<path d="M9 6h11M9 12h11M9 18h11"/><path d="M4.5 6h.01M4.5 12h.01M4.5 18h.01"/>',
   close: '<path d="M6 6l12 12M18 6L6 18"/>',
   timer: '<circle cx="12" cy="13" r="8"/><path d="M12 9v4l2.5 2.5M9 2h6"/>',
   power: '<path d="M12 3v8"/><path d="M6.3 6.8a8 8 0 1 0 11.4 0"/>',
   num:   '<circle cx="6" cy="6" r="1.3"/><circle cx="12" cy="6" r="1.3"/><circle cx="18" cy="6" r="1.3"/>' +
          '<circle cx="6" cy="12" r="1.3"/><circle cx="12" cy="12" r="1.3"/><circle cx="18" cy="12" r="1.3"/>' +
          '<circle cx="6" cy="18" r="1.3"/><circle cx="12" cy="18" r="1.3"/><circle cx="18" cy="18" r="1.3"/>'
};

var vdrProgressTimer = null;
var vdrActual = null;
var vdrSocket = null;          // own socket to osd2web, the one to homectld stays open
var vdrCurrent = null;         // the selected VDR { name, url, mac }
var vdrOnline = false;
var vdrWakingSince = 0;
var vdrEpgView = null;         // open EPG list { mode: 'now'|'schedule', channelid }

// config 'vdr' (ctStringSet): 'Name;Host:Port;MAC,...', the fields are recognized by their
//   content (like vdr.c), name and MAC are optional, 'OFF' allows the power off by the remote

function vdrList()
{
   let list = [];

   for (let entry of (config.vdr || '').split(',')) {
      let vdr = { name: '', url: '', mac: '', powerOff: false };
      let rest = [];

      for (let p of entry.split(';').map(s => s.trim()).filter(s => s != '')) {
         if (p.toUpperCase() == 'OFF')
            vdr.powerOff = true;
         else if (vdr.mac == '' && /^[0-9a-f]{2}([:-][0-9a-f]{2}){5}$/i.test(p))
            vdr.mac = p;
         else if (vdr.url == '' && /^[^:]+:[0-9]+$/.test(p))
            vdr.url = p;
         else
            rest.push(p);
      }

      if (vdr.url == '' && rest.length)
         vdr.url = rest.shift();

      if (rest.length)
         vdr.name = rest[0];

      if (vdr.name == '')
         vdr.name = vdr.url.split(':')[0];

      if (vdr.url != '')
         list.push(vdr);
   }

   return list;
}

function vdrIcon(name)
{
   return '<svg class="vdrIcon" viewBox="0 0 24 24" aria-hidden="true">' + vdrIcons[name] + '</svg>';
}

// key button, 'repeat' keys repeat while pressed

function vdrKey(key, content, cls, repeat, title)
{
   return '<button type="button" class="vdrKey ' + (cls || '') + '" data-key="' + key + '"' +
      (repeat ? ' data-repeat="1"' : '') + ' title="' + (title || key) + '">' + content + '</button>';
}

function initVdr()
{
   $('#container').removeClass('hidden');

   let num = '';

   for (const k of ['1','2','3','4','5','6','7','8','9'])
      num += vdrKey(k, k, 'vdrNumKey');

   num += '<span></span>' + vdrKey('0', '0', 'vdrNumKey') + '<span></span>';

   const vdrs = vdrList();
   let selected = null;

   try { selected = localStorage.getItem(storagePrefix + 'Vdr'); } catch (e) {}

   vdrCurrent = vdrs.find(v => v.name == selected) || vdrs[0] || null;

   let select = '';

   for (const v of vdrs)
      select += '<option value="' + v.name + '"' + (v == vdrCurrent ? ' selected' : '') + '>' + v.name + '</option>';

   document.getElementById("container").innerHTML =
      '<div class="vdrContent">' +
      '  <div class="vdrPresent" onclick="vdrEpgOpen(\'schedule\')" title="Programm des Kanals">' +
      '    <div class="vdrChannel">' +
      '      <img id="vdrChannelLogo" class="vdrChannelLogo" style="display:none;" onerror="this.style.display=\'none\'" onload="this.style.display=\'\'"/>' +
      '      <div id="vdrChannel" class="vdrChannelName"></div>' +
      '    </div>' +
      '    <div class="vdrEpg">' +
      '      <div class="vdrEpgLine"><span id="vdrStartTime" class="vdrTime"></span><span id="vdrTitle" class="vdrTitle"></span></div>' +
      '      <div id="vdrShorttext" class="vdrShorttext"></div>' +
      '      <div class="vdrProgress"><div id="vdrProgressBar"></div></div>' +
      '      <div class="vdrEpgLine vdrEpgNext"><span id="vdrStartTimeNext" class="vdrTime"></span><span id="vdrTitleNext" class="vdrTitle"></span></div>' +
      '    </div>' +
      '  </div>' +
      '  <div id="vdrRemote" class="vdrRemote">' +
      '    <div class="vdrHeadRow">' +
      '      <span class="vdrSelectWrap' + (vdrs.length > 1 ? '' : ' vdrSingle') + '">' +
      '        <span id="vdrState" class="vdrState"></span>' +
      '        <select id="vdrSelect" class="vdrSelect" onchange="vdrSelect(this.value)"' + (vdrs.length > 1 ? '' : ' disabled') + '>' + select + '</select>' +
      '      </span>' +
      '      <button type="button" id="vdrPower" class="vdrKey vdrRound vdrPower" title="Power" onclick="vdrPower()">' + vdrIcon('power') + '</button>' +
      '    </div>' +
      '    <div class="vdrFnRow">' +
             vdrKey('Menu', vdrIcon('menu'), 'vdrRound', false, 'Menü') +
             vdrKey('Info', vdrIcon('info'), 'vdrRound', false, 'Info') +
             vdrKey('Back', vdrIcon('back'), 'vdrRound', false, 'Zurück') +
      '      <button type="button" class="vdrKey vdrRound vdrEpgKey" title="Was läuft jetzt" onclick="vdrEpgOpen(\'now\')">' + vdrIcon('list') + '</button>' +
      '      <button type="button" class="vdrKey vdrRound vdrNumToggle" title="Ziffern" onclick="vdrToggleNum()">' + vdrIcon('num') + '</button>' +
      '    </div>' +
      '    <div class="vdrPadArea">' +
      '      <div class="vdrDpad">' +
               vdrKey('Up', vdrIcon('up'), 'vdrWedge vdrWedgeUp', true) +
               vdrKey('Down', vdrIcon('down'), 'vdrWedge vdrWedgeDown', true) +
               vdrKey('Left', vdrIcon('left'), 'vdrWedge vdrWedgeLeft', true) +
               vdrKey('Right', vdrIcon('right'), 'vdrWedge vdrWedgeRight', true) +
               vdrKey('Ok', 'OK', 'vdrOk') +
      '      </div>' +
      '    </div>' +
      '    <div class="vdrRockRow">' +
      '      <div class="vdrRocker">' +
               vdrKey('Volume+', vdrIcon('plus'), '', true, 'Lauter') +
      '        <span class="vdrRockerLabel">VOL</span>' +
               vdrKey('Volume-', vdrIcon('minus'), '', true, 'Leiser') +
      '      </div>' +
      '      <div class="vdrRockMid">' +
               vdrKey('Mute', vdrIcon('mute'), 'vdrRound', false, 'Stumm') +
      '      </div>' +
      '      <div class="vdrRocker">' +
               vdrKey('Channel+', vdrIcon('up'), '', true, 'Kanal +') +
      '        <span class="vdrRockerLabel">CH</span>' +
               vdrKey('Channel-', vdrIcon('down'), '', true, 'Kanal -') +
      '      </div>' +
      '    </div>' +
      '    <div class="vdrNumPad">' + num + '</div>' +
      '    <div class="vdrColorRow">' +
             vdrKey('Red', '', 'vdrColor vdrRed', false, 'Rot') +
             vdrKey('Green', '', 'vdrColor vdrGreen', false, 'Grün') +
             vdrKey('Yellow', '', 'vdrColor vdrYellow', false, 'Gelb') +
             vdrKey('Blue', '', 'vdrColor vdrBlue', false, 'Blau') +
      '    </div>' +
      '  </div>' +  // vdrRemote
      '  <div id="vdrEpgList" class="vdrEpgList" style="display:none;">' +
      '    <div class="vdrEpgHead">' +
      '      <button type="button" id="vdrEpgBack" class="vdrKey vdrRound" title="Zurück" onclick="vdrEpgBack()">' + vdrIcon('back') + '</button>' +
      '      <div id="vdrEpgTitle" class="vdrEpgTitle"></div>' +
      '      <button type="button" class="vdrKey vdrRound" title="Schließen" onclick="vdrEpgClose()">' + vdrIcon('close') + '</button>' +
      '    </div>' +
      '    <div id="vdrEpgRows" class="vdrEpgRows"></div>' +
      '  </div>' +
      '</div>';

   document.getElementById('vdrEpgRows').addEventListener('click', vdrEpgClick);

   vdrInitKeys(document.getElementById('vdrRemote'));
   vdrConnect();

   if (vdrProgressTimer)
      clearInterval(vdrProgressTimer);

   vdrProgressTimer = setInterval(vdrUpdateProgress, 30000);

   // calc container size

   $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('dashboardMenu') - getTotalHeightOf('footer') - sab - 15);
   window.onresize = function() {
      $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('dashboardMenu') - getTotalHeightOf('footer') - sab - 15);
   };
}

function vdrToggleNum()
{
   $('#vdrRemote').toggleClass('vdrShowNum');
   vdrHaptic();
}

function vdrHaptic()
{
   if (navigator.vibrate)
      navigator.vibrate(8);
}

// pointer handling: send on press, keys with data-repeat repeat while hold

function vdrInitKeys(remote)
{
   let timer = null;

   const stop = function() {
      if (timer) {
         clearTimeout(timer);
         timer = null;
      }
   };

   remote.addEventListener('pointerdown', function(ev) {
      const btn = ev.target.closest('.vdrKey[data-key]');

      if (!btn || ev.button > 0)
         return;

      ev.preventDefault();
      stop();
      vdrHaptic();
      vdrKeyPress(btn.dataset.key);

      if (btn.dataset.repeat) {
         const repeat = function() {
            vdrKeyPress(btn.dataset.key);
            timer = setTimeout(repeat, 150);
         };
         timer = setTimeout(repeat, 450);
      }
   });

   for (const evName of ['pointerup', 'pointercancel', 'pointerleave'])
      remote.addEventListener(evName, stop);

   remote.addEventListener('pointerout', function(ev) {
      if (ev.target.closest('.vdrKey[data-repeat]'))
         stop();
   });

   remote.addEventListener('contextmenu', function(ev) { ev.preventDefault(); });
}

function vdrUpdateProgress()
{
   const bar = document.getElementById('vdrProgressBar');

   if (!bar) {
      clearInterval(vdrProgressTimer);
      vdrProgressTimer = null;
      return;
   }

   const present = vdrActual ? vdrActual.present : null;

   if (!present || !present.starttime || !present.duration) {
      bar.parentNode.style.visibility = 'hidden';
      return;
   }

   let pct = (Date.now() / 1000 - present.starttime) * 100 / present.duration;
   pct = Math.max(0, Math.min(100, pct));
   bar.parentNode.style.visibility = '';
   bar.style.width = pct + '%';
}

function dispatchVdrMessage(event, jMessage)
{
   console.log("VDR: Event", event);

   if (event == 'actual') {
      let actual = jMessage.object;
      const start = new Date(actual.present.starttime * 1000)
      const startNext = new Date(actual.following.starttime * 1000)
      console.log("VDR: Got", event, JSON.stringify(actual, undefined, 4));

      $('#vdrChannel').html(actual.channel.channelname);
      vdrSetChannelLogo(actual.channel);

      $('#vdrStartTime').html(start.toTimeLocal());
      $('#vdrTitle').html(actual.present.title);
      $('#vdrShorttext').html(actual.present.shorttext);

      $('#vdrStartTimeNext').html(startNext.toTimeLocal());
      $('#vdrTitleNext').html(actual.following.title);
      vdrActual = actual;
      vdrUpdateProgress();
      vdrEpgMarkCurrent();
   }
   else if (event == 'epg') {
      vdrEpgShow(jMessage.object);
   }
}

function vdrSetChannelLogo(channel)
{
   // osd2web serves the logos by HTTP on the port of its websocket

   let logo = document.getElementById('vdrChannelLogo');

   if (!logo)
      return;

   if (!channel || !channel.channelname) {
      logo.removeAttribute('src');
      logo.style.display = 'none';
      return;
   }

   if (!vdrCurrent)
      return;

   const src = vdrLogoUrl(channel);

   if (logo.getAttribute('src') != src)
      logo.setAttribute('src', src);
}

function vdrLogoUrl(channel)
{
   return 'http://' + vdrCurrent.url + '/data/channellogo?name=' + encodeURIComponent(channel.channelname) +
      '&id=' + encodeURIComponent(channel.channelid || '');
}

function vdrKeyPress(key)
{
   if (key == undefined || key == "")
      return;

   if (!vdrSocket || !vdrOnline)
      return;

   vdrSocket.send({ "event": "keypress",
                    "object": { "key": key, "repeat": 1 }
                  });
}

//***************************************************************************
// Connection to osd2web of the selected VDR
//***************************************************************************

function vdrSelect(name)
{
   vdrCurrent = vdrList().find(v => v.name == name) || null;

   try { localStorage.setItem(storagePrefix + 'Vdr', name); } catch (e) {}

   vdrConnect();
}

function vdrConnect()
{
   vdrDisconnect();
   vdrClearEpg();

   if (!vdrCurrent)
      return;

   const vdr = vdrCurrent;
   console.log("VDR: connecting", vdr.name, vdr.url);

   // a reconnect already scheduled by WebSocketClient may open a closed client again,
   //   therefore each callback checks if its client is still the current one

   const sock = new WebSocketClient({
      url: 'ws://' + vdr.url,
      protocol: 'osd2vdr',
      autoReconnectInterval: 2000,   // keeps trying, after a wakeup it connects by itself
      onopen: function() {
         if (sock !== vdrSocket) {
            sock.autoReconnectInterval = 0;
            sock.close();
            return;
         }

         vdrOnline = true;
         vdrWakingSince = 0;
         vdrUpdateState();

         let token = '', user = '';

         try {
            token = localStorage.getItem(storagePrefix + 'Token') || '';
            user = localStorage.getItem(storagePrefix + 'User') || '';
         } catch (e) {}

         sock.send({ "event" : "login", "object" :
                          { "type" : "active",
                            "user" : user,
                            "page" : currentPage,
                            "token" : token,
                            "url" : window.location.href }
                        });
      },
      onclose: function() {
         if (sock !== vdrSocket)
            return;

         vdrOnline = false;
         vdrUpdateState();
      },
      onmessage: function(msg) {
         if (sock !== vdrSocket)
            return;

         let jMessage = JSON.parse(msg.data);
         dispatchVdrMessage(jMessage.event, jMessage);
      }
   });

   vdrSocket = sock;
   vdrUpdateState();
}

function vdrDisconnect()
{
   if (vdrSocket) {
      vdrSocket.autoReconnectInterval = 0;
      try { vdrSocket.close(); } catch (e) {}
      vdrSocket = null;
   }

   vdrOnline = false;
}

function vdrClearEpg()
{
   vdrActual = null;
   vdrEpgClose();

   for (const id of ['vdrChannel', 'vdrStartTime', 'vdrTitle', 'vdrShorttext', 'vdrStartTimeNext', 'vdrTitleNext'])
      $('#' + id).html('');

   vdrSetChannelLogo(null);
   vdrUpdateProgress();
}

function vdrUpdateState()
{
   const waking = !vdrOnline && vdrWakingSince && Date.now() - vdrWakingSince < 120000;
   const state = vdrOnline ? 'online' : waking ? 'waking' : 'offline';

   $('#vdrRemote').attr('data-state', state);
   $('#vdrState').attr('title', state == 'online' ? 'verbunden' : waking ? 'wird geweckt ...' : 'nicht erreichbar');
   const offLocked = vdrOnline && vdrCurrent && !vdrCurrent.powerOff;

   $('#vdrPower')
      .toggleClass('vdrPowerLocked', !!offLocked)
      .attr('title', offLocked ? 'Ausschalten nicht freigegeben (Option OFF)'
            : vdrOnline ? 'VDR ausschalten' : 'VDR einschalten (Wake-on-LAN)');

   if (waking)
      setTimeout(vdrUpdateState, 5000);
}

// power: offline -> wakeup by homectld (Wake-on-LAN), online -> 'Power' key to the VDR

function vdrPower()
{
   if (!vdrCurrent)
      return;

   vdrHaptic();

   if (vdrOnline) {
      if (!vdrCurrent.powerOff)
         return;

      confirmDialog(function() {
         vdrSocket.send({ "event": "keypress", "object": { "key": "Power", "repeat": 1 } });
         socket.send({ "event" : "vdrwakeup", "object" : { "name" : vdrCurrent.name, "action" : "poweroff" } });   // for the log
      }, "VDR '" + vdrCurrent.name + "' ausschalten?", 'Ausschalten', 'Abbrechen');
      return;
   }

   socket.send({ "event" : "vdrwakeup", "object" : { "name" : vdrCurrent.name } });
   vdrWakingSince = Date.now();
   vdrUpdateState();
}

//***************************************************************************
// EPG list (osd2web 'epg' request)
//   'now'      - what's on now, all channels; click on the logo -> schedule of the channel
//   'schedule' - events of one channel
//   click on an event switches to its channel
//***************************************************************************

function vdrEscape(s)
{
   return String(s == undefined ? '' : s).replace(/[&<>"']/g, c =>
      ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
}

function vdrEpgOpen(mode, channelid)
{
   if (!vdrSocket || !vdrOnline)
      return;

   vdrHaptic();

   if (mode == 'schedule' && !channelid)
      channelid = vdrActual && vdrActual.channel ? vdrActual.channel.channelid : '';

   vdrEpgView = { mode: mode, channelid: channelid || '' };

   $('#vdrEpgTitle').html(mode == 'now' ? 'Was läuft jetzt' : '');
   $('#vdrEpgBack').css('visibility', mode == 'schedule' ? 'visible' : 'hidden');
   $('#vdrEpgRows').html('<div class="vdrEpgInfo">lade ...</div>');
   $('#vdrEpgList').css('display', '');

   let request = { "mode": mode };

   if (mode == 'schedule' && channelid)
      request.channelid = channelid;

   vdrSocket.send({ "event": "epg", "object": request });
}

function vdrEpgClose()
{
   vdrEpgView = null;
   $('#vdrEpgList').css('display', 'none');
   $('#vdrEpgRows').html('');
}

function vdrEpgBack()
{
   if (vdrEpgView && vdrEpgView.mode == 'schedule')
      vdrEpgOpen('now');
   else
      vdrEpgClose();
}

function vdrEpgShow(epg)
{
   if (!vdrEpgView || !epg || epg.mode != vdrEpgView.mode)
      return;

   const now = Date.now() / 1000;
   const currentId = vdrActual && vdrActual.channel ? vdrActual.channel.channelid : '';
   const days = ['So', 'Mo', 'Di', 'Mi', 'Do', 'Fr', 'Sa'];
   let html = '';
   let lastDay = '';

   if (epg.mode == 'schedule') {
      if (!epg.channel || (vdrEpgView.channelid && epg.channel.channelid != vdrEpgView.channelid))
         return;

      vdrEpgView.channelid = epg.channel.channelid;
      $('#vdrEpgTitle').html('<img class="vdrEpgTitleLogo" src="' + vdrLogoUrl(epg.channel) + '" onerror="this.remove()"/>' +
                             '<span>' + vdrEscape(epg.channel.channelname) + '</span>');
   }

   for (const ev of epg.events || []) {
      const channel = epg.mode == 'now' ? ev.channel : epg.channel;

      if (!channel)
         continue;

      const start = new Date(ev.starttime * 1000);
      const end = new Date(ev.endtime * 1000);
      const running = ev.starttime <= now && ev.endtime > now;
      const timer = ev.timermatch == 'full' || ev.timermatch == 'partial' || ev.hastimer;
      let pct = running && ev.duration ? Math.max(0, Math.min(100, (now - ev.starttime) * 100 / ev.duration)) : 0;

      if (epg.mode == 'schedule') {
         const day = days[start.getDay()] + ' ' + start.getDate() + '.' + (start.getMonth() + 1) + '.';

         if (day != lastDay) {
            html += '<div class="vdrEpgDay">' + day + '</div>';
            lastDay = day;
         }
      }

      html += '<div class="vdrEpgRow' + (epg.mode == 'now' && channel.channelid == currentId ? ' vdrEpgCurrent' : '') +
         (running ? ' vdrEpgRunning' : '') + '" data-channelid="' + vdrEscape(channel.channelid) + '">';

      if (epg.mode == 'now')
         html += '<div class="vdrEpgLogo" data-schedule="' + vdrEscape(channel.channelid) + '" title="Programm von ' + vdrEscape(channel.channelname) + '">' +
            '<img loading="lazy" src="' + vdrLogoUrl(channel) + '" onerror="this.remove()"/>' +
            '<span>' + vdrEscape(channel.channelname) + '</span></div>';

      html += '<div class="vdrEpgEvent">' +
         '<div class="vdrEpgLine"><span class="vdrTime">' + start.toTimeLocal() +
         (epg.mode == 'now' ? ' - ' + end.toTimeLocal() : '') + '</span>' +
         '<span class="vdrTitle">' + vdrEscape(ev.title) + '</span>' +
         (timer ? '<span class="vdrEpgTimer' + (ev.timermatch == 'partial' ? ' vdrEpgPartial' : '') + '" title="Timer">' + vdrIcon('timer') + '</span>' : '') +
         '</div>' +
         (ev.shorttext && ev.shorttext != ev.title ? '<div class="vdrShorttext">' + vdrEscape(ev.shorttext) + '</div>' : '') +
         (running ? '<div class="vdrProgress"><div style="width:' + pct + '%"></div></div>' : '') +
         '</div></div>';
   }

   if (html == '')
      html = '<div class="vdrEpgInfo">keine EPG Daten</div>';

   const rows = document.getElementById('vdrEpgRows');
   rows.innerHTML = html;
   rows.scrollTop = 0;

   const current = rows.querySelector('.vdrEpgCurrent');

   if (current)
      current.scrollIntoView({ block: 'center' });
}

// mark the current channel in the 'now' list (after switching)

function vdrEpgMarkCurrent()
{
   if (!vdrEpgView || vdrEpgView.mode != 'now' || !vdrActual || !vdrActual.channel)
      return;

   for (const row of document.querySelectorAll('#vdrEpgRows .vdrEpgRow'))
      row.classList.toggle('vdrEpgCurrent', row.dataset.channelid == vdrActual.channel.channelid);
}

function vdrEpgClick(ev)
{
   const logo = ev.target.closest('[data-schedule]');

   if (logo) {
      vdrEpgOpen('schedule', logo.dataset.schedule);
      return;
   }

   const row = ev.target.closest('.vdrEpgRow');

   if (!row || !vdrSocket || !vdrOnline)
      return;

   vdrHaptic();
   vdrSocket.send({ "event": "switch", "object": { "channelid": row.dataset.channelid } });
}
