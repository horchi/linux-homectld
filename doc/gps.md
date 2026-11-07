[← Back to the overview](../README.md)

<a id="gps-tours"></a>
# GPS Tours (recording of trips)

With a GPS receiver (see `gps.u-blox`, sensor type `GPS`) the page *Map* of the WEBIF shows the live position.
In addition trips can be recorded as tours and displayed later. Any receiver delivering NMEA 0183 on a
serial port works (USB pucks, modules on a UART), see the receiver notes in `gps.u-blox/README.md`; the
receiver settings of the GPS configuration are u-blox specific. While a tour is recorded the *Map* tab
of the main menu and the *Touren* button of the tool bar are colored (red, orange while paused), the tour
head shows the driving time (duration minus pauses and ferries); a pause / night stop entered during an automatic
pause takes over its begin, end and duration.

## Recording

- *Map -> ● Tour aufzeichnen*: enter a name and optionally the odometer reading [km] and start the
  recording (needs control rights), *■ Tour beenden* stops it and asks for the odometer at the end.
  Only one tour can be active at a time.
- while a tour is active the tool bar offers buttons for events: fuel stop (liters, total price,
  odometer), toll, pause, night stop, ferry (each with optional costs) and a note. Every event is stored
  with its time and the current GPS position (table `gpstourevents`). The odometer readings at start
  and end are columns of `gpstours`, they can be corrected later together with the name (pencil
  button in the tour list)
- costs and fuel consumption are calculated from the events: total costs (fuel + toll + other),
  consumption per fuel stop = liters / km since the previous full tank and the average over all
  fuel stops with a known distance. The chain starts at the last full fuel stop before the tour
  (of any tour or entered outside of a tour, see below), so the first fill of a tour is not
  charged to the tour's km only; without any earlier fuel stop the odometer of the tour start is
  used (marked *ab Tour-Start*, assumes a full tank at the start). The distance is taken
  from the odometer readings (*Tacho*) and in parallel from the GPS distance of the tour at the
  moment of the fuel stop (*GPS*, stored with the event, tour-bound by nature), both values are
  shown. A fuel stop marked *nicht voll* (partial fill) gets no value of its own, its liters are
  added to the next full one.
  They are shown in the tour list, in the info box of the displayed tour and in the control panel
  while recording
- while a tour is active a point is stored whenever the position moved at least
  *gpsTourMinDistance* meters (Setup -> Configuration -> GPS, default 25 m) since the last stored point.
  Standing still therefore does not produce data.
- without movement for *gpsTourPauseAfter* minutes (default 5) the tour is *paused*, on the next
  movement it continues automatically. The pause time is summed up and shown in the tour list.
  A GPS receiver drifts by 100-200 m at standstill, therefore 'movement' for the pause means
  leaving the radius *gpsTourPauseRadius* (default 200 m) around the position of the last movement;
  while paused no points are stored. Each pause is kept as event *Stillstand* with begin, end and
  duration in the event list of the tour (only its note is editable).
- ferry: the button *Fähre* (only while a tour is active) starts the crossing with the current position and
  optional price / note, the same button ends it (with confirmation, *Tour beenden* ends it too); the tour head
  shows *auf der Fähre*. On the ferry the recording goes on as long as the receiver has a fix (a point only
  on leaving the pause radius), there is no automatic pause. Without GPS (in the hold, homectld switched off)
  no points are stored; ended without a fix the next point is taken as the end position, the distance up to it
  belongs to the ferry. A ferry still open at a restart of the daemon is resumed. Begin, end, end position
  and the distance of the crossing are stored with the event (`gpstourevents`: `ENDLAT`, `ENDLON`,
  `ENDDISTANCE`). The ferry time doesn't count as driving time (*Fahrzeit*), its distance isn't taken for the
  consumption by GPS, its price counts to the total costs
- the active tour survives a restart of the daemon: it is continued automatically, including the
  distance and the point count.
- the recording is bound to the sensor `GPS:0x0a` (*Coordinate*), it has to be active. While a tour is
  active its *Record* flag in *Setup -> IO Setup* is set by the daemon, when the tour is stopped it is cleared.
- u-blox receivers can suppress the drift themselves: *Empfänger: Static Hold* (Setup -> Configuration -> GPS,
  km/h, 0 = off) freezes the position below that speed, *Dynamikmodell Automotive* selects the vehicle
  platform model, *Galileo aktivieren* switches a u-blox 8 to GPS + Galileo + GLONASS (the u-blox 7 has no
  Galileo). The daemon sends them to `gpsmqtt.py` (which owns the serial port) at its start, on change
  and when the script registers, the script writes them into the receiver on every port open (see `gps.u-blox/README.md`).

