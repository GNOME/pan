
create table if not exists article (
  id integer primary key asc autoincrement,
  flag boolean check(flag in (false,true)) default false, 
  message_id text not null unique, 
  subject text not null, 
  author_id integer not null, 
  time_posted integer not null, 
  line_count integer not null, 
 --  marked boolean check(marked = False or marked = True), 
  binary boolean check(binary = False or binary = True), 
  expected_parts integer not null, -- 1 for text article
 --  -- in there in another table ?
 --  refs text not null,
 --
 --  is_read boolean check(is_read = False or is_read = True),
  foreign key(author_id) references author (id) on delete restrict
);

create table if not exists author (
  id integer primary key asc autoincrement,
  name text not null,
  address text not null unique
);

create table if not exists article_group (
  id integer primary key asc autoincrement,
  article_id integer not null references article (id) on delete cascade,
  group_id integer not null  references `group` (id) on delete cascade
);

create unique index if not exists article_group_ag on article_group (article_id, group_id);

create table if not exists article_xref (
  id integer primary key asc autoincrement,
  article_group_id integer not null references article_group (id) on delete cascade,
  server_id integer not null references server (id) on delete cascade,
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

-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
