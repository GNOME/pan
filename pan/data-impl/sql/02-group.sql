create table if not exists `group` (
  id integer primary key asc autoincrement,
  name text not null,

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
  read_ranges text,

  -- data extracted from newsgroup.xov, field 2
  total_article_count integer,

  -- data extracted from newsgroup.xov, field 3.
  -- TODO: check if redundant with read_ranges
  unread_article_count integer,

  -- remaining fields of newsgroup.xov are stored in table server_group

  subscribed boolean check (subscribed in (False, True))
);

create unique index if not exists group_name on `group` (name);

create table if not exists server_group (
  id integer primary key asc autoincrement,
  server_id integer references server (id) on delete cascade,
  group_id integer references `group` (id) on delete cascade,
  -- contains data extracted from newsgroups.xov file.  This may
  -- contains the article count of a newsgroup on a specific server.
  xover_high integer
);

create unique index if not exists server_group_idx on server_group (server_id, group_id);

-- In my tests, there's about 14k descriptions for 150k groups. So
-- using a separate table is probably good
create table if not exists group_description (
  group_id integer references `group` (id) on delete cascade,
  description blob -- might not be utf-8
);

create unique index if not exists group_desc_idx on group_description (group_id);

-- remove groups that are no longer attached to a server, i.e. its
-- only remaining server was deleted
create trigger if not exists delete_orphan_groups after delete on server
  begin
    delete from `group` where ( select count() from server_group
                                 where server_group.server_id == OLD.id) == 0;
  end;

-- Add local groups
insert into `group` (name) values ('Sent'),('Drafts') on conflict do nothing;

-- Assign local groups to local server
insert into server_group (server_id, group_id) values (
  (select id from `server` where host = 'local'),
  (select id from `group` where name = 'Sent')
) on conflict do nothing;

-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