## Dish alignment

The view *Sat* (satellite button of the tool bar) helps aligning the TV dish: from the live position it
calculates azimuth (true north), elevation and LNB skew to the satellite of *TV-Satellit: Orbitposition*
(Setup -> Configuration -> GPS, default 19.2 = Astra 19.2 E, west negative) and draws the bearing on the map
(dashed blue arrow of 75 m with the azimuth), so that a landmark in that direction can be picked. The vehicle axis is the course of the
last movement (sensor `GPS:0x0b` *Heading* of `gpsmqtt.py`, kept while standing, has to be active), drawn
in blue with the outline of the motorhome (7 x 2.3 m, the GPS position as its center; orange and pointing
north, gray, as long as the heading is unknown); the box shows the
angle to the satellite relative to it ('44° links') with the vehicle from above,
the switch *rückwärts eingeparkt* adds 180° when the vehicle was reversed into the pitch. For a crank mast
(*TV-Satellit: Kurbel*, degrees per turn, default 12 for the Kathrein HDM 140, flat antenna horizontal at 0)
the box shows the crank turns for the elevation, (90° − elevation) / degrees per turn. With the script
sensor `dvb-signal.sh` the box also shows the signal monitor (lock, level, C/N about once per second) with
start / stop. The sensor is configured as *TV-Satellit: Signal-Sensor* (e.g. `SC:0x30`), without it the
view is not offered.

## Storage

- table `gpstours` holds only the tour itself: name, start, stop, distance [m], number of points and pause time [s]
- the points are the samples of `GPS:0x0a` (text `lat/lng`) between start and stop of the tour. With each point
  the speed (`GPS:0x03`) and the altitude (`GPS:0x05`) are stored with the same time stamp (if the sensors are
  active), e.g. for speed or altitude profiles. Outside of a tour nothing is stored by the tour recording
- tour samples have `AGGREGATE = 'T'`; they are never aggregated or deleted by the aggregation, so the tours stay
  complete. Regular samples (`'S'`, sensors with the *Record* flag) are aggregated as before
- deleting a tour in the WEBIF also deletes its samples (coordinate, speed, altitude) from `samples`
  and its events from `gpstourevents`

## Display

- *Map -> Live*: the map follows the live position (button *Live-Tracking* on the map)
- on stop the daemon proposes the arrival at the final position as end of the tour (the time the
  position came within *Tour: Toleranz Ankunft* meters of the final position and stayed there, default
  200 m) - for a forgotten stop or a long wait at the reception; *jetzt* is offered as well
- *Map -> Touren*: the recorded tours are listed by name in the panel on the left, the selected one
  shows its details on the right (start, end, duration, pause, distance, odometer, costs, consumption)
  and the list of its events (time, kind, liters, price, odometer, consumption, note) where events can
  be changed or deleted (control rights); *Karte* draws the tour on the map (green marker = start,
  red marker = end) with the events as icons, their popup shows the details and offers the same
  actions; the route on a ferry is drawn in its own color (teal), without points on the ferry begin and
  end are connected by a dashed line (tooltip with time, duration and km). The details show the ferries
  (count, duration, distance, costs) and *Strecke (GPS)* the driven distance with the ferry separately
  (*+ x km Fähre*, live while on the ferry); the pencil button changes name, odometer readings and a free comment for the whole tour
  (shown above the details), the trash button deletes the tour
  (control rights). The active tour can be displayed too but not deleted. Entering an event never
  changes the view.
- *Zentrieren* (button on the map) brings the whole route into view

<a id="gps-fuel"></a>
## Fuel stops and consumption

- *Map -> Tankstopps* (gas station icon) lists all fuel stops: the ones entered during tours (with a
  link to the tour) and the ones entered here outside of a tour (*Tankstopp eintragen*, control rights)
  with date / time, odometer, liters, total price, *voll getankt* and a note. The position is the
  current GPS position if available
- the consumption is calculated across the tours from full tank to full tank by the odometer readings:
  liters / km since the previous full fuel stop, partial fills are added to the next full one. The head
  shows the average over all counted fills, the total liters and costs and the average price per liter.
  A fuel stop without odometer or the first one gets no value
- the fuel stops are the events of type `fuel` in `gpstourevents`, standalone ones have `TOURID = 0`;
  editing / deleting works from this list as well as from the tour

[← Back to the overview](../README.md)
