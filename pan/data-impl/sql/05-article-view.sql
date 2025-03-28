-- temporary tables used to construct the list of shown article of a group
-- this is used by my-tree.cc
create table if not exists article_view
  (
    id integer primary key asc autoincrement,
    article_id integer unique references article (id) on delete cascade,
    parent_id integer references article (id) on delete set null,
    init boolean default False,
    show integer default True
  );

-- to update header-pane, we must give the list of newly exposed
-- articles
create table if not exists exposed_article
  (
    article_id integer unique references article (id) on delete cascade
  );

-- to update header-pane, we must give the list of newly hidden
-- articles
create table if not exists hidden_article
  (
    article_id integer unique references article (id) on delete cascade
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
create temp trigger if not exists exposed_article
  after update of show on article_view
  when old.show == False and new.show == True
  begin
    insert into exposed_article values (new.article_id);
  end;

-- header pane may start with a reduced list of article (e.g. unread
-- articles) which can be completed later (e.g.  user wants to view
-- all articles)
create temp trigger if not exists added_article
  after insert on article_view
  when new.show == True and new.init == False
  begin
    insert into exposed_article values (new.article_id);
  end;
  
create temp trigger if not exists hidden_article
  after update of show on article_view
  when old.show == True and new.show == False
  begin
    insert into hidden_article values (new.article_id);
  end;

create temp trigger if not exists reparented_article
  after update of show on article_view
  when old.parent_id != new.parent_id
  begin
    insert into reparented_article values (new.article_id);
  end;
                
-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
