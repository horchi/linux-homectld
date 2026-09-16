-- ---------------------------------------------------------------------------
-- cleanup-false-zeros.sql
--
-- Removes the false 0 samples written by the first store after a daemon start
-- (homectld 0.1.17 .. 0.1.21: loadIoStates() reset the values received during
-- the init to 0) and repairs the peaks poisoned by them.
--
-- usage:  mysql -u <user> -p<password> <database> < cleanup-false-zeros.sql
--
--   1. adapt the parameters below (@since, @dry_run, the marker sensors)
--   2. run with @dry_run = 1 and check the listed candidates
--   3. set @dry_run = 0 and run again - this deletes the candidates and
--      recalculates the poisoned peaks (min over the sensor's history,
--      some seconds per sensor)
--
-- How the false zeros are found: the marker sensors are sensors which are
-- never 0 in reality (e.g. a CPU temperature script sensor). Their 0 samples
-- mark the first store after each start. At these times all 0 samples are
-- candidates; deleted are the ones of the marker sensors and those whose
-- previous or next sample is not 0. Sensors which were 0 before and after the
-- start (genuine zeros) are kept.
-- ---------------------------------------------------------------------------

set @since   := '2026-08-01';      -- first start with 0.1.17 on this host
set @dry_run := 1;                 -- 1: only list, 0: delete and repair

drop temporary table if exists tmp_marker;
create temporary table tmp_marker (type varchar(8), address int unsigned, primary key (type, address));

insert into tmp_marker values
   ('DS18', 1626598978);  -- pool temp (use a sensor which never 0 in reality since @since)

-- ---------------------------------------------------------------------------
-- the start store times
-- ---------------------------------------------------------------------------

drop temporary table if exists tmp_times;
create temporary table tmp_times (time datetime primary key);

insert ignore into tmp_times
   select s.time
     from samples s
     join tmp_marker m on m.type = s.type and m.address = s.address
    where s.aggregate = 'S' and s.value = 0 and s.time >= @since;

select 'start store times' as info, count(*) as count, min(time) as first, max(time) as last from tmp_times;

-- ---------------------------------------------------------------------------
-- the 0 samples at these times
-- ---------------------------------------------------------------------------

drop temporary table if exists tmp_cand;
create temporary table tmp_cand (type varchar(8), address int unsigned, time datetime,
                                 prev float, next float, marker tinyint, remove tinyint,
                                 primary key (type, address, time));

insert into tmp_cand
   select s.type, s.address, s.time, null, null,
          exists (select 1 from tmp_marker m where m.type = s.type and m.address = s.address),
          0
     from tmp_times t
     join samples s on s.time = t.time
    where s.aggregate = 'S' and s.value = 0;

-- ---------------------------------------------------------------------------
-- their neighbours (the previous / next sample within 2 hours, start stores
-- excluded). Done row by row in a procedure: with constant values the lookup
-- is an index range, as correlated subquery MariaDB scans the whole history
-- of the sensor for each row (minutes instead of milliseconds).
-- The procedure is dropped at the end.
-- ---------------------------------------------------------------------------

drop procedure if exists tmp_cleanup_neighbours;

delimiter //

create procedure tmp_cleanup_neighbours()
begin
   declare v_type varchar(8);
   declare v_addr int unsigned;
   declare v_time datetime;
   declare v_done int default 0;
   declare cur cursor for select type, address, time from tmp_cand;
   declare continue handler for not found set v_done = 1;

   open cur;

   fetch_loop: loop
      fetch cur into v_type, v_addr, v_time;

      if v_done then
         leave fetch_loop;
      end if;

      update tmp_cand
         set prev = (select p.value from samples p
                      where p.address = v_addr and p.type = v_type and p.aggregate = 'S'
                        and p.time < v_time and p.time > v_time - interval 2 hour
                        and p.time not in (select time from tmp_times)
                      order by p.time desc limit 1),
             next = (select n.value from samples n
                      where n.address = v_addr and n.type = v_type and n.aggregate = 'S'
                        and n.time > v_time and n.time < v_time + interval 2 hour
                        and n.time not in (select time from tmp_times)
                      order by n.time limit 1)
       where type = v_type and address = v_addr and time = v_time;
   end loop;

   close cur;
end //

delimiter ;

call tmp_cleanup_neighbours();
drop procedure tmp_cleanup_neighbours;

update tmp_cand set remove = (marker or coalesce(prev, 0) <> 0 or coalesce(next, 0) <> 0);

select 'candidates - deleted with @dry_run = 0' as info;
select type, address, time, prev, next, marker from tmp_cand where remove order by type, address, time;

select 'kept - genuine zeros (0 before and after the start)' as info;
select type, address, count(*) as count from tmp_cand where not remove group by type, address order by type, address;

-- ---------------------------------------------------------------------------
-- delete
-- ---------------------------------------------------------------------------

delete s
  from samples s
  join tmp_cand c on c.type = s.type and c.address = s.address and c.time = s.time
 where s.aggregate = 'S' and s.value = 0 and c.remove and @dry_run = 0;

select if(@dry_run, 'dry run - nothing deleted', concat(row_count(), ' samples deleted')) as info;

-- ---------------------------------------------------------------------------
-- peaks poisoned by the start stores
-- ---------------------------------------------------------------------------

drop temporary table if exists tmp_peaks;
create temporary table tmp_peaks (type varchar(8), address int unsigned, minv float, timemin datetime, primary key (type, address));

insert into tmp_peaks
   select p.type, p.address, null, null
     from peaks p
     join tmp_times t on t.time = p.timemin
    where p.minv = 0;

select 'poisoned peaks (minv 0 at a start time)' as info;
select p.type, p.address, p.minv, p.timemin, p.maxv, p.timemax from peaks p join tmp_peaks k on k.type = p.type and k.address = p.address order by p.type, p.address;

-- recalculate (after the delete, so only with @dry_run = 0)

update tmp_peaks k
   set k.minv = (select min(s.value) from samples s where s.type = k.type and s.address = k.address and s.aggregate = 'S')
 where @dry_run = 0;

update tmp_peaks k
   set k.timemin = (select min(s.time) from samples s where s.type = k.type and s.address = k.address and s.aggregate = 'S' and s.value = k.minv)
 where @dry_run = 0;

update peaks p
  join tmp_peaks k on k.type = p.type and k.address = p.address
   set p.minv = k.minv, p.timemin = k.timemin
 where @dry_run = 0 and k.timemin is not null;

select if(@dry_run, 'dry run - peaks unchanged', 'peaks repaired') as info;
select p.type, p.address, p.minv, p.timemin from peaks p join tmp_peaks k on k.type = p.type and k.address = p.address where @dry_run = 0 order by p.type, p.address;
