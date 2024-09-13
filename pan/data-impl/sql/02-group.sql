pragma foreign_keys = on;

-- speed up writing in file DB. This is necessary due to the amount of writes
-- performed when loading new articles
-- see https://www.sqlite.org/pragma.html#pragma_synchronous
pragma synchronous = off;

pragma case_sensitive_like=on;

create table if not exists `group` (
  id integer primary key asc autoincrement,
  name text not null,

  subscribed boolean check (subscribed in (False, True)) default False,

  migrated boolean check (subscribed in (False, True)) default False,

  -- some news server like gmane use pseudo groups to flag spam.
  -- e.g gmane.spam.detected
  pseudo boolean check (subscribed in (False, True)) default False,

  -- n means posting not okay; m means moderated.
  -- Since almost all groups allow posting, Pan assumes that as the default.
  -- Only moderated or no-posting groups are listed here.
  permission text  check (permission in ("y","n","m")) default "y",

  -- data extracted from newsgroup.xov, field 2
  total_article_count integer,

  -- data extracted from newsgroup.xov, field 3.
  -- TODO: check if redundant with read_ranges
  unread_article_count integer
  -- remaining fields of newsgroup.xov are stored in table server_group
);

create unique index if not exists group_name on `group` (name);

create table if not exists server_group (
  id integer primary key asc autoincrement,
  server_id integer not null references server (id) on delete cascade,
  -- a group must not be deleted if it's still provided by a server
  group_id integer not null references `group` (id) on delete restrict,


  -- This contains the highest article number found for a newsgroup on
  -- a specific server.  see "high water mark" in
  -- https://www.rfc-editor.org/rfc/rfc3977#section-6.1.1.2
  -- used to be stored in newsgroups.xov file
  xover_high integer,

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

-- In my tests, there's about 14k descriptions for 150k groups. So
-- using a separate table is probably good
create table if not exists group_description (
  group_id integer unique not null references `group` (id) on delete cascade,
  description blob -- might not be utf-8
);

create unique index if not exists group_desc_idx on group_description (group_id);

-- remove groups that are no longer attached to a server, i.e. its
-- only remaining server was deleted by user
-- create trigger if not exists delete_orphan_groups_server after delete on server
--   begin
--     delete from `group` where ( select count() from server_group
--                                  where server_group.server_id == OLD.id) == 0;
--   end;

-- check and remove orphaned group, i.e. groups that are no longer
-- attached to a server, i.e. either a server was removed by user, or
-- a group was removed from all its server (when old groups are
-- cleaned up after a refresh group list)
create trigger if not exists delete_orphan_groups after delete on `server`
  begin
    delete from `group` where id in (
      select distinct g.id from `group` as g
        left outer join server_group as sg on g.id == sg.group_id
        where sg.group_id is null
    );
  end;

-- cleanup orphaned group on startup (may happen after a crash)
delete from `group` where id not in (
  select distinct group_id from server_group
);

-- Add local groups
insert into `group` (name) values ('Sent'),('Drafts') on conflict do nothing;

-- Assign local groups to local server
insert into server_group (server_id, group_id) values (
  (select id from `server` where host = 'local'),
  (select id from `group` where name = 'Sent')
),(
  (select id from `server` where host = 'local'),
  (select id from `group` where name = 'Drafts')
) on conflict do nothing;

-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
