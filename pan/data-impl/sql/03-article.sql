
create table if not exists article (
  id integer primary key asc autoincrement,
  flag boolean check(flag in (false,true)) default false, 
  message_id text not null unique, 
  author_id integer not null references author (id) on delete restrict,
  subject_id integer not null references subject (id) on delete restrict,
  `references` text, -- somewhat redundant with of parent_id and ghost_parent_id

  -- parent_id is used to create a tree model of the article thread.
  parent_id integer references article (id) on delete set null,

  -- like parent_id, but keep track of article mentioned in references
  -- but missing in DB
  ghost_parent_id integer references ghost (id) on delete set null,

  time_posted integer not null,

  -- default 0 otherwise line_count accumulation does not work
  -- in SQL null + 3 => null
  line_count integer default 0,

  -- bytes is made of the sum of the article parts. I need to
  -- denormalize this as sorting column can be done on
  -- bytes. Computing bytes from article_part while sorting would be
  -- very long. Anyway article_part is never updated, so no worry.
  bytes integer default 0,

  --  marked boolean check(marked = False or marked = True),
  binary boolean check(binary = False or binary = True),

  -- Single, Incomplete, Complete
  part_state text check( part_state in ('S','I','C')),

  -- 1 for text article, potentially a lot for binaries
  expected_parts integer,

  -- used to mark article pending deletion. Article is eventually
  -- deleted after GUI update
  to_delete boolean check(to_delete = False or to_delete = True) default False,

  cached boolean check(cached = False or cached = True) default False,
  is_read boolean check(is_read = False or is_read = True) default False
);

create unique index if not exists article_message_id on `article` (message_id);
create index if not exists article_to_delete on `article` (to_delete);
create index if not exists article_parent_id on `article` (parent_id);

-- filled with message_id extracted from references header for missing
-- articles this table is emptied when articles are found or when
-- their children article are deleted
create table if not exists ghost (
  id integer primary key asc autoincrement,
  ghost_msg_id text not null unique,
  -- cannot self reference id as ghost article are removed when a real article is found
  -- can be null if there's no ancestor
  ghost_parent_msg_id integer
);

create index if not exists ghost_parent_msg_id on ghost (ghost_parent_msg_id);

create table if not exists author (
  id integer primary key asc autoincrement,
  author text not null unique
);

-- offload subject in its own table, because it is seldom used in query and quite big.
create table if not exists subject (
  id integer primary key asc autoincrement,
  subject text not null unique
);
create index if not exists subject_idx on subject (subject);

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

create trigger if not exists update_article_bytes after insert on article_part
  begin
    update article set bytes = bytes + new.size where id = new.article_id;
  end;


-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
