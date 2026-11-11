/*
 *  p4/pellets.js
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  The tab 'Pellets' of the page of the Fröling S 3200 (p4/page.js): the pellet deliveries with the
 *  days and the stoker hours between them and the consumption per stoker hour (see p4service.c).
 *  Everybody with the right 'control' sees the list, adding, changing and deleting needs 'full control'.
 */

p4RegisterTab({ id: 'pellets', title: 'Pellets', show: p4ShowPellets, rights: 0x02 });

function p4PelletsMayEdit()
{
   return localStorage.getItem(storagePrefix + 'Rights') & 0x04;
}

// the evaluation reads the stoker hours of the samples for each delivery, this takes a moment

function p4ShowPellets()
{
   $('#container').empty().append($('<div></div>').addClass('p4Menu'));
   showProgressDialog(120000);
   socket.send({ 'event': 'p4pellets', 'object': {} });
}

function p4PelletsReceived(data)
{
   hideProgressDialog();

   if (currentPage != 'p4menu' || p4Tab != 'pellets')
      return;

   if (data.error) {
      $('#container').empty().append($('<div></div>').addClass('p4Menu')
                                     .append($('<div></div>').addClass('p4MenuError rounded-border').text(data.error)));
      return;
   }

   let edit = p4PelletsMayEdit();
   let $page = $('<div></div>').addClass('p4Menu');
   let $table = $('<table></table>').addClass('tableMultiCol p4PelletTable')
       .append($('<thead></thead>').append($('<tr></tr>')
                                           .append($('<td></td>').text('Datum'))
                                           .append($('<td></td>').text('Menge [kg]'))
                                           .append($('<td></td>').text('Preis [€]'))
                                           .append($('<td></td>').text('Notiz'))
                                           .append($('<td></td>').text('Tage'))
                                           .append($('<td></td>').text('€ / t'))
                                           .append($('<td></td>').text('Stoker [h]'))
                                           .append($('<td></td>').text('kg / h'))
                                           .append($('<td></td>'))));
   let $body = $('<tbody></tbody>');

   for (const p of (data.pellets || []))
      $body.append(p4PelletRow(p, edit));

   if (edit)
      $body.append(p4PelletRow({ id: -1, time: Math.floor(Date.now() / 1000), amount: 0, price: 0, comment: '' }, edit));

   $table.append($body);
   $page.append($table);
   $('#container').empty().append($page);
   setupContainerHeight();
}

function p4PelletRow(p, edit)
{
   let sum = p.sum;
   let isNew = p.id == -1 && !sum;
   let $row = $('<tr></tr>').addClass(sum ? 'p4PelletSum' : isNew ? 'p4PelletNew' : '');
   let date = new Date(p.time * 1000);
   let day = date.getFullYear() + '-' + String(date.getMonth() + 1).padStart(2, '0') + '-' + String(date.getDate()).padStart(2, '0');

   function input(type, value, step) {
      return $('<input></input>').attr('type', type).attr('step', step).addClass('rounded-border input')
         .prop('disabled', sum || !edit).val(value);
   }

   let $time = input('date', day);
   let $amount = input('number', p.amount, 1);
   let $price = input('number', p.price, 0.1);
   let $comment = input('search', p.comment);

   $row.append($('<td></td>').append($time))
      .append($('<td></td>').append($amount))
      .append($('<td></td>').append($price))
      .append($('<td></td>').append($comment))
      .append($('<td></td>').text(p.duration != null ? p.duration : ''))
      .append($('<td></td>').text(p.price && p.amount ? (p.price / (p.amount / 1000)).toFixed(2) + ' €' : ''))
      .append($('<td></td>').text(p.stokerHours != null ? p.stokerHours : ''))
      .append($('<td></td>').attr('title', p.consumptionHint || '')
              .text(sum ? p.consumptionDelta + ' kg' : (p.consumptionH ? p.consumptionH.toFixed(2) : '')));

   let $actions = $('<td></td>');

   if (!sum && edit) {
      $actions.append($('<button></button>').addClass('rounded-border buttonOptions')
                      .text(isNew ? 'hinzufügen' : 'ändern')
                      .click(function() {
                         let amount = parseInt($amount.val());
                         let price = parseFloat(String($price.val()).replace(',', '.'));

                         if (!amount || !price || !$time.val()) {
                            showInfoDialog({ 'message': 'Menge, Preis und Datum werden benötigt', 'status': 1 });
                            return;
                         }

                         showProgressDialog(120000);                   // the list is evaluated again
                         socket.send({ 'event': 'p4pelletsadd', 'object':
                                       { 'id': p.id, 'time': new Date($time.val()).getTime() / 1000,
                                         'amount': amount, 'price': price, 'comment': $comment.val() } });
                      }));

      if (!isNew)
         $actions.append($('<button></button>').addClass('rounded-border buttonOptions').css('margin-left', '6px')
                         .text('löschen')
                         .click(function() {
                            confirmDialog(function() {
                               showProgressDialog(120000);
                               socket.send({ 'event': 'p4pelletsadd', 'object': { 'id': p.id, 'delete': true } });
                            }, 'Die Lieferung vom ' + date.toLocaleDateString(dateLocale) + ' (' + p.amount + ' kg) löschen?', 'Löschen', 'Abbrechen');
                         }));
   }

   return $row.append($actions);
}
