/*
 *  usercfg.js
 *
 *  (c) 2020 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

// !!  sync this arry with UserRights of websock.h  !!

var rights = [ "View",
               "Control",
               "Full Control",
               "Settings",
               "Admin" ];

// -------------------------------
// init

function initUserConfig(users, root)
{
   var table = document.getElementById("userTable");
   table.innerHTML = "";

   for (var i = 0; i < users.length; i++)
   {
      var item = users[i];

      var html = "<td id=\"row_" + item.user + "\" >" + item.user + "</td>";
      html += "<td>";
      for (var b = 0; b < rights.length; b++) {
         var checked = item.rights & Math.pow(2, b); // (2 ** b);
         html += "<input id=\"bit_" + item.user + b + "\" class=\"rounded-border input\" style=\"width:auto;\" type=\"checkbox\" " + (checked ? "checked" : "") + "/>"
         html += "<span style=\"padding-right:20px; padding-left:5px;\">" + rights[b] + "</span>";
      }
      html += "</td>";
      html += "<td>";
      html += "<button class=\"rounded-border\" style=\"margin-right:10px;\" onclick=\"userConfig('" + item.user + "', 'store')\">Speichern</button>";
      html += "<button class=\"rounded-border\" style=\"margin-right:10px;\" onclick=\"userConfig('" + item.user + "', 'resettoken')\">Reset Token</button>";
      html += "<button class=\"rounded-border\" style=\"margin-right:10px;\" onclick=\"userConfig('" + item.user + "', 'resetpwd')\">Reset Passwort</button>";
      html += "<button class=\"rounded-border\" style=\"margin-right:10px;\" onclick=\"userConfig('" + item.user + "', 'delete')\">Löschen</button>";
      html += "</td>";

      var elem = document.createElement("tr");
      elem.innerHTML = html;
      table.appendChild(elem);
   }

   html =  "  <span>User: </span><input id=\"input_user\" class=\"rounded-border input\"/>";
   html += "  <span>Passwort: </span><input id=\"input_passwd\" class=\"rounded-border input\"/>";
   html += "  <button class=\"rounded-border button2\" onclick=\"addUser()\">+</button>";

   document.getElementById("addUserDiv").innerHTML = html;
}

window.userConfig = function(user, action)
{
   // console.log("userConfig(" + action + ", " + user + ")");

   if (action == "delete") {
      if (confirm("User '" + user + "' löschen?"))
         socket.send({ "event": "userconfig", "object":
                       { "user": user,
                         "action": "del" }});
   }
   else if (action == "store") {
      var rightsMask = 0;
      for (var b = 0; b < rights.length; b++) {
         if ($("#bit_" + user + b).is(":checked"))
            rightsMask += Math.pow(2, b); // 2 ** b;
      }

      socket.send({ "event": "userconfig", "object":
                    { "user": user,
                      "action": "store",
                      "rights": rightsMask }});
   }
   else if (action == "resetpwd") {
      var passwd = prompt("neues Passwort", "");
      if (passwd && passwd != "")
         console.log("Reset password to: "+ passwd);
         socket.send({ "event": "userconfig", "object":
                       { "user": user,
                         "passwd": $.md5(passwd),
                         "action": "resetpwd" }});
   }
   else if (action == "resettoken") {
      socket.send({ "event": "userconfig", "object":
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
         showInfoDialog("Passwords not match or empty");
   }
   else
      showInfoDialog("Missing login!");
}

window.addUser = function()
{
   // console.log("Add user: " + $("#input_user").val());

   socket.send({ "event": "userconfig", "object":
                 { "user": $("#input_user").val(),
                   "passwd": $.md5($("#input_passwd").val()),
                   "action": "add" }
               });

   $("#input_user").val("");
   $("#input_passwd").val("");
}
