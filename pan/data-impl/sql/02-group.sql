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

-- remove groups that are no longer attached to a server, i.e. its
-- only remaining server was deleted
create trigger if not exists delete_orphan_groups after delete on server
  begin
    delete from `group` where ( select count() from server_group where server_group.server_id == OLD.id) == 0;
  end

-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
