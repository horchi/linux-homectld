/*
 *  images.js
 *
 *  (c) 2020-2026 Jörg Wendel
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 *  The setup page 'Images': the user images (img/user/) as tiles and the upload,
 *  the styles are in images.css
 */

function initImages()
{
   // console.log("Images: " + JSON.stringify(images, undefined, 4));

   $('#container').removeClass('hidden');
   $('#container').empty();

   prepareSetupMenu();

   let userImages = (images || []).filter(function(p) { return p && p.indexOf('img/user/') == 0; }).sort();
   let grid = $('<div></div>').addClass('imgGrid');

   // the upload: click or drop an image, the name is taken from the file (without extension)

   let preview = $('<img></img>').attr('id', 'previewImg').addClass('imgPreview hidden');
   let drop = $('<div></div>').addClass('imgDrop')
       .append($('<span></span>').addClass('mdi mdi-image-plus-outline imgDropIcon'))
       .append($('<span></span>').addClass('imgDropText').text('Bild auswählen oder hierher ziehen'))
       .append(preview)
       .click(function() { selectImage(); })
       .on('dragover', function(e) { e.preventDefault(); $(this).addClass('imgDropOver'); })
       .on('dragleave drop', function() { $(this).removeClass('imgDropOver'); })
       .on('drop', function(e) {
          e.preventDefault();
          let files = e.originalEvent.dataTransfer ? e.originalEvent.dataTransfer.files : null;
          if (files && files.length)
             loadImageFile(files[0]);
       });

   grid.append($('<div></div>').addClass('imgTile imgUpload').attr('id', 'addImageDiv')
               .append(drop)
               .append($('<div></div>').addClass('imgUploadFields')
                       .append($('<input></input>')
                               .attr('id', 'uploadName')
                               .attr('placeholder', 'Name (ohne Endung)')
                               .attr('title', 'Namen ohne Endung!')
                               .addClass('rounded-border input'))
                       .append($('<button></button>')
                               .attr('type', 'button')
                               .addClass('rounded-border buttonOptions imgUploadBtn')
                               .append($('<span></span>').addClass('mdi mdi-upload'))
                               .append($('<span></span>').text('Hochladen'))
                               .click(function() { uploadImage(); }))));

   for (let path of userImages) {
      let name = path.substring(path.lastIndexOf('/') + 1);

      grid.append($('<div></div>').addClass('imgTile')
                  .attr('title', path)
                  .append($('<div></div>').addClass('imgThumb')
                          .append($('<img></img>').attr('src', path).attr('loading', 'lazy').attr('alt', name)))
                  .append($('<div></div>').addClass('imgInfo')
                          .append($('<span></span>').addClass('imgName').text(name))
                          .append($('<button></button>')
                                  .attr('type', 'button')
                                  .attr('title', 'Löschen')
                                  .addClass('iconButton iconRed imgDelete')
                                  .append($('<span></span>').addClass('mdi mdi-trash-can-outline'))
                                  .click(function() { delImage(path); }))));
   }

   $('#container').append($('<div></div>')
                          .addClass('imgPage')
                          .append($('<div></div>').addClass('imgPageHead')
                                  .append($('<span></span>').addClass('mdi mdi-image-multiple-outline'))
                                  .append($('<span></span>').text('Images'))
                                  .append($('<span></span>').addClass('imgCount').text(userImages.length))
                                  .append($('<span></span>').addClass('imgPath').text('img/user/')))
                          .append(grid));

   // calc container size

   $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('footer') - sab - 10);
   window.onresize = function() {
      $("#container").height($(window).height() - getTotalHeightOf('menu') - getTotalHeightOf('footer') - sab - 10);
   };
}

function delImage(path, action)
{
   if (confirm("Image '" + path + "' löschen?"))
      socket.send({ "event": "imageconfig", "object":
                    { "path": path,
                      "action": "delete" }});
}

// the selected / dropped file into the preview, its name (without extension) as upload name if none is set

function loadImageFile(file)
{
   if (!file || !file.type || file.type.indexOf('image/') != 0) {
      showInfoDialog({ 'status': -1, 'message': 'Das ist kein Bild' });
      return;
   }

   let reader = new FileReader();

   reader.onload = function() {
      $('#previewImg').attr('src', reader.result).removeClass('hidden');
      $('.imgDropIcon, .imgDropText').addClass('hidden');

      if ($('#uploadName').val() == '')
         $('#uploadName').val(file.name.replace(/\.[^.]+$/, ''));
   };

   reader.readAsDataURL(file);
}

function selectImage()
{
   var input = document.createElement('input');
   input.type = 'file';
   input.accept = 'image/*';
   input.onchange = e => { loadImageFile(e.target.files[0]); };
   input.click();
}

function uploadImage()
{
   var output = document.getElementById('previewImg');

   if (!output || !output.src || output.classList.contains('hidden')) {
      showInfoDialog({ 'status': -1, 'message': 'Erst ein Bild auswählen' });
      return;
   }

   if ($('#uploadName').val() == '') {
      showInfoDialog({ 'status': -1, 'message': 'Name fehlt' });
      return;
   }

   console.log("upload file: ", $('#uploadName').val());
   socket.send({ 'event': 'imageconfig', 'object':
                 { 'action': 'upload',
                   'name': $('#uploadName').val(),
                   'data' : output.src }});
}
