
create table if not exists article (
  id integer primary key asc autoincrement,
  flag boolean check(flag in (false,true)) default false, 
  message_id text not null unique, 
  author_id integer not null references author (id) on delete restrict,
  subject_id integer not null references subject (id) on delete restrict,
  `references` text, -- json data

  time_posted integer not null,

  -- default 0 otherwise line_count accumulation does not work
  -- in SQL null + 3 => null
  line_count integer default 0,

  --  marked boolean check(marked = False or marked = True),
  binary boolean check(binary = False or binary = True),

  -- Single, Incomplete, Complete
  part_state text check( part_state in ('S','I','C')),

  -- 1 for text article, potentially a lot for binaries
  expected_parts integer,

  cached boolean check(cached = False or cached = True) default False,
  is_read boolean check(is_read = False or is_read = True) default False
);

create unique index if not exists article_message_id on `article` (message_id);

create table if not exists author (
  id integer primary key asc autoincrement,
  author text not null unique
);

-- offload subject in its own table, because it is seldom used in query and quite big.
create table if not exists subject (
  id integer primary key asc autoincrement,
  subject text not null unique
);

create table if not exists article_group (
  id integer primary key asc autoincrement,
  article_id integer not null references article (id) on delete cascade,
  group_id integer not null  references `group` (id) on delete cascade,

  -- score is computed by pan.
  score integer default 0
);

create unique index if not exists article_group_ag on article_group (article_id, group_id);

create index if not exists article_group_id on `article_group` (group_id);

create table if not exists article_xref (
  id integer primary key asc autoincrement,
  article_group_id integer not null references article_group (id) on delete cascade,
  server_id integer not null references server (id) on delete cascade,

  -- article number in server. Can be set to 0, in which case the
  -- article is retrieved by message_id
  number integer not null
);

create unique index if not exists article_xref_ids on article_xref (article_group_id, server_id);

-- check and remove orphaned article, i.e. articles that are no longer
-- attached any group and server, i.e. a group was removed or a server was removed
-- this should be taken care of at runtime, but let's cleanup on startup as well
delete from `article` where id in (
  select distinct a.id
    from `article` as a
         left outer join article_group as ag on ag.article_id == a.id
   where ag.article_id is null
);

create table if not exists article_part (
  id integer primary key asc autoincrement,
  article_id integer not null references article (id) on delete cascade,
  part_number integer not null,
  part_message_id text not null,
  size integer not null
);

create index if not exists article_part_article_id
  on `article_part` (article_id);
create unique index if not exists article_part_msg_id
  on `article_part` (part_message_id);
create unique index if not exists article_part_art_id_pt_nb
  on `article_part` (article_id, part_number);

-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
