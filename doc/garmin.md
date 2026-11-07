[← Back to the overview](../README.md)

<a id="garmin-activities"></a>
# Garmin Activities

The tab *Garmin* shows the activities of a Garmin Connect account (e.g. recorded with a Fenix
watch), grouped by activity type. It is offered when the Garmin login exists (`/etc/homectld/garmin`).

- installation and the one time login: see `garmin/README.md` (`garmin.py login`, works with and
  without two factor authentication, the tokens are valid for about a year)
- nothing is polled - the activities are fetched only with the button *Abgleich mit Garmin*, which asks
  for the period (since the last activity, since a date or all). Data volume about 3-4 kB per activity,
  tracks are not downloaded
- the activities are stored in the table `activities`, not as sensors and not in `samples`
- groups per type are collapsible and show count, total duration, total distance and the maximum speed;
  water sports (windsurfing, kiteboarding, sailing, ...) show the speeds in knots, running and walking as pace
- *Details* shows the values of the activity. The sync already stores most of them from the list
  entry (speeds, heart rate, calories, elevation, training effect, min/max temperature, ...) as a
  preliminary version; *Details nachladen* fetches the rest from Garmin (min. heart rate, average
  temperature), the dot in the list marks the fully loaded ones
- a click on a row opens the details, the pencil button a dialog to change the type (e.g. activities
  recorded with a wrong profile), the name and the location at Garmin, the trash button deletes the
  activity at Garmin (after confirmation); both need control rights
- bulk edit: check boxes in the rows (the one in the group header selects the whole group, a click with
  Shift or Ctrl selects all rows from the last clicked one to this one), *Auswahl bearbeiten* changes
  type, name and/or location of all selected activities, empty fields stay as they are; Garmin has no
  bulk update, garmin.py sends one request per activity with all changed fields (about 1 s each)
- filters: type, period, name and location (name / location are regular expressions, the location
  field suggests the known locations); a click on a column header sorts the lists (again: reverse)
- *Track* in the details loads the GPS track, simplified by Garmin to 1000 points or at least one
  point per 15 seconds, and shows it on a map colored by speed, hovering a segment shows speed, time
  and altitude; details and tracks are cached in the tables `activities` / `activitytracks`,
  *Neu laden* fetches them again
- distance check: the watch sums up (almost) no distance in some water sport sessions although the GPS
  track is complete (e.g. 6 m for 35 minutes of windsurfing). When a track is loaded its length (sum of
  the segments) is stored in `activities.trackdistance`; if Garmin's distance deviates by more than
  the configured percentage (*Distanz aus dem Track ab Abweichung*, default 50 %, 0 = off) the list,
  the group sums and the details show the track's distance (in italics, the details show both values).
  The sync of the list does not load tracks, so the check only applies to activities with a loaded track
- *Karte*: world map with the start positions of the activities as points colored by type (the
  legend filters by type), a click on a point opens the track; the period, name and location filters apply
- the maps have a *Zentrieren* button which brings the whole route / all points into view

[← Back to the overview](../README.md)
