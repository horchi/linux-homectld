/*
 *  commands.js
 *
 *  (c) 2020-2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  The setup page 'Commands': a card per command of the daemon (commands2Json), grouped by
 *  'group', the styles are in commands.css
 */

function initCommands()
{
   $('#container').removeClass('hidden');
   $('#container').html('');

   prepareSetupMenu();

   let page = $('<div></div>').addClass('cmdPage');
   let groups = {};

   $('#container').append(page);

   for (let i = 0; i < commands.length; i++) {
      let command = commands[i];
      let group = command.group || 'Allgemein';

      if (!groups[group]) {
         groups[group] = $('<div></div>').addClass('cmdGrid');
         page.append($('<div></div>').addClass('cmdGroup')
                     .append($('<div></div>').addClass('cmdGroupTitle')
                             .append($('<span></span>').addClass('mdi ' + (command.groupIcon || 'mdi-cog-outline')))
                             .append($('<span></span>').text(group)))
                     .append(groups[group]));
      }

      let card = $('<div></div>').addClass('cmdCard').addClass(command.style ? 'cmd' + command.style : '');
      let button = $('<button></button>')
          .attr('type', 'button')
          .addClass('rounded-border buttonOptions cmdRun')
          .append($('<span></span>').addClass('mdi mdi-play'))
          .append($('<span></span>').text('Ausführen'))
          .click(function() {
             console.log("send command with " + command.cmd);
             socket.send({ "event" : "command", "object" : { "what" : command.cmd } });

             // a short feedback, blocks a double click (the result comes as message of the daemon)

             button.prop('disabled', true);
             card.addClass('cmdSent');
             setTimeout(function() {
                button.prop('disabled', false);
                card.removeClass('cmdSent');
             }, 2000);
          });

      card.append($('<div></div>').addClass('cmdIcon')
                  .append($('<span></span>').addClass('mdi ' + (command.icon || 'mdi-console-line'))))
         .append($('<div></div>').addClass('cmdText')
                 .append($('<div></div>').addClass('cmdTitle').text(command.title))
                 .append($('<div></div>').addClass('cmdDesc').text(command.description || '')))
         .append(button);

      groups[group].append(card);
   }

   $("#container").height($(window).height() - getTotalHeightOf('menu') - 8);
   window.onresize = function() {
      $("#container").height($(window).height() - getTotalHeightOf('menu') - 8);
   };
}
