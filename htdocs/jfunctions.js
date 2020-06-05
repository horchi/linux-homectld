
function confirmSubmit(msg)
{
    if (confirm(msg))
        return true;

    return false;
}

function showContent(elm)
{
    if (document.getElementById(elm).style.display == "block")
        document.getElementById(elm).style.display = "none"
    else
        document.getElementById(elm).style.display = "block"
}

function readonlyContent(elm, chk)
{
    var elm = document.querySelectorAll('[id*=' + elm + ']');  var i;

	 if (chk.checked == 1)
    {
		  for (i = 0; i < elm.length; i++)
        {
			   elm[i].readOnly = false;
			   elm[i].style.backgroundColor = "#fff";
		  }
	 }
    else
    {
		  for (i = 0; i < elm.length; i++)
        {
			   elm[i].readOnly = true;
			   elm[i].style.backgroundColor = "#ddd";
		  }
	 }
}

function disableContent(elm, chk)
{
    var elm = document.querySelectorAll('[id*=' + elm + ']');  var i;

	 if (chk.checked == 1)
    {
		  for (i = 0; i < elm.length; i++)
			   elm[i].disabled = false;
	 }
    else
    {
		  for (i = 0; i < elm.length; i++)
			   elm[i].disabled = true;
	 }
}

function showHide(id)
{
    if (document.getElementById) {
        var mydiv = document.getElementById(id);
        mydiv.style.display = mydiv.style.display == 'block' ? 'none' : 'block';
    }
}
