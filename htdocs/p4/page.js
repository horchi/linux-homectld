/*
 *  p4/page.js
 *
 *  (c) 2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  The page of the Fröling S 3200 in the main menu (name 'p4PageTitle', default 'S3200', only with 'p4Enabled')
 *  is a frame with tabs in a sub menu below the main menu (like the setup pages have):
 *
 *     Service-Menü   p4/menu.js       (done)
 *     Fehler         p4/errors.js     (planned, phase 5)
 *     Pellets        p4/pellets.js    (planned, phase 6)
 *     Schema         (open, a dedicated scheme view of the heating)
 *
 *  A tab is registered by p4RegisterTab({ id, title, show, rights }) in the file of the tab, 'show()' fills
 *  #container. The selected tab is remembered.
 */

var p4Tabs = [];
var p4Tab = null;

function p4RegisterTab(tab)
{
   p4Tabs.push(tab);
}

function p4VisibleTabs()
{
   let rights = parseInt(localStorage.getItem(storagePrefix + 'Rights')) || 0;

   return p4Tabs.filter(function(t) { return !t.rights || (rights & t.rights); });
}

// the sub menu below the main menu, called from prepareSetupMenu() (main.js) for the page 'p4menu'

function p4PrepareSubMenu()
{
   $('#p4SubMenu').remove();

   $("#navMenu").append($('<div></div>')
                        .attr('id', 'p4SubMenu')
                        .addClass('setupMenu'));

   for (const tab of p4VisibleTabs()) {
      $("#p4SubMenu")
         .append($('<button></button>')
                 .addClass('rounded-border button2' + (tab.id == p4Tab ? ' active' : ''))
                 .html(tab.title)
                 .click(function() { p4SelectTab(tab.id); }));
   }

   if (typeof initScrollStrip == 'function')
      initScrollStrip('p4SubMenu');
}

function p4SelectTab(id)
{
   let tab = p4VisibleTabs().find(function(t) { return t.id == id; });

   if (!tab)
      tab = p4VisibleTabs()[0];

   if (!tab)
      return;

   p4Tab = tab.id;

   try { localStorage.setItem(storagePrefix + 'p4Tab', p4Tab); } catch (e) { }

   p4PrepareSubMenu();
   $('#container').removeClass('hidden');
   tab.show();
}

// the page was selected in the main menu

function initP4Page()
{
   let stored = null;

   try { stored = localStorage.getItem(storagePrefix + 'p4Tab'); } catch (e) { }

   p4SelectTab(p4Tab || stored);
}
