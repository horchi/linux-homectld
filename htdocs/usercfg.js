/*
 *  usercfg.js
 *
 *  (c) 2020-2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

// !! sync this arry with UserRights of websock.h !!

var rights = [ "View",
               "Control",
               "Full Control",
               "Settings",
               "Admin" ];

// the icons of the rights (same order)

var rightIcons = [ "mdi-eye-outline",
                   "mdi-gesture-tap",
                   "mdi-tune-vertical",
                   "mdi-cog-outline",
                   "mdi-shield-crown-outline" ];

// -------------------------------
// init - the setup page 'User': a card per user, the styles are in usercfg.css

function initUserConfig(users)
{
   $('#container').removeClass('hidden');
   $('#container').empty();

   prepareSetupMenu();

   // the layout: cards (grid) or one line per user over the full width (list), remembered per browser

   let layout = 'grid';

   try { layout = localStorage.getItem(storagePrefix + 'userLayout') || 'grid'; } catch (e) {}

   let grid = $('<div></div>').addClass('usrGrid').toggleClass('usrList', layout == 'list');

   let layoutButton = function(value, icon, title) {
      return $('<button></button>')
         .attr('type', 'button')
         .attr('title', title)
         .addClass('rounded-border usrLayoutBtn mdi ' + icon)
         .toggleClass('usrLayoutActive', layout == value)
         .click(function() {
            layout = value;
            try { localStorage.setItem(storagePrefix + 'userLayout', layout); } catch (e) {}
            grid.toggleClass('usrList', layout == 'list');
            $(this).addClass('usrLayoutActive').siblings().removeClass('usrLayoutActive');
         });
   };

   $('#container').append($('<div></div>')
                          .attr('id', 'userContainer')
                          .addClass('usrPage')
                          .append($('<div></div>').addClass('usrPageHead')
                                  .append($('<span></span>').addClass('mdi mdi-account-group-outline'))
                                  .append($('<span></span>').text('Benutzer'))
                                  .append($('<span></span>').addClass('usrCount').text(users.length))
                                  .append($('<span></span>').addClass('usrLayout')
                                          .append(layoutButton('grid', 'mdi-view-grid-outline', 'Karten'))
                                          .append(layoutButton('list', 'mdi-view-agenda-outline', 'Liste (eine Zeile pro Benutzer)'))))
                          .append(grid));

   for (let i = 0; i < users.length; i++) {
      let item = users[i];
      let rightsDiv = $('<div></div>').addClass('usrRights');

      for (let b = 0; b < rights.length; b++) {
         let id = 'bit_' + item.user + b;

         rightsDiv.append($('<span></span>').addClass('usrRight')
                          .append($('<input></input>')
                                  .attr('id', id)
                                  .attr('type', 'checkbox')
                                  .prop('checked', !!(item.rights & Math.pow(2, b))))
                          .append($('<label></label>')
                                  .attr('for', id)
                                  .append($('<span></span>').addClass('mdi ' + rightIcons[b]))
                                  .append($('<span></span>').text(rights[b]))));
      }

      let action = function(icon, text, act, cls = '') {
         return $('<button></button>')
            .attr('type', 'button')
            .addClass('rounded-border buttonOptions usrAction ' + cls)
            .append($('<span></span>').addClass('mdi ' + icon))
            .append($('<span></span>').text(text))
            .click(function() { userConfig(item.user, act); });
      };

      let card = $('<div></div>').addClass('usrCard')
          .attr('id', 'row_' + item.user)
          .append($('<div></div>').addClass('usrHead')
                  .append($('<span></span>').addClass('usrAvatar').text(item.user.substring(0, 1).toUpperCase()))
                  .append($('<span></span>').addClass('usrName').text(item.user))
                  .append(item.rights & 0x10 ? $('<span></span>').addClass('usrBadge').text('Admin') : null))
          .append(rightsDiv)
          .append($('<div></div>').addClass('usrActions')
                  .append(action('mdi-content-save-outline', 'Speichern', 'store', 'usrSave'))
                  .append(action('mdi-key-change', 'Reset Token', 'resettoken'))
                  .append(action('mdi-lock-reset', 'Reset Passwort', 'resetpwd'))
                  .append(action('mdi-trash-can-outline', 'Löschen', 'delete', 'usrDelete')));

      rightsDiv.on('change', 'input', function() { card.addClass('usrChanged'); });
      grid.append(card);
   }

   // the new user

   grid.append($('<div></div>').addClass('usrCard usrNew')
               .attr('id', 'addUserDiv')
               .append($('<div></div>').addClass('usrHead')
                       .append($('<span></span>').addClass('usrAvatar mdi mdi-account-plus-outline'))
                       .append($('<span></span>').addClass('usrName').text('Neuer Benutzer')))
               .append($('<div></div>').addClass('usrFields')
                       .append($('<input></input>').attr('id', 'input_user').attr('placeholder', 'User')
                               .attr('autocomplete', 'off').addClass('rounded-border input'))
                       .append($('<input></input>').attr('id', 'input_passwd').attr('placeholder', 'Passwort')
                               .attr('type', 'password').attr('autocomplete', 'new-password').addClass('rounded-border input')))
               .append($('<div></div>').addClass('usrActions')
                       .append($('<button></button>')
                               .attr('type', 'button')
                               .addClass('rounded-border buttonOptions usrAction usrSave')
                               .append($('<span></span>').addClass('mdi mdi-plus'))
                               .append($('<span></span>').text('Anlegen'))
                               .click(function() { addUser(); }))));

   // calc container size

   $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('footer') - sab - 10);
   window.onresize = function() {
      $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('footer') - sab - 10);
   };
}

// the page of the logged in user (menu 'User'): logout and the password

function initUser()
{
   $('#container').removeClass('hidden');
   setupContainerHeight();

   let user = localStorage.getItem(storagePrefix + 'User') || '';

   $('#container').empty()
      .append($('<div></div>')
              .attr('id', 'userContainer')
              .addClass('usrPage')
              .append($('<div></div>').addClass('usrCard usrSelf')
                      .append($('<div></div>').addClass('usrHead')
                              .append($('<span></span>').addClass('usrAvatar').text(user.substring(0, 1).toUpperCase() || '?'))
                              .append($('<span></span>').addClass('usrName').text(user || 'nicht angemeldet'))
                              .append($('<button></button>')
                                      .attr('type', 'button')
                                      .addClass('rounded-border buttonOptions usrAction usrLogout')
                                      .append($('<span></span>').addClass('mdi mdi-logout'))
                                      .append($('<span></span>').text('Logout'))
                                      .click(function() { doLogout(); })))
                      .append($('<div></div>').addClass('usrSection').text('Passwort ändern'))
                      .append($('<div></div>').addClass('usrFields')
                              .append($('<input></input>').attr('id', 'input_passwd').attr('type', 'password')
                                      .attr('placeholder', 'neues Passwort').attr('autocomplete', 'new-password')
                                      .addClass('rounded-border input'))
                              .append($('<input></input>').attr('id', 'input_passwd2').attr('type', 'password')
                                      .attr('placeholder', 'wiederholen').attr('autocomplete', 'new-password')
                                      .addClass('rounded-border input')))
                      .append($('<div></div>').addClass('usrActions')
                              .append($('<button></button>')
                                      .attr('type', 'button')
                                      .addClass('rounded-border buttonOptions usrAction usrSave')
                                      .append($('<span></span>').addClass('mdi mdi-content-save-outline'))
                                      .append($('<span></span>').text('Speichern'))
                                      .click(function() { chpwd(); })))));
}

window.userConfig = function(user, action)
{
   // console.log("userConfig(" + action + ", " + user + ")");

   if (action == "delete") {
      if (confirm("User '" + user + "' löschen?"))
         socket.send({ "event": "storeuserconfig", "object":
                       { "user": user,
                         "action": "del" }});
   }
   else if (action == "store") {
      var rightsMask = 0;
      for (var b = 0; b < rights.length; b++) {
         if ($("#bit_" + user + b).is(":checked"))
            rightsMask += Math.pow(2, b); // 2 ** b;
      }

      socket.send({ "event": "storeuserconfig", "object":
                    { "user": user,
                      "action": "store",
                      "rights": rightsMask }});
   }
   else if (action == "resetpwd") {
      var passwd = prompt("neues Passwort", "");

      // only with a password (the send was outside the 'if', also on 'Abbrechen'); the password isn't logged

      if (passwd && passwd != "")
         socket.send({ "event": "storeuserconfig", "object":
                       { "user": user,
                         "passwd": $.md5(passwd),
                         "action": "resetpwd" }});
   }
   else if (action == "resettoken") {
      socket.send({ "event": "storeuserconfig", "object":
                    { "user": user,
                      "action": "resettoken" }});
   }
}

window.chpwd  = function()
{
   var user = localStorage.getItem(storagePrefix + 'User');

   if (user && user != "") {
      console.log("Change password of " + user);

      if ($("#input_passwd").val() != "" && $("#input_passwd").val() == $("#input_passwd2").val()) {
         socket.send({ "event": "changepasswd", "object":
                       { "user": user,
                         "passwd": $.md5($("#input_passwd").val()),
                         "action": "resetpwd" }});

         $("#input_passwd").val("");
         $("#input_passwd2").val("");
      }
      else
         showInfoDialog({'message' : 'Passwords not match or empty'});
   }
   else
      showInfoDialog({'message' : 'Missing login!'});
}

window.addUser = function()
{
   // console.log("Add user: " + $("#input_user").val());

   socket.send({ "event": "storeuserconfig", "object":
                 { "user": $("#input_user").val(),
                   "passwd": $.md5($("#input_passwd").val()),
                   "action": "add" }
               });

   $("#input_user").val("");
   $("#input_passwd").val("");
}
