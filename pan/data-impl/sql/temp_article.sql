drop table if exists article_view;
drop table if exists added_article;
drop table if exists removed_article;
drop table if exists reparented_article;

-- temporary tables used to construct the list of shown article of a group
-- this is used by my-tree.cc
create table article_view
  (
    id integer primary key asc autoincrement,
    article_id integer references article (id) on delete cascade,
    parent_id integer references article (id) on delete set null,
    has_child boolean,
    shown integer default True
  );

-- to update header-pane, we must give the list of added article
create table added_article
  (
    article_id integer references article (id) on delete cascade,
    added boolean default False
  );

-- to update header-pane, we must give the list of removed article
create table removed_article
  (
    article_id integer references article (id) on delete cascade,
    removed boolean default False
  );

-- to update header-pane, we must give the list of article where the
-- parent_id was modified (because a parent article was filtered in or
-- out)
create table reparented_article
  (
    article_id integer references article (id) on delete cascade,
    reparented boolean default False
  );

-- create temp trigger if not exists added_article
--   after update of shown on article_view
--   when old.shown == False and new.shown == True
--   begin
--     insert into added_article set added = True where article_id = new.id
--   end
  
-- create temp trigger if not exists removed_article
--   after update of shown on article_view
--   when old.shown == True and new.shown == False
--   begin
--     insert into removed_article set removed = True where article_id = new.id
--   end

-- create temp trigger if not exists reparented_article
--   after update of shown on article_view
--   when old.parent_id != new.parent_id
--   begin
--     insert into reparented_article set reparented = True where article_id = new.id
--   end
                
-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
