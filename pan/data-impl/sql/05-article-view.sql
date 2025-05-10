-- used to construct the list of shown article of a group this is used
-- by my-tree.cc
create table if not exists article_view
  (
    id integer primary key asc autoincrement,
    article_id integer unique references article (id) on delete restrict,
    parent_id integer references article (id) on delete set null,
    -- track status of article in article_view vs header list in header-pane GUI
    -- value is:
    -- (e)xposed: article was exposed, i.e added to article_view and header list,
    --    it needs to be added to header list
    -- (r)eparented: article reparented, i.e. its parent_id was changed
    --    following addition or deletion of its parent. The parent needs to
    --    be updated in header list.
    -- (s)hown: article is displayed in header list.
    -- (h)idden: article was filtered out (or deleted), needs to be removed from
    --    header list and will be removed from article_view
    status text check (status in ('r','s','h','e')) not null,
    -- used in mark and sweep algorithm to find hidden or deleted articles
    mark integer default (0)
  );

-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
