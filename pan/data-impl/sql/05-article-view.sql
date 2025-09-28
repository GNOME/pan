-- used to construct the list of shown article of a group this is used
-- by my-tree.cc
create table if not exists article_view
  (
    id integer primary key asc autoincrement,
    article_id integer unique references article (id) on delete cascade,
    parent_id integer references article (id) on delete set null,
    -- track status of article in article_view
    -- value is:
    -- (e)xposed: article was exposed, i.e added to article_view and header list
    -- (u)nchanged: article not changed
    -- (r)eparented: article reparented, i.e. its parent_id was changed
    --     following addition or deletion of its parent
    -- (h)idden: article was filtered out and will be removed from article_view and header list
    -- (p)ending_deletion: article is going to be deleted. Sequence is: mark for deletion,
    --     update header-pane (some comparison may be done with deleted article data),
    --     then remove article from article table. Foreign key ensures that
    --     article in article view is also deleted
    status text check (status in ('r','u','h','e','p')) not null
  );

create index if not exists article_view_parent_id on `article_view` (parent_id);
create index if not exists article_view_status on `article_view` (status);

-- Local Variables:
-- mode: sql
-- sql-product: sqlite
-- End:
