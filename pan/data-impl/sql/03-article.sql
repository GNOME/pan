
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

create trigger if not exists article_delete_create_ghost before delete on article
  when (
    -- the deleted article has a real children
    exists (select * from article where parent_id == old.id)
    or
    -- the deleted article has a ghost chidren
    exists (select * from ghost where ghost_parent_msg_id == old.message_id)
    )
  begin
    -- create new ghost article from deleted article
    insert into ghost (ghost_msg_id, ghost_parent_msg_id)
    values (old.message_id, (
      case -- prefer ghost_msg_id over parent_id because the latter is a short-circuit
      when old.ghost_parent_id notnull
        then (select ghost_msg_id from ghost where id == old.ghost_parent_id)
      else  (select message_id from article where id == old.parent_id)
      end
    ));

    -- update ghost_parent of children article only if they don't already have a ghost_parent
    update article
       set ghost_parent_id = (select id from ghost where ghost_msg_id == old.message_id)
     where parent_id == old.id and ghost_parent_id is null;

    -- update parent_id of chidren article to the parent of deleted article
    update article
       set parent_id = old.parent_id
     where parent_id == old.id;
  end;

create trigger if not exists article_delete_ghost after delete on article
  -- when article has a ghost ancestor (no need to check otherwise)
  when old.ghost_parent_id is not null
  begin
    delete from ghost where ghost_msg_id in (
      -- retrieve the list of ghost ancestors of the deleted article
      with recursive p(id,msg_id, p_msg_id) as (
        select id, ghost_msg_id, ghost_parent_msg_id from ghost where ghost_msg_id == ghost.ghost_msg_id
         union all
        select g.id, ghost_msg_id, ghost_parent_msg_id from ghost as g join p on g.ghost_msg_id == p.p_msg_id
	     limit 500 -- safety net
      )
          -- select the id, the join is used to check the ghost id with the ghost parent id
      select distinct p1.msg_id from p as p1, p as p2
       where
         --  delete the ghost that do not have children other that the ones in the list provided by the CTE above (aka p)
         not exists (
           select * from ghost as g
            where g.ghost_parent_msg_id is not null
              and g.ghost_parent_msg_id == p1.msg_id
              and p1.msg_id != p2.msg_id
         )
         and
         -- that do not have real article children
         not exists (
           select * from article as a
            where a.ghost_parent_id is not null
              and p1.id == a.ghost_parent_id
         )
    );
  end;

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
