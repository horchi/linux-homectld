/*
 *  p4/errors.js
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  The tab 'Fehler' of the page of the Fröling S 3200 (p4/page.js): the error list of the control,
 *  kept by the homectld in the table 'p4errors' (p4service.c). It is updated by the service
 *  p4mqttd (at an error, every 10 minutes), the button reads it from the control at once.
 */

p4RegisterTab({ id: 'errors', title: 'Fehler', show: p4ShowErrors });

function p4ShowErrors()
{
   $('#container').empty().append($('<div></div>').addClass('p4Menu p4Errors'));
   socket.send({ 'event': 'p4errors', 'object': {} });
}

function p4ErrorsRefresh()
{
   showProgressDialog(90000);
   socket.send({ 'event': 'p4errors', 'object': { 'action': 'refresh' } });
}

function p4Duration(seconds)
{
   if (seconds < 0)
      seconds = 0;

   let d = Math.floor(seconds / 86400);
   let h = Math.floor(seconds % 86400 / 3600);
   let m = Math.floor(seconds % 3600 / 60);
   let s = seconds % 60;

   if (d)
      return d + ' d ' + h + ' h';
   if (h)
      return h + ' h ' + m + ' min';
   if (m)
      return m + ' min ' + s + ' s';

   return s + ' s';
}

function p4ErrorsReceived(data)
{
   hideProgressDialog();

   if (currentPage != 'p4menu' || p4Tab != 'errors')
      return;

   let $page = $('<div></div>').addClass('p4Menu p4Errors');

   if (data.error) {
      $page.append($('<div></div>').addClass('p4MenuError rounded-border').text(data.error));
      $('#container').empty().append($page);
      setupContainerHeight();
      return;
   }

   $page.append($('<div></div>').addClass('p4MenuActions')
                .append($('<button></button>').addClass('rounded-border button1')
                        .text('Von der Heizung lesen')
                        .attr('title', 'Liest die Fehlerliste jetzt aus der Steuerung (sonst bei einer Störung und alle 10 Minuten)')
                        .prop('disabled', !data.online)
                        .click(p4ErrorsRefresh)));

   let $table = $('<table></table>').addClass('tableMultiCol p4ErrorTable')
       .append($('<thead></thead>').append($('<tr></tr>')
                                           .append($('<td></td>').text('Zeit'))
                                           .append($('<td></td>').text('Fehler'))
                                           .append($('<td></td>').text('Status'))
                                           .append($('<td></td>').text('Dauer'))));
   let $body = $('<tbody></tbody>');

   for (const e of (data.errors || [])) {
      let last = Math.max(e.time1, e.time4, e.time2);
      let end = e.time2 ? e.time2 : (e.time4 ? e.time4 : data.now);
      let open = e.state == 'gekommen';

      $body.append($('<tr></tr>').addClass(open ? 'p4ErrorOpen' : '')
                   .append($('<td></td>').text(new Date(last * 1000).toLocaleString(dateLocale)))
                   .append($('<td></td>').text(e.text).attr('title', 'Nummer ' + e.number + '/' + e.info))
                   .append($('<td></td>').text(e.state))
                   .append($('<td></td>').text(p4Duration(end - e.time1))));
   }

   if (!(data.errors || []).length)
      $body.append($('<tr></tr>').append($('<td></td>').attr('colspan', 4).text('Keine Fehler')));

   $table.append($body);
   $page.append($table);
   $('#container').empty().append($page);
   setupContainerHeight();
}
