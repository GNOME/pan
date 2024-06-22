create table if not exists `group` (
  id integer primary key asc autoincrement,
  name text not null,

  subscribed boolean check (subscribed in (False, True)) default False
);

create unique index if not exists group_name on `group` (name);

create table if not exists server_group (
  id integer primary key asc autoincrement,
  server_id integer references server (id) on delete cascade,
  group_id integer references `group` (id) on delete cascade,

  -- From Thomas T: In theory these are denormalisations because if a
  -- server holds the group it should in general hold all the articles
  -- in the group. However, we don't fetch information from all
  -- servers so it's not entirely, and in any case a specific server
  -- may not have got all the articles yet.

  -- In theory, a group should contain a list of articles, each with a read/not-read status.
  -- In practice, retrieving these status can be quite long.
  -- Anyway, pan internal data use these ranges, so I'll stick with them for now. I may
  -- revisit this and remove this attribute if storing a status per article is viable
  -- (which may require subtle usage of indexes and views)

  -- stored as a list of ranges
  read_ranges text
);

create unique index if not exists server_group_idx on server_group (server_id, group_id);


-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
