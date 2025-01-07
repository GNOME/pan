
create table if not exists article (
  id integer primary key asc autoincrement,
  flag boolean check(flag in (false,true)) default false, 
  message_id text not null unique, 
  subject text not null, 
  author_id integer not null, 
  `references` text,
  time_posted integer not null, 
  line_count integer not null, 
  --  marked boolean check(marked = False or marked = True),
  binary boolean check(binary = False or binary = True), 
  expected_parts integer not null, -- 1 for text article

  is_read boolean check(is_read = False or is_read = True) default False,

  foreign key(author_id) references author (id) on delete restrict
);

create unique index if not exists article_message_id on `article` (message_id);

create table if not exists author (
  id integer primary key asc autoincrement,
  author text not null unique
);

create table if not exists article_group (
  id integer primary key asc autoincrement,
  article_id integer not null references article (id) on delete cascade,
  group_id integer not null  references `group` (id) on delete cascade
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

-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
