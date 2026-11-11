/*
 *  p4/schema.js
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  The tab 'Schema' of the page of the Fröling S 3200 (p4/page.js): an own plant picture of the heating
 *  (config 'p4Schema', img/p4/schema/schema-<name>.png) with the sensors of the heating (types P4...)
 *  placed on it. It is the schema of the main menu (schema.js) with the set 'p4': the elements, the editing
 *  ('Setup Schema' in the burger menu, rights settings) and the storing are the same, only the picture and
 *  the elements (types P4VA, P4DO, ..., P4UC for texts, P4CN for lines) are the ones of the heating.
 */

p4RegisterTab({ id: 'schema', title: 'Schema', show: p4ShowSchema });

function p4ShowSchema()
{
   schemaEditActive = false;
   schemaSet = 'p4';

   if (!config.p4Schema) {
      $('#container').empty().append($('<div></div>').addClass('p4Menu')
                                     .append($('<div></div>').addClass('p4MenuError rounded-border')
                                             .text('Kein Bild gewählt (Konfiguration -> Heizung S 3200 -> Bild des Schemas).')));
      return;
   }

   $('#container').empty();
   socket.send({ 'event': 'schema', 'object': { 'set': 'p4' } });
}

function p4SchemaReceived(data)
{
   if (currentPage != 'p4menu' || p4Tab != 'schema')
      return;

   schemaSet = 'p4';
   initSchema(data);
}
