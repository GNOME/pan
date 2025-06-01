-- temporary tables used to construct the list of shown article of a group
-- this is used by my-tree.cc
create table if not exists article_view
  (
    id integer primary key asc autoincrement,
    article_id integer unique references article (id) on delete cascade,
    parent_id integer references article (id) on delete set null,
    show integer default True
  );

create index if not exists article_view_parent_id on `article_view` (parent_id);

-- used to decide when to trigger update of the exposed_article,
-- hidden_article and reparented_article tables
-- There should be only one row in this table.
create table if not exists article_view_status
  (
    id integer primary key asc autoincrement,
    key text unique,
    value boolean default False
  );
insert into article_view_status (key, value) values ("init", True) on conflict do nothing;

-- to update header-pane, we must give the list of newly exposed
-- articles
create table if not exists exposed_article
  (
    article_id integer unique references article (id) on delete cascade
  );

-- to update header-pane, we must give the list of newly hidden
-- articles, hidden means filtered out, not removed
create table if not exists hidden_article
  (
    article_id integer unique references article (id) on delete cascade
  );

-- no foreign keys as the articles were removed from article table
create table if not exists removed_article
  (
    message_id text unique
  );

-- to update header-pane, we must give the list of article where the
-- parent_id was modified (because a parent article was filtered in or
-- out)
create table if not exists reparented_article
  (
    article_id integer unique references article (id) on delete cascade
  );

-- do not create upsert as entries should be cleaned up when
-- updating header list
create trigger if not exists exposed_article
  after update of show on article_view
  when old.show == False and new.show == True
  begin
    insert into exposed_article values (new.article_id);
  end;

create trigger if not exists hidden_article
  after update of show on article_view
  when old.show == True and new.show == False
  begin
    insert into hidden_article values (new.article_id);
  end;

create trigger if not exists removed_article
  after delete on article
  begin
    insert into removed_article values (old.message_id);
  end;

-- article_view.parent_id is not set when inserting new record, but it
-- is updated in a second pass when initializing article_view. The
-- init value is used to avoid storing the article in
-- reparented_article during article_view initialisation.
create trigger if not exists reparented_article
  after update of parent_id on article_view
  when old.parent_id != new.parent_id and new.show == True
       and (select value from article_view_status where key = "init") == False
  begin
    insert into reparented_article values (new.article_id);
  end;
                
-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
