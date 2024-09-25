
create table if not exists article (
  id integer primary key asc autoincrement,
  flag boolean check(flag in (false,true)) default false, 
  message_id text not null unique, 
  subject text not null,
  author_id integer not null references author (id) on delete restrict,
  `references` text, -- somewhat redundant with of parent_id and ghost_parent_id

  -- parent_id is used to create a tree model of the article thread.
  parent_id integer references article (id) on delete set null,

  -- like parent_id, but keep track of article mentioned in references
  -- but missing in DB
  ghost_parent_id integer references ghost (id) on delete set null,

  time_posted integer not null,

  line_count integer,
  --  marked boolean check(marked = False or marked = True),
  binary boolean check(binary = False or binary = True),


  -- 1 for text article, potentially a lot for binaries
  expected_parts integer,

  is_read boolean check(is_read = False or is_read = True) default False
);

create unique index if not exists article_message_id on `article` (message_id);

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

create table if not exists author (
  id integer primary key asc autoincrement,
  author text not null unique
);

create table if not exists article_xref (
  id integer primary key asc autoincrement,
  article_id integer not null,
  group_id integer not null,
  server_id integer not null,

  -- article number in server. Can be set to 0, in which case the
  -- article is retrieved by message_id
  number integer not null, 

  foreign key(article_id) references article (id) on delete cascade,
  foreign key(group_id) references `group` (id) on delete cascade,
  foreign key(server_id) references server (id) on delete cascade
  -- primary key(message_id, group_name, server, number)
);

create index if not exists xref_article_id
  on `article_xref` (article_id);
create unique index if not exists xref_ags
  on `article_xref` (article_id, group_id, server_id);

create index if not exists xref_group_id
  on `article_xref` (group_id);

-- check and remove orphaned article, i.e. articles that are no longer
-- attached any group and server, i.e. a group was removed or a server was removed
-- this should be taken care of at runtime, but let's cleanup on startup as well
delete from `article` where id in (
  select distinct a.id
    from `article` as a
         left outer join article_xref as x on x.article_id == a.id
   where x.article_id is null
);

create table if not exists article_part (
  id integer primary key asc autoincrement,
  article_id integer not null,
  part_number integer not null,
  part_message_id text not null,
  size integer not null,
  foreign key(article_id) references article (id) on delete cascade
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
