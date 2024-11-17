
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
  author text not null unique
);

create table if not exists article_xref (
  id integer primary key asc autoincrement,
  article_id integer not null,
  group_id integer not null,
  server_id integer not null,
  number integer not null,
  foreign key(article_id) references article (id) on delete cascade,
  foreign key(group_id) references `group` (id) on delete cascade,
  foreign key(server_id) references server (id) on delete cascade
  -- primary key(message_id, group_name, server, number)
);

create table if not exists article_part (
  id integer primary key asc autoincrement,
  article_id integer not null,
  part_number integer not null,
  part_message_id text not null,
  size integer not null,
  foreign key(article_id) references article (id) on delete cascade
);

-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
