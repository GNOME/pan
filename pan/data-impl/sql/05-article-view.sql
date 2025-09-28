-- used to construct the list of shown article of a group this is used
-- by my-tree.cc
create table if not exists article_view
  (
    id integer primary key asc autoincrement,
    article_id integer unique references article (id) on delete cascade,
    parent_id integer references article (id) on delete set null,
    -- track status of article in article_view
    -- value is (e)xposed: article was exposed, i.e added to article_view and header list
    -- (u)nchanged: article not changed
    -- (r)eparented: article reparented, i.e. its parent_id was changed
    --               following addition or deletion of its parent
    -- (h)idden: article was filtered out and will be removed from article_view and header list
    status text check (status in ('r','u','h','e')) not null
  );

create index if not exists article_view_parent_id on `article_view` (parent_id);
create index if not exists article_view_status on `article_view` (status);

-- no foreign keys as the articles were removed from article table
create table if not exists removed_article
  (
    message_id text unique
  );

create trigger if not exists removed_article
  after delete on article
  begin
    insert into removed_article values (old.message_id);
  end;

-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
