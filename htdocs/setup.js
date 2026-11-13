/*
 *  setup.js
 *
 *  (c) 2020-2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

var theConfigdetails = {};
var setupCategory = '';

// ----------------------------------------------------------------
// Base Setup
// ----------------------------------------------------------------

//***************************************************************************
// ComboChoice - labels of the suggestions (the daemon stores value and label, the label is
//   resolved here so the daemon doesn't block on it); one resolver per config item
//***************************************************************************

var comboLabelResolvers = {
   // Windy: the data endpoint of the widget is a script 'window.wf<appid> = { spotInfo: { spotName }, .. }'
   'windyAppSpotID': function(value, done) {
      if (!config.windyAppID || !/^\d+$/.test(value))
         return done(null);

      let script = document.createElement('script');
      script.src = 'https://windy.app/widget/data.php?id=wf' + config.windyAppID + '&spotID=' + value;
      script.onload = function() {
         let data = window['wf' + config.windyAppID];
         script.remove();
         done(data && data.spotInfo && data.spotInfo.spotName ? String(data.spotInfo.spotName).trim() : null);
      };
      script.onerror = function() { script.remove(); done(null); };
      document.body.appendChild(script);
   }
};

// resolve the labels which are still the plain value, one after the other (the Windy script
//   uses one global for all spots); the daemon stores them, the datalist is patched locally

function comboResolveLabels(item)
{
   let resolver = comboLabelResolvers[item.name];

   if (!resolver)
      return;

   let pending = (item.options || []).filter(function(o) { return !o.label || o.label == o.value; });

   let next = function() {
      let o = pending.shift();

      if (!o)
         return;

      resolver(o.value, function(label) {
         if (label && label != o.value) {
            o.label = label;
            $('#combolist_' + item.name + ' option').filter(function() { return this.value == o.value; }).text(label);
            let showLabel = $('#input_' + item.name).data('showLabel');
            if (showLabel) showLabel();
            socket.send({ "event" : "storeconfig", "object" : { "action" : "label", "name" : item.name, "value" : o.value, "label" : label } });
         }
         next();
      });
   };

   next();
}

// the icons of the categories (Material Design Icons), unknown ones get the default

var configCategoryIcons = {
   'Daemon'                    : 'mdi-cog-outline',
   'WEB Interface'             : 'mdi-web',
   'Mail'                      : 'mdi-email-outline',
   'MQTT'                      : 'mdi-access-point-network',
   'Heizung S 3200'            : 'mdi-fire-circle',
   'GPS'                       : 'mdi-crosshairs-gps',
   'Garmin'                    : 'mdi-watch',
   'DECONZ'                    : 'mdi-zigbee',
   'HomeMatic'                 : 'mdi-home-automation',
   'HASP Panel'                : 'mdi-tablet-dashboard',
   'Home Automation Interface' : 'mdi-home-assistant',
   'Logitech Media Server'     : 'mdi-speaker',
   'VDR'                       : 'mdi-television-classic',
   'Sensors'                   : 'mdi-thermometer',
   'User Defined'              : 'mdi-account-cog-outline'
};

function configCategoryIcon(category)
{
   return configCategoryIcons[category] || 'mdi-tune-variant';
}

function initConfig(configdetails = null)
{
   if (configdetails)
      theConfigdetails = configdetails;

   showControlContainer();
   $('#container').removeClass('hidden');
   $('#container').empty();

   prepareSetupMenu();

   // die zuletzt gewählte Kategorie wiederherstellen, sofern sie (noch) existiert

   let categories = [];

   for (var i = 0; i < theConfigdetails.length; i++)
      if (!categories.includes(theConfigdetails[i].category))
         categories.push(theConfigdetails[i].category);

   if (setupCategory == '')
      setupCategory = localStorage.getItem(storagePrefix + 'setupCategory');

   if (!categories.includes(setupCategory) && setupCategory != 'User Defined')
      setupCategory = categories.length ? categories[0] : '';

   $("#controlContainer")
      .empty()
      .append($('<div></div>')
              .append($('<button></button>')
                      .attr('id', 'btnStoreIoSetup')
                      // .attr('disabled', true)
                      .addClass('rounded-border tool-button')
                      .html('Speichern')
                      .click(function() { storeConfig(); }))
              .append($('<div></div>')
                      .addClass('button-group-spacing'))
             );

   // the page: head with the category and a filter, a card per item in the grid (styles in setup.css)

   let filter = $('<input></input>')
       .attr('type', 'search')
       .attr('placeholder', 'Filter ...')
       .addClass('rounded-border input cfgFilter')
       .on('input', function() {
          let f = $(this).val().toLowerCase();
          $('#setupContainer .cfgItem[data-search]').each(function() {
             $(this).toggle(f == '' || $(this).attr('data-search').includes(f));
          });
       });

   dlgContent = ($('<div></div>')
                 .attr('id', 'setupContainer')
                 .addClass('cfgGrid'));

   $('#container').append($('<div></div>')
                          .addClass('cfgPage')
                          .append($('<div></div>')
                                  .addClass('cfgPageHead')
                                  .append($('<div></div>')
                                          .addClass('cfgPageTitle')
                                          .append($('<span></span>').addClass('mdi ' + configCategoryIcon(setupCategory)))
                                          .append($('<span></span>').text(setupCategory || 'Konfiguration'))
                                          .append($('<span></span>').addClass('cfgCount')
                                                  .text(theConfigdetails.filter(function(c) { return c.category == setupCategory; }).length)))
                                  .append(filter))
                          .append(dlgContent));

   let haveUserDefined = categories.includes('User Defined');

   // die Kategorie Buttons

   let addCategoryButton = function(category) {
      $("#controlContainer").append($('<div></div>')
                                    .append($('<button></button>')
                                            .addClass('rounded-border tool-button cfgCatButton')
                                            .attr('title', category)
                                            .toggleClass('cfgCatActive', setupCategory == category)
                                            .click(function(event) {
                                               setupCategory = category;
                                               localStorage.setItem(storagePrefix + 'setupCategory', setupCategory);
                                               initConfig(theConfigdetails);
                                            })
                                            .append($('<span></span>').addClass('mdi ' + configCategoryIcon(category)))
                                            .append($('<span></span>').text(category))));
   };

   for (const category of categories)
      addCategoryButton(category);

   for (var i = 0; i < theConfigdetails.length; i++) {
      let item = theConfigdetails[i];
      let choiceSel = null;

      if (item.category != setupCategory) {
         // console.log("Skipping category", item.category)
         continue;
      }

      // the card: title and name, the description, the value (a bool: the switch right of the title)

      let title = $('<span></span>').addClass('cfgTitle');
      let head = $('<div></div>').addClass('cfgHead').append(title)
          .append($('<span></span>').addClass('cfgName').attr('title', item.name).text(item.name));
      let card = $('<div></div>')
          .attr('id', 'div_' + item.name)
          .attr('data-search', (item.title + ' ' + item.name + ' ' + (item.description || '')).toLowerCase())
          .addClass('cfgItem')
          .toggleClass('cfgWide', [4, 8, 10].includes(item.type))
          .append(head)
          .on('input change', function() { $(this).addClass('cfgChanged'); });

      if (setupCategory == "User Defined") {
         title.addClass('cfgEdit')
            .attr('title', 'bearbeiten')
            .on('click', function(e) {
               e.preventDefault();
               addEditConfigItem(item.name);
            })
            .append($('<i></i>').addClass('mdi mdi-lead-pencil'))
            .append(document.createTextNode(item.title));
      }
      else
         title.html(item.title);

      if (item.description)
         card.append($('<div></div>').addClass('cfgDesc').text(item.description));

      let itemsDiv = $('<div></div>').addClass('cfgValue');

      if (item.type == 3)       // ctBool
         itemsDiv.insertBefore(head.find('.cfgName'));
      else
         card.append(itemsDiv);

      $(dlgContent).append(card);

      switch (item.type) {
      case 0:     // ctInteger
         $(itemsDiv)
            .append($('<span></span>')
                    .append($('<input></input>')
                            .attr('id', 'input_' + item.name)
                            .attr('type', 'number')
                            .addClass('rounded-border inputNum')
                            .val(config[item.name])));
         break;

      case 1:     // ctNumber (float)
         $(itemsDiv)
            .append($('<span></span>')
                    .append($('<input></input>')
                            .attr('id', 'input_' + item.name)
                            .attr('type', 'number')
                            .attr('step', '0.1')
                            .addClass('rounded-border inputFloat')
                            .val(parseFloat(config[item.name].replace(',', '.')))));
         break;

      case 2:     // ctString
         $(itemsDiv)
            .append($('<span></span>')
                    .append($('<input></input>')
                            .attr('id', 'input_' + item.name)
                            .attr('type', 'search')
                            .addClass('rounded-border input')
                            .val(config[item.name])));
         break;

      case 3:     // ctBool
         $(itemsDiv)
            .append($('<span></span>')
                    .append($('<input></input>')
                            .attr('id', 'checkbox_' + item.name)
                            .attr('type', 'checkbox')
                            .addClass('rounded-border input')
                            .prop('checked', config[item.name] == 1))
                    .append($('<label></label>')
                            .prop('for', 'checkbox_' + item.name)));
         break;

      case 4:     // ctRange
      {
         $(itemsDiv).append(rangeDiv = $('<span></span>')
                       .addClass('cfgRanges'));
         function appendRangeTuple(index, name, range, description = '')
         {
            var aRange = range.split("-");

            $(rangeDiv)
               .append($('<span></span>')
                       .append($('<input></input>')
                               .attr('id', 'range_' + name + index + 'From')
                               .attr('type', 'search')
                               .addClass('rounded-border inputTime')
                               .val(aRange[0] ? aRange[0] : ''))
                       .append($('<span></span>')
                               .html('bis'))
                       .append($('<input></input>')
                               .attr('id', 'range_' + name + index + 'To')
                               .attr('type', 'search')
                               .addClass('rounded-border inputTime')
                               .val(aRange[1] ? aRange[1] : '')));
         }

         let n = 0;

         if (config[item.name] != '') {
            var ranges = config[item.name].split(",");
            for (n = 0; n < ranges.length; ++n)
               appendRangeTuple(n, item.name, ranges[n]);
         }
         appendRangeTuple(n, item.name, '', item.description);

         break;
      }
      case 5:    // ctChoice
         $(itemsDiv)
            .append($('<span></span>')
                    .append(choiceSel = $('<select></select>')
                            .attr('id', 'input_' + item.name)
                            .addClass('rounded-border input')
                            .prop('name', 'style')));

         if (item.options != null) {
            for (var o = 0; o < item.options.length; o++) {
               $(choiceSel).append($('<option></option>')
                                   .val(item.options[o])
                                   .html(item.options[o])
                                   .attr('selected', item.options[o] == config[item.name]));
            }
         }
         break;

      case 6:    // ctMultiSelect
         $(itemsDiv)
            .append(choiceSel = $('<span></span>')
                    .append($('<input></input>')
                            .attr('id', 'mselect_' + item.name)
                            .attr('type', 'text')
                            .addClass('rounded-border input')
                            .data('index', i)
                            .data('value', config[item.name])
                            .prop('name', 'style')
                            .val('')
                           ));
         break;

      case 7:    // ctBitSelect
         let bmGroup = null;
         $(itemsDiv)
            .append(bmGroup = $('<span></span>')
                    .addClass('cfgBits')
                    .attr('id', 'bmaskgroup_' + item.name));

         var array = config[item.name].split(',');

         for (var o = 0; o < item.options.length; o++) {
            var checked = false;
            for (var n = 0; n < array.length; n++) {
               if (array[n] == item.options[o])
                  checked = true;
            }
            $(bmGroup)
               .append($('<input></input>')
                       .attr('id', 'bmask' + item.name + '_' + item.options[o])
                       .attr('type', 'checkbox')
                       .prop('checked', checked)
                       .addClass('rounded-border input'))
               .append($('<label></label>')
                       .prop('for', 'bmask' + item.name + '_' + item.options[o])
                       .html(item.options[o]));
         }
         break;

      case 9:     // ctComboChoice - text input, the values stored before as suggestions (datalist: label and value)
      {
         let comboInp = $('<input></input>')
             .attr('id', 'input_' + item.name)
             .attr('type', 'text')
             .attr('list', 'combolist_' + item.name)
             .addClass('rounded-border input')
             .val(config[item.name]);

         let comboList = $('<datalist></datalist>').attr('id', 'combolist_' + item.name);

         for (let o of item.options || [])
            comboList.append($('<option></option>').val(o.value).html(o.label && o.label != o.value ? o.label : ''));

         // the label of the entered value as overlay at the right of the input (display only)

         let comboLabel = $('<span></span>').addClass('comboLabel');

         let showLabel = function() {
            let o = (item.options || []).find(function(o) { return o.value == comboInp.val(); });
            comboLabel.text(o && o.label && o.label != o.value ? o.label : '');
         };

         comboInp.on('input change', showLabel);
         comboInp.data('showLabel', showLabel);       // comboResolveLabels() refreshes it
         showLabel();

         let comboSpan = $('<span></span>').addClass('comboChoice')
             .append($('<span></span>').addClass('comboField').append(comboInp).append(comboLabel).append(comboList));

         if (item.options && item.options.length)
            comboSpan.append($('<button></button>')
                             .attr('type', 'button')
                             .addClass('rounded-border tool-button mdi mdi-delete comboForget')
                             .attr('title', 'den eingetragenen Wert aus den Vorschlägen entfernen')
                             .click(function() {
                                if (item.options.some(function(o) { return o.value == comboInp.val(); }))
                                   socket.send({ "event" : "storeconfig", "object" : { "action" : "forget", "name" : item.name, "value" : comboInp.val() } });
                             }));

         $(itemsDiv).append(comboSpan);
         comboResolveLabels(item);
         break;
      }

      case 10:    // ctStringSet - one input per element, stored comma separated
      {
         let setSpan = $('<span></span>')
             .attr('id', 'strset_' + item.name)
             .addClass('stringSet');

         let addBtn = $('<button></button>')
             .attr('type', 'button')
             .addClass('rounded-border tool-button-svg mdi mdi-plus')
             .attr('title', 'Eintrag hinzufügen');

         let addRow = function(value) {
            let inp = $('<input></input>')
                .attr('type', 'search')
                .addClass('rounded-border input')
                .val(value);

            let row = $('<div></div>')
                .addClass('stringSetRow')
                .append(inp)
                .append($('<button></button>')
                        .attr('type', 'button')
                        .addClass('rounded-border tool-button-svg mdi mdi-minus')
                        .attr('title', 'Eintrag entfernen')
                        .click(function() { row.remove(); }));

            row.insertBefore(addBtn);
            return inp;
         };

         setSpan.append(addBtn);
         $(itemsDiv).append(setSpan);

         for (let s of (config[item.name] || '').split(',')) {
            if (s.trim() != '')
               addRow(s.trim());
         }

         addBtn.click(function() { addRow('').focus(); });
         break;
      }

      case 8:     // ctText
      {
         // the parameters of the MQTT sensors (category 'Sensors') are JSON, shown structured

         let value = config[item.name];
         let json = item.category == 'Sensors' ? configJsonPretty(value) : null;

         $(itemsDiv)
            .append($('<span></span>')
                    .append($('<textarea></textarea>')
                            .attr('id', 'input_' + item.name)
                            .attr('type', 'number')
                            .addClass('rounded-border input')
                            .toggleClass('configJson', json != null)
                            .attr('rows', json != null ? Math.min(json.split('\n').length + 1, 30) : null)
                            .attr('spellcheck', json != null ? 'false' : null)
                            .val(json != null ? json : value)));
         break;
      }
      }
   }

   if (!haveUserDefined)
      addCategoryButton("User Defined");

   if (setupCategory == "User Defined")
   {
      $(dlgContent)
         .append($('<button></button>')
                 .attr('type', 'button')
                 .addClass('rounded-border cfgItem cfgAddItem')
                 .append($('<span></span>').addClass('mdi mdi-file-plus-outline'))
                 .append($('<span></span>').text('Neuer Eintrag'))
                 .click(function() { addEditConfigItem(); }));
   }
   else if (!$(dlgContent).children().length)
      $(dlgContent).append($('<div></div>').addClass('cfgEmpty').text('Keine Einträge'));

   $('input[id^="mselect_"]').each(function () {
      var item = theConfigdetails[$(this).data('index')];
      $(this).autocomplete({
         source: item.options,
         multiselect: true});
      if ($(this).data('value').trim() != '')
         setAutoCompleteValues($(this), $(this).data('value').trim().split(','));
   });

   $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('footer') - sab - 8);
   window.onresize = function() {
      $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('footer') - sab - 8);
   };
}

function addEditConfigItem(name = null)
{
   // must match 'enum ConfigItemType' in daemon.h
   const configItemTypes = [
      { type: 0, title: 'Integer' },       // ctInteger
      { type: 1, title: 'Number' },        // ctNum
      { type: 2, title: 'String' },        // ctString
      { type: 3, title: 'Bool' },          // ctBool
      { type: 4, title: 'Range' },         // ctRange
      { type: 5, title: 'Choice' },        // ctChoice
      { type: 6, title: 'MultiSelect' },   // ctMultiSelect
      { type: 7, title: 'BitSelect' },     // ctBitSelect
      { type: 8, title: 'Text' },          // ctText
      { type: 9, title: 'ComboChoice' },   // ctComboChoice
      { type: 10, title: 'StringSet' }     // ctStringSet
   ];

   // Prüfen, ob wir im Bearbeitungsmodus sind
   let isEditMode = (name !== null && name !== undefined && name !== '');
   let currentItem = null;

   if (isEditMode) {
      // Das bestehende Item aus den globalen Konfigurationsdetails suchen
      currentItem = theConfigdetails.find(item => item.name === name);
      if (!currentItem) {
         console.error("Item nicht gefunden:", name);
         return;
      }
   }

   let typeSel = null;
   let form = document.createElement("div");
   $(form).append($('<div></div>')
                  .append($('<div></div>')
                          .append($('<span></span>').html('Name'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'cfgItemName')
                                          .attr('type', 'search')
                                          .addClass('rounded-border inputSetting')
                                          .val(isEditMode ? currentItem.name : '')
                                          .prop('disabled', isEditMode)
                                          )))
                  .append($('<div></div>')
                          .append($('<span></span>').html('Type'))
                          .append($('<span></span>')
                                  .append(typeSel = $('<select></select>')
                                          .attr('id', 'cfgItemType')
                                          .addClass('rounded-border inputSetting')
                                          )))
                  .append($('<div></div>')
                          .append($('<span></span>').html('Title'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'cfgItemTitle')
                                          .attr('type', 'search')
                                          .addClass('rounded-border inputSetting')
                                          .val(isEditMode ? currentItem.title : '')
                                          )))
                  .append($('<div></div>')
                          .append($('<span></span>').html('Description'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'cfgItemDescription')
                                          .attr('type', 'search')
                                          .addClass('rounded-border inputSetting')
                                          .val(isEditMode ? currentItem.description : '')
                                         )))
                  .append($('<div></div>')
                          .css('display', isEditMode ? 'none' : 'block')
                          .append($('<span></span>').html('Value'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'cfgItemValue')
                                          .attr('type', 'search')
                                          .addClass('rounded-border inputSetting')
                                          // Holt den aktuellen Live-Wert aus dem globalen 'config' Objekt
                                          .val(isEditMode ? (config[name] !== undefined ? config[name] : '') : '')
                                         )))
                 );

   for (var t = 0; t < configItemTypes.length; t++) {
      $(typeSel).append($('<option></option>')
                        .val(configItemTypes[t].type)
                        .html(configItemTypes[t].title)
                        .prop('selected', isEditMode && currentItem.type === configItemTypes[t].type));
   }

   $(form).dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      hide: "fade",
      width: "80%",
      title: isEditMode ? "Edit Config Item" : "New Config Item",
      open: function() {
      },
      buttons: {
         'Abbrechen': function () {
            $(this).dialog('close');
         },
         'Speichern': function () {
            socket.send({ "event" : "storeconfig", "object" : {
               'action': isEditMode ? 'edit' : 'add',
               'name': $('#cfgItemName').val(),
               'type': parseInt($('#cfgItemType').val()),
               'title': $('#cfgItemTitle').val(),
               'category': 'User Defined',
               'description': $('#cfgItemDescription').val(),
               'value': $('#cfgItemValue').val()  // ignored in edit mode!
            }});

            $(this).dialog('close');
         }
      },
      close: function() {
         $(this).dialog('destroy').remove();
      }
   });
}

function addConfigItem(name = null)
{
   if (!isEmpty(name))
      return;

   // must match 'enum ConfigItemType' in daemon.h

   const configItemTypes = [
      { type: 0, title: 'Integer' },       // ctInteger
      { type: 1, title: 'Number' },        // ctNum
      { type: 2, title: 'String' },        // ctString
      { type: 3, title: 'Bool' },          // ctBool
      { type: 4, title: 'Range' },         // ctRange
      { type: 5, title: 'Choice' },        // ctChoice
      { type: 6, title: 'MultiSelect' },   // ctMultiSelect
      { type: 7, title: 'BitSelect' },     // ctBitSelect
      { type: 8, title: 'Text' },          // ctText
      { type: 9, title: 'ComboChoice' },   // ctComboChoice
      { type: 10, title: 'StringSet' }     // ctStringSet
   ];

   let typeSel = null;
   let form = document.createElement("div");
   $(form).append($('<div></div>')
                  .append($('<div></div>')
                          .append($('<span></span>').html('Name'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'cfgItemName')
                                          .attr('type', 'search')
                                          .addClass('rounded-border inputSetting')
                                          )))
                  .append($('<div></div>')
                          .append($('<span></span>').html('Type'))
                          .append($('<span></span>')
                                  .append(typeSel = $('<select></select>')
                                          .attr('id', 'cfgItemType')
                                          .addClass('rounded-border inputSetting')
                                          )))
                  .append($('<div></div>')
                          .append($('<span></span>').html('Title'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'cfgItemTitle')
                                          .attr('type', 'search')
                                          .addClass('rounded-border inputSetting')
                                          )))
                  .append($('<div></div>')
                          .append($('<span></span>').html('Description'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'cfgItemDescription')
                                          .attr('type', 'search')
                                          .addClass('rounded-border inputSetting')
                                         )))
                  .append($('<div></div>')
                          .append($('<span></span>').html('Value'))
                          .append($('<span></span>')
                                  .append($('<input></input>')
                                          .attr('id', 'cfgItemValue')
                                          .attr('type', 'search')
                                          .addClass('rounded-border inputSetting')
                                         )))
                 );

   for (var t = 0; t < configItemTypes.length; t++) {
      $(typeSel).append($('<option></option>')
                        .val(configItemTypes[t].type)
                        .html(configItemTypes[t].title));
   }

   $(form).dialog({
      modal: true,
      resizable: false,
      closeOnEscape: true,
      hide: "fade",
      width: "400px",
      title: "New Config Item",
      open: function() {
      },
      buttons: {
         'Abbrechen': function () {
            $(this).dialog('close');
         },
         'Speichern': function () {
            socket.send({ "event" : "storeconfig", "object" : {
               'action': 'add',
               'name': $('#cfgItemName').val(),
               'type': parseInt($('#cfgItemType').val()),
               'title': $('#cfgItemTitle').val(),
               'category': 'User Defined',
               'description': $('#cfgItemDescription').val(),
               'value': $('#cfgItemValue').val()
            }});

            $(this).dialog('close');
         }
      },
      close: function() {
         $(this).dialog('destroy').remove();
      }
   });
}

// JSON object -> indented text, null if it's no JSON object

function configJsonPretty(value)
{
   try {
      let obj = JSON.parse(value);

      if (obj && typeof obj === 'object' && !Array.isArray(obj))
         return JSON.stringify(obj, null, 3);
   }
   catch (e) {}

   return null;
}

function storeConfig()
{
   var jsonObj = {};
   var rootConfig = document.getElementById("container");

   console.log("storeSettings");

   // the JSON parameters of the sensors - check and store them compact

   let jsonError = null;
   let jsonValues = {};

   $(rootConfig).find('textarea.configJson').each(function() {
      try {
         jsonValues[this.id.substring(this.id.indexOf('_') + 1)] = JSON.stringify(JSON.parse(this.value));
         $(this).removeClass('configJsonError');
      }
      catch (e) {
         $(this).addClass('configJsonError');
         jsonError = jsonError || (this.id.substring(this.id.indexOf('_') + 1) + ': ' + e.message);
      }
   });

   if (jsonError) {
      showInfoDialog({ 'status': -1, 'message': 'Nicht gespeichert, ungültiges JSON in ' + jsonError });
      return;
   }

   // ctString, ctNumber, ctInteger, ctChoice, ctText

   var elements = rootConfig.querySelectorAll("[id^='input_']");

   for (var i = 0; i < elements.length; i++) {
      var name = elements[i].id.substring(elements[i].id.indexOf("_") + 1); {
         if (elements[i].getAttribute('type') == 'number' && elements[i].getAttribute('step') != undefined &&
             elements[i].value != '')
            jsonObj[name] = parseFloat(elements[i].value).toLocaleString("de-DE");
         else
            jsonObj[name] = elements[i].value;
      }
   }

   Object.assign(jsonObj, jsonValues);

   // ctBool

   var elements = rootConfig.querySelectorAll("[id^='checkbox_']");

   for (var i = 0; i < elements.length; i++) {
      var name = elements[i].id.substring(elements[i].id.indexOf("_") + 1);
      jsonObj[name] = (elements[i].checked ? "1" : "0");
   }

   // ctRange

   var elements = rootConfig.querySelectorAll("[id^='range_']");

   for (var i = 0; i < elements.length; i++) {
      var name = elements[i].id.substring(elements[i].id.indexOf("_") + 1);
      if (name.match("0From$")) {
         name = name.substring(0, name.indexOf("0From"));
         jsonObj[name] = toTimeRangesString("range_" + name);
         // console.log("value: " + jsonObj[name]);
      }
   }

   // ctMultiSelect -> as string

   var elements = rootConfig.querySelectorAll("[id^='mselect_']");

   for (var i = 0; i < elements.length; i++) {
      var name = elements[i].id.substring(elements[i].id.indexOf("_") + 1);
      var value = getAutoCompleteValues($("#mselect_" + name));
      value = value.replace(/ /g, ',');
      jsonObj[name] = value;
   }

   // ctStringSet -> as comma separated string

   var elements = rootConfig.querySelectorAll("[id^='strset_']");

   for (var i = 0; i < elements.length; i++) {
      var name = elements[i].id.substring(elements[i].id.indexOf("_") + 1);
      var values = [];

      for (let inp of elements[i].querySelectorAll('input')) {
         for (let s of inp.value.split(',')) {
            if (s.trim() != '')
               values.push(s.trim());
         }
      }

      jsonObj[name] = values.join(',');
   }

   // ctBitSelect -> as string

   var elements = rootConfig.querySelectorAll("[id^='bmaskgroup_']");

   for (var i = 0; i < elements.length; i++) {
      var name = elements[i].id.substring(elements[i].id.indexOf("_") + 1);
      var bits = rootConfig.querySelectorAll("[id^='bmask" + name + "_']");
      var value = '';

      for (var i = 0; i < bits.length; i++) {
         var o = bits[i].id.substring(bits[i].id.indexOf("_") + 1);
         if (bits[i].checked)
            value += o + ','
      }
      // console.log("store bitmak: " + value);
      jsonObj[name] = value;
   }

   // console.log(JSON.stringify(jsonObj, undefined, 4));

   jsonObj['action'] = 'store';   // confirmed by homectld also for a page with only one item
   socket.send({ "event" : "storeconfig", "object" : jsonObj });
}

function toTimeRangesString(base)
{
   var times = '';

   for (var i = 0; i < 10; i++) {
      var id = "#" + base + i;

      if ($(id + "From") && $(id + "From").val() && $(id + "To") && $(id + "To").val()) {
         if (times != "") times += ",";
         times += $(id + "From").val() + "-" + $(id + "To").val();
      }
      else {
         break;
      }
   }

   return times;
}
