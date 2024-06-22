create table if not exists `group` (
  id integer primary key asc autoincrement,
  name text not null,
  subscribed boolean check (subscribed in (False, True))
);

create unique index if not exists group_name on `group` (name);

create table if not exists server_group (
  id integer primary key asc autoincrement,
  server_id integer references server (id) on delete cascade,
  group_id integer references `group` (id) on delete cascade
);

create unique index if not exists server_group_idx on server_group (server_id, group_id);

create view if not exists subscribed_group as
  select name from 'group' where subscribed = 1 order by name asc;

-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
